#include <stdio.h>
#include <string.h>
#include "fs_test.h"
#include "ram_disk.h"
#include "user/commands.h"
#include "user/shell.h"
#include "user/syscall.h"

/* laptop test of the shell logic.  test_shell <disk image> */

extern char mock_output[];
extern uint32_t mock_output_len;
extern const char *mock_input;

extern struct MockExec {
    int32_t  result;
    uint32_t pid;
    int      calls;
    char     name[256];
    uint32_t parent_inode;
    uint32_t buffer_size;
} mock_exec;
extern int32_t mock_kill_result;
extern uint32_t mock_kill_pid;
extern int mock_kill_calls;
extern struct SyscallProcessInfo mock_ps[16];
extern int32_t mock_ps_count;

static struct FsTestContext ctx;
static int commands_run;

static void print_line(const char *text) {
    fputs(text, stdout);
}

static void check(const char *id, const char *what, bool ok) {
    fs_test_check(&ctx, id, what, ok, 1, ok);
}

static void check_output(const char *id, const char *what, const char *expected) {
    bool ok = strcmp(mock_output, expected) == 0;

    check(id, what, ok);
    if (!ok) {
        printf("     expected \"%s\"\n     actual   \"%s\"\n", expected, mock_output);
    }
}

/* ---------- commands that exist only in this test ---------- */

static int cmd_fruit(int argc, char **argv, struct ShellIO *io) {
    (void)argc;
    (void)argv;
    commands_run++;
    shell_out_str(io, "apple\nbanana\ncherry\n");
    return 0;
}

static int cmd_pass(int argc, char **argv, struct ShellIO *io) {
    (void)argc;
    (void)argv;
    commands_run++;
    if (io->in != NULL) {
        shell_out(io, io->in, io->in_len);
    }
    return 0;
}

/* Output grows ahead of the input, so stages sharing one buffer would corrupt it. */
static int cmd_prefix(int argc, char **argv, struct ShellIO *io) {
    (void)argc;
    (void)argv;
    shell_out_str(io, ">>");
    shell_out(io, io->in, io->in_len);
    return 0;
}

static int cmd_fail(int argc, char **argv, struct ShellIO *io) {
    (void)argc;
    (void)argv;
    commands_run++;
    shell_out_str(io, "fail: broken\n");
    return 7;
}

static void out_bytes(struct ShellIO *io, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        shell_out(io, "z", 1);
    }
}

static int cmd_full(int argc, char **argv, struct ShellIO *io) {
    (void)argc;
    (void)argv;
    out_bytes(io, SHELL_IO_BUFFER);
    return 0;
}

static int cmd_big(int argc, char **argv, struct ShellIO *io) {
    (void)argc;
    (void)argv;
    out_bytes(io, SHELL_IO_BUFFER + 1);
    return 0;
}

static const struct ShellCommand test_commands[] = {
    { "grep", cmd_grep },
    { "fruit", cmd_fruit },
    { "pass", cmd_pass },
    { "prefix", cmd_prefix },
    { "fail", cmd_fail },
    { "full", cmd_full },
    { "big", cmd_big },
};

/* ---------- helpers ---------- */

static void reset_output(void) {
    mock_output[0] = '\0';
    mock_output_len = 0;
    commands_run = 0;
}

static int run(const char *text) {
    char line[SHELL_LINE_MAX * 2];

    snprintf(line, sizeof(line), "%s", text);
    reset_output();
    return shell_execute_commands(line, test_commands, sizeof(test_commands) / sizeof(test_commands[0]));
}

/* Walks the folder table by hand, so the expected inode does not come from the shell code. */
static uint32_t child_inode(uint32_t parent, const char *name) {
    static uint8_t table[65536];
    struct EXT2DriverRequest request = {
        .buf = table, .name = ".", .name_len = 1,
        .parent_inode = parent, .buffer_size = sizeof(table),
    };
    uint32_t offset = 0;

    memset(table, 0, sizeof(table));
    if (sys_read_dir(&request) != 0) {
        return 0;
    }
    while (offset + sizeof(struct EXT2DirectoryEntry) < sizeof(table)) {
        struct EXT2DirectoryEntry entry;

        memcpy(&entry, table + offset, sizeof(entry));
        if (entry.rec_len == 0) {
            break;
        }
        if (entry.inode != 0 && entry.name_len == strlen(name) &&
            memcmp(table + offset + sizeof(entry), name, entry.name_len) == 0) {
            return entry.inode;
        }
        offset += entry.rec_len;
    }
    return 0;
}

static void write_file(uint32_t parent, const char *name, const void *data, uint32_t size) {
    struct EXT2DriverRequest request = {
        .buf = (void *)data, .name = (char *)name, .name_len = (uint8_t)strlen(name),
        .parent_inode = parent, .buffer_size = size,
    };

    check("SETUP", name, sys_write_file(&request) == 0);
}

static void check_dir(const char *id, const char *path, uint32_t inode, const char *absolute) {
    char resolved[SHELL_PATH_MAX];
    uint32_t got = 0;

    check(id, path, shell_resolve_dir(path, &got, resolved) == 0 && got == inode &&
          strcmp(resolved, absolute) == 0);
}

/* ---------- cases ---------- */

static void parser_cases(void) {
    static const char *const bad[] = { "fruit |", "| grep a", "fruit || pass", "|", "fruit | " };
    struct ShellPipeline p;
    char line[SHELL_LINE_MAX];
    char xyz[] = "xyz";

    strcpy(line, "ls -a /docs");
    check("H01", "parse ls -a /docs",
          shell_parse(line, &p) == NULL && p.count == 1 && p.stages[0].argc == 3 &&
          strcmp(p.stages[0].argv[0], "ls") == 0 && strcmp(p.stages[0].argv[1], "-a") == 0 &&
          strcmp(p.stages[0].argv[2], "/docs") == 0);

    strcpy(line, "  cat   a  ");
    check("H02", "extra spaces",
          shell_parse(line, &p) == NULL && p.stages[0].argc == 2 &&
          strcmp(p.stages[0].argv[1], "a") == 0);

    strcpy(line, "cat a | grep x");
    check("H03", "two stages",
          shell_parse(line, &p) == NULL && p.count == 2 && p.stages[0].argc == 2 &&
          p.stages[1].argc == 2 && strcmp(p.stages[1].argv[0], "grep") == 0);

    strcpy(line, "cat a|grep x");
    check("H03", "pipe without spaces",
          shell_parse(line, &p) == NULL && p.count == 2 && strcmp(p.stages[0].argv[1], "a") == 0);

    strcpy(line, "   ");
    check("H03", "blank line is no command", shell_parse(line, &p) == NULL && p.count == 0);

    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int result = run(bad[i]);
        check("H04", bad[i], result == -1 && commands_run == 0 &&
              strcmp(mock_output, "shell: empty command in pipeline\n") == 0);
    }

    run("a b c d e f g h i j k l m n o p");
    check_output("H05", "16 arguments is allowed", "a: command not found\n");
    run("a b c d e f g h i j k l m n o p q");
    check_output("H05", "17 arguments", "shell: too many arguments\n");
    run("fruit | pass | pass | pass");
    check("H05", "4 stages is allowed", commands_run == 4);
    run("fruit | pass | pass | pass | pass");
    check("H05", "5 stages rejected", commands_run == 0 &&
          strcmp(mock_output, "shell: too many commands in pipeline\n") == 0);

    reset_output();
    shell_execute_line(xyz);
    check_output("H06", "unknown command", "xyz: command not found\n");
    run("fruit | xyz");
    check("H06", "unknown later stage runs nothing", commands_run == 0 &&
          strcmp(mock_output, "xyz: command not found\n") == 0);
    check("H06", "blank line is fine", run("   ") == 0 && mock_output_len == 0);
}

static void cwd_cases(void) {
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");
    uint32_t a = child_inode(docs, "a");
    uint32_t b = child_inode(a, "b");
    uint32_t many = child_inode(SHELL_ROOT_INODE, "many");
    uint32_t got = 0;
    char path[SHELL_PATH_MAX];
    char name[SHELL_NAME_MAX + 1];
    char too_long[SHELL_PATH_MAX + 1];

    check("H07", "fixture has docs/a/b", docs != 0 && a != 0 && b != 0);
    check("H07", "starts at root", shell_cwd_inode() == 1 && strcmp(shell_cwd_path(), "/") == 0);

    check_dir("H08", "/", 1, "/");
    check_dir("H08", ".", 1, "/");
    check_dir("H08", "..", 1, "/");
    check_dir("H08", "docs", docs, "/docs");
    check_dir("H08", "docs/a/..", docs, "/docs");
    check_dir("H08", "/docs/..", 1, "/");
    check_dir("H08", "/docs/a/b", b, "/docs/a/b");
    check_dir("H08", "//docs///a/./b/", b, "/docs/a/b");
    check_dir("H08", "/docs/a/b/../../..", 1, "/");
    check_dir("H08", "/many", many, "/many");

    shell_cwd_set(a, "/docs/a");
    check_dir("H08", "b", b, "/docs/a/b");
    check_dir("H08", "..", docs, "/docs");
    check_dir("H08", "../..", 1, "/");
    check_dir("H08", "/many", many, "/many");

    reset_output();
    shell_print_prompt();
    check_output("H08", "prompt shows the CWD", "tapOS:/docs/a$ ");

    memset(too_long, 'x', SHELL_PATH_MAX);
    too_long[SHELL_PATH_MAX] = '\0';
    shell_cwd_set(1, too_long);
    check("H08", "path too long leaves CWD alone", shell_cwd_inode() == a);
    shell_cwd_set(1, "/");

    check("H09", "folder missing", shell_resolve_dir("/nothing", &got, path) == -1);
    check("H09", "missing in the middle", shell_resolve_dir("/nothing/a", &got, path) == -1);
    check("H09", "file is not a folder", shell_resolve_dir("/s1", &got, path) == -1);
    check("H09", "file in the middle", shell_resolve_dir("/s1/a", &got, path) == -1);

    check("H10", "docs/a/new", shell_resolve_parent("docs/a/new", &got, name) == 0 &&
          got == a && strcmp(name, "new") == 0);
    check("H10", "/top", shell_resolve_parent("/top", &got, name) == 0 && got == 1 &&
          strcmp(name, "top") == 0);
    check("H10", "name only", shell_resolve_parent("new", &got, name) == 0 && got == 1 &&
          strcmp(name, "new") == 0);
    check("H10", "trailing slash", shell_resolve_parent("docs/a/", &got, name) == 0 &&
          got == docs && strcmp(name, "a") == 0);
    check("H10", "root has no parent", shell_resolve_parent("/", &got, name) == -1);
    check("H10", "missing parent", shell_resolve_parent("nothing/new", &got, name) == -1);
}

static void grep_cases(void) {
    static char big[16384];
    const char *text = "alpha\nbeta x\ngamma\n\nx last";
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");

    write_file(SHELL_ROOT_INODE, "notes", text, (uint32_t)strlen(text));
    write_file(docs, "inner", "one\ntwo x\n", 10);

    run("grep x notes");
    check_output("H11", "lines from a file", "beta x\nx last\n");
    run("grep nomatch notes");
    check_output("H11", "no match prints nothing", "");
    run("grep X notes");
    check_output("H11", "case sensitive", "");
    run("grep x /docs/inner");
    check_output("H11", "absolute path", "two x\n");

    shell_cwd_set(docs, "/docs");
    run("grep x inner");
    check_output("H11", "path relative to the CWD", "two x\n");
    shell_cwd_set(1, "/");

    memset(big, 'a', sizeof(big));
    memcpy(big + sizeof(big) - 7, "\nneedle", 7);
    write_file(SHELL_ROOT_INODE, "exact", big, sizeof(big));
    run("grep needle exact");
    check_output("H11", "file of exactly 16 KiB", "needle\n");

    run("fruit | grep an");
    check_output("H12", "pipeline into grep", "banana\n");
    run("fruit | pass | pass | grep rr");
    check_output("H12", "four stages", "cherry\n");
    run("fruit | prefix | prefix | prefix");
    check_output("H12", "stages use separate buffers", ">>>>>>apple\nbanana\ncherry\n");
    run("fruit | grep a | grep pp");
    check_output("H12", "grep into grep", "apple\n");
    run("fail | pass");
    check("H12", "failed stage stops the pipeline", commands_run == 1 &&
          strcmp(mock_output, "fail: broken\n") == 0);
    check("H12", "status of the failed stage", run("fail") == 7);

    run("grep x");
    check_output("H13", "no input", "grep: no input\n");
    run("grep");
    check_output("H13", "no arguments", "usage: grep PATTERN [FILE]\n");
    run("grep a b c");
    check_output("H13", "too many arguments", "usage: grep PATTERN [FILE]\n");

    run("grep x nothing");
    check_output("H14", "file missing", "grep: nothing: not found\n");
    run("grep x nothing/file");
    check_output("H14", "parent missing", "grep: nothing/file: not found\n");
    run("grep x docs");
    check_output("H14", "folder", "grep: docs: not a file\n");
    run("grep x s71680");
    check_output("H14", "file over 16 KiB", "grep: s71680: file too large\n");
}

static void buffer_cases(void) {
    run("big");
    check_output("H15", "one byte too many", "pipeline: output too large\n");
    run("big | grep z");
    check_output("H15", "too large in the first stage", "pipeline: output too large\n");
    run("full");
    check("H15", "exactly full is allowed", mock_output_len == SHELL_IO_BUFFER);
    run("full | pass");
    check("H15", "exactly full through a pipe", mock_output_len == SHELL_IO_BUFFER);
}

static void reader_cases(void) {
    char line[SHELL_LINE_MAX];
    char long_input[SHELL_LINE_MAX + 40];

    reset_output();
    mock_input = "ab\bc\n";
    shell_read_line(line);
    check("H16", "backspace edits the line", strcmp(line, "ac") == 0);
    check_output("H16", "echo", "ab\bc\n");

    reset_output();
    mock_input = "\b\b\x01\x02\x03\x04\t\x1b" "a\n";
    shell_read_line(line);
    check("H17", "backspace, arrows and tab ignored", strcmp(line, "a") == 0);
    check_output("H17", "ignored keys are not echoed", "a\n");

    memset(long_input, 'k', sizeof(long_input) - 2);
    long_input[sizeof(long_input) - 2] = '\n';
    long_input[sizeof(long_input) - 1] = '\0';
    reset_output();
    mock_input = long_input;
    shell_read_line(line);
    check("H17", "line stops at SHELL_LINE_MAX", strlen(line) == SHELL_LINE_MAX - 1);
}

/* Runs a line through the real command table, like the REPL does. */
static int run_real(const char *text) {
    char line[SHELL_LINE_MAX * 2];

    snprintf(line, sizeof(line), "%s", text);
    reset_output();
    return shell_execute_line(line);
}

static void set_ps_row(int i, uint32_t pid, uint32_t state, const char *name) {
    mock_ps[i].pid = pid;
    mock_ps[i].state = state;
    memset(mock_ps[i].name, 0, sizeof(mock_ps[i].name));
    memcpy(mock_ps[i].name, name, strlen(name) < sizeof(mock_ps[i].name) ? strlen(name) : sizeof(mock_ps[i].name));
}

static void proc_cases(void) {
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");

    mock_exec = (struct MockExec) { .result = 0, .pid = 5 };
    check("H18", "exec returns 0", run_real("exec hello") == 0);
    check_output("H18", "exec message", "exec: started hello (PID 5)\n");
    check("H18", "exec request", strcmp(mock_exec.name, "hello") == 0 &&
          mock_exec.parent_inode == 1 && mock_exec.buffer_size == 0);

    shell_cwd_set(docs, "/docs");
    mock_exec = (struct MockExec) { .result = 0, .pid = 4294967295u };
    run_real("exec a/prog");
    check("H18", "relative path uses the CWD", mock_exec.parent_inode == child_inode(docs, "a") &&
          strcmp(mock_exec.name, "prog") == 0);
    check_output("H18", "largest PID", "exec: started prog (PID 4294967295)\n");
    run_real("exec /hello");
    check("H18", "absolute path from another folder", mock_exec.parent_inode == 1);
    shell_cwd_set(1, "/");

    mock_exec = (struct MockExec) { .result = 1 };
    run_real("exec clock");
    check_output("H19", "code 1", "exec: too many processes\n");
    mock_exec = (struct MockExec) { .result = 3 };
    run_real("exec clock");
    check_output("H19", "code 3", "exec: not enough memory\n");
    mock_exec = (struct MockExec) { .result = 4 };
    run_real("exec nothing");
    check_output("H19", "code 4", "exec: nothing: not found\n");
    mock_exec = (struct MockExec) { .result = -1 };
    run_real("exec clock");
    check_output("H19", "code -1", "exec: invalid request\n");
    mock_exec = (struct MockExec) { .result = 0 };
    run_real("exec nothing/prog");
    check("H19", "missing folder never reaches the kernel", mock_exec.calls == 0 &&
          strcmp(mock_output, "exec: nothing/prog: not found\n") == 0);
    run_real("exec");
    check_output("H19", "no argument", "usage: exec PATH\n");
    run_real("exec a b");
    check_output("H19", "two arguments", "usage: exec PATH\n");

    mock_ps_count = 3;
    set_ps_row(0, 1, 2, "shell");
    set_ps_row(1, 2, 1, "clock");
    set_ps_row(2, 12345, 1, "abcdefghijklmnopqrstuvwxyz012345");
    run_real("ps");
    check_output("H20", "process table",
                 "PID  STATE    NAME\n"
                 "1    running  shell\n"
                 "2    ready    clock\n"
                 "12345ready    abcdefghijklmnopqrstuvwxyz012345\n");
    run_real("ps | grep clock");
    check_output("H20", "ps through a pipe", "2    ready    clock\n");
    mock_ps_count = 0;
    run_real("ps");
    check_output("H20", "empty list", "PID  STATE    NAME\n");
    mock_ps_count = -1;
    check("H20", "kernel error is a failure", run_real("ps") != 0 &&
          strcmp(mock_output, "ps: cannot read the process list\n") == 0);

    mock_kill_result = 0;
    check("H21", "kill succeeds silently", run_real("kill 7") == 0 && mock_output_len == 0 &&
          mock_kill_pid == 7);
    run_real("kill 4294967295");
    check("H21", "largest PID", mock_kill_pid == 4294967295u);
    mock_kill_result = 1;
    run_real("kill 99");
    check_output("H21", "no such process", "kill: 99: no such process\n");
    mock_kill_result = 2;
    run_real("kill 1");
    check_output("H21", "init refused", "kill: cannot kill init (PID 1)\n");
    mock_kill_result = -1;
    run_real("kill 3");
    check_output("H21", "other failure", "kill: 3: failed\n");

    const char *bad[] = { "kill", "kill abc", "kill 12x", "kill -1", "kill 4294967296", "kill 1 2" };
    mock_kill_calls = 0;
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        run_real(bad[i]);
        check("H21", bad[i], strcmp(mock_output, "usage: kill PID\n") == 0);
    }
    check("H21", "bad PIDs never reach the kernel", mock_kill_calls == 0);
}

/* ---------- E commands (M3) ---------- */

static void fsutil_cases(void) {
    static const char tail[] = { 'a', 'b', 0, 0, 0 };
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");
    uint32_t a = child_inode(docs, "a");
    uint32_t b = child_inode(a, "b");
    const char *data = NULL;
    uint32_t size = 0;
    uint32_t inode = 0;
    uint8_t type = 0;
    struct FsuTarget target;

    write_file(SHELL_ROOT_INODE, "tail", tail, sizeof(tail));
    write_file(SHELL_ROOT_INODE, "empty", "", 0);
    check("H22", "exact size keeps trailing NUL bytes",
          fsu_read_file(SHELL_ROOT_INODE, "tail", &data, &size) == 0 && size == sizeof(tail) &&
          memcmp(data, tail, sizeof(tail)) == 0);
    check("H22", "empty file has size 0",
          fsu_read_file(SHELL_ROOT_INODE, "empty", &data, &size) == 0 && size == 0);
    check("H22", "missing file", fsu_read_file(SHELL_ROOT_INODE, "nothing", &data, &size) == 3);
    check("H22", "folder is not a file", fsu_read_file(SHELL_ROOT_INODE, "docs", &data, &size) == 1);
    check("H22", "read_path folder", strcmp(fsu_read_path("/docs", &data, &size), "is a directory") == 0);
    check("H22", "read_path missing", strcmp(fsu_read_path("/no/x", &data, &size), "not found") == 0);

    check("H23", "lookup folder", fsu_lookup(docs, "a", &inode, &type) == 0 && inode == a &&
          type == EXT2_FT_DIR);
    check("H23", "lookup file", fsu_lookup(SHELL_ROOT_INODE, "tail", NULL, &type) == 0 &&
          type == EXT2_FT_REG_FILE);
    check("H23", "lookup skips dot entries", fsu_lookup(docs, "..", NULL, NULL) == -1);
    check("H23", "stat path", fsu_stat("/docs/a/b", &target) == 0 && target.parent == a &&
          target.inode == b && strcmp(target.name, "b") == 0);
    check("H23", "stat refuses ..", fsu_stat("docs/..", &target) == -1);
    check("H23", "parent", fsu_parent(b) == a && fsu_parent(SHELL_ROOT_INODE) == SHELL_ROOT_INODE);
    check("H23", "inside", fsu_is_inside(b, docs) && fsu_is_inside(b, b) &&
          fsu_is_inside(docs, SHELL_ROOT_INODE) && !fsu_is_inside(docs, b));
}

static void cd_cases(void) {
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");
    uint32_t a = child_inode(docs, "a");

    check("H24", "cd into a folder", run_real("cd docs/a") == 0 && mock_output_len == 0 &&
          shell_cwd_inode() == a && strcmp(shell_cwd_path(), "/docs/a") == 0);
    run_real("cd ..");
    check("H24", "cd .. goes up", shell_cwd_inode() == docs && strcmp(shell_cwd_path(), "/docs") == 0);
    reset_output();
    shell_print_prompt();
    check_output("H24", "prompt follows cd", "tapOS:/docs$ ");
    run_real("cd");
    check("H24", "cd alone goes to root", shell_cwd_inode() == SHELL_ROOT_INODE &&
          strcmp(shell_cwd_path(), "/") == 0);
    run_real("cd /docs/a/b/../..");
    check("H24", "absolute path with ..", shell_cwd_inode() == docs);
    run_real("cd /");
    check("H24", "cd / ", shell_cwd_inode() == SHELL_ROOT_INODE);

    check("H25", "missing folder fails", run_real("cd nothing") != 0 &&
          strcmp(mock_output, "cd: nothing: no such directory\n") == 0 &&
          shell_cwd_inode() == SHELL_ROOT_INODE);
    run_real("cd tail");
    check_output("H25", "file is not a folder", "cd: tail: not a directory\n");
    run_real("cd a b");
    check_output("H25", "two arguments", "usage: cd [DIR]\n");
}

static void ls_cases(void) {
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");

    check("H26", "ls a folder", run_real("ls /docs") == 0 &&
          strcmp(mock_output, "a/\ninner\n") == 0);
    shell_cwd_set(docs, "/docs");
    run_real("ls");
    check_output("H26", "ls uses the CWD", "a/\ninner\n");
    run_real("ls a");
    check_output("H26", "relative folder", "b/\n");
    run_real("ls a/b");
    check_output("H26", "nested folder", "deep.txt\n");
    run_real("ls ..");
    check("H26", "parent folder lists root", strstr(mock_output, "docs/\n") != NULL &&
          strstr(mock_output, "notes\n") != NULL && strstr(mock_output, "..") == NULL);
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    run_real("ls notes");
    check_output("H26", "file path prints itself", "notes\n");
    run_real("ls / | grep docs");
    check_output("H26", "ls through a pipe", "docs/\n");

    check("H27", "missing path", run_real("ls nothing") != 0 &&
          strcmp(mock_output, "ls: nothing: not found\n") == 0);
    run_real("ls a b");
    check_output("H27", "two arguments", "usage: ls [DIR]\n");
}

static void mkdir_cases(void) {
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");
    uint32_t box;

    check("H28", "mkdir in root", run_real("mkdir box") == 0 && mock_output_len == 0);
    box = child_inode(SHELL_ROOT_INODE, "box");
    check("H28", "folder exists and is empty", box != 0 && run_real("ls box") == 0 &&
          mock_output_len == 0);
    check("H28", "new folder can be entered", run_real("cd box") == 0 && shell_cwd_inode() == box);
    run_real("mkdir inner deeper");
    check("H28", "several relative folders", child_inode(box, "inner") != 0 &&
          child_inode(box, "deeper") != 0);
    run_real("cd inner");
    run_real("cd ..");
    check("H28", "child .. points back", shell_cwd_inode() == box);
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    run_real("mkdir /docs/a/new");
    check("H28", "absolute nested path", child_inode(child_inode(docs, "a"), "new") != 0);

    run_real("mkdir box");
    check_output("H29", "folder exists", "mkdir: box: already exists\n");
    run_real("mkdir notes");
    check_output("H29", "a file has the name", "mkdir: notes: already exists\n");
    run_real("mkdir /");
    check_output("H29", "root", "mkdir: /: already exists\n");
    run_real("mkdir nothing/x");
    check_output("H29", "missing parent", "mkdir: nothing/x: no such directory\n");
    run_real("mkdir notes/x");
    check_output("H29", "file as parent", "mkdir: notes/x: no such directory\n");
    check("H29", "one failure keeps going", run_real("mkdir box box2") != 0 &&
          child_inode(SHELL_ROOT_INODE, "box2") != 0);
    run_real("mkdir");
    check_output("H29", "no argument", "usage: mkdir DIR...\n");
}

static void cat_cases(void) {
    static char big[20000];
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");

    check("H30", "cat a file", run_real("cat notes") == 0 &&
          strcmp(mock_output, "alpha\nbeta x\ngamma\n\nx last") == 0);
    run_real("cat /docs/inner notes");
    check_output("H30", "several files in order", "one\ntwo x\nalpha\nbeta x\ngamma\n\nx last");
    shell_cwd_set(docs, "/docs");
    run_real("cat inner");
    check_output("H30", "relative to the CWD", "one\ntwo x\n");
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    run_real("cat notes | grep x");
    check_output("H30", "cat | grep", "beta x\nx last\n");
    run_real("grep a notes | cat");
    check_output("H30", "cat passes pipeline input", "alpha\nbeta x\ngamma\nx last\n");
    run_real("cat empty");
    check("H30", "empty file", mock_output_len == 0);

    check("H31", "missing file", run_real("cat nothing") != 0 &&
          strcmp(mock_output, "cat: nothing: not found\n") == 0);
    run_real("cat docs");
    check_output("H31", "folder", "cat: docs: is a directory\n");
    run_real("cat /");
    check_output("H31", "root", "cat: /: is a directory\n");
    run_real("cat nothing notes");
    check_output("H31", "error then the next file", "cat: nothing: not found\nalpha\nbeta x\ngamma\n\nx last");
    run_real("cat");
    check_output("H31", "no argument and no input", "usage: cat FILE...\n");
    memset(big, 'q', sizeof(big));
    write_file(SHELL_ROOT_INODE, "big", big, sizeof(big));
    run_real("cat big");
    check_output("H31", "larger than one stage", "pipeline: output too large\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || ram_disk_load(argv[1]) != 0) {
        fprintf(stderr, "usage: %s <disk image>\n", argv[0]);
        return 2;
    }
    initialize_filesystem_ext2();

    fs_test_init(&ctx, print_line);
    parser_cases();
    cwd_cases();
    grep_cases();
    buffer_cases();
    reader_cases();
    proc_cases();
    fsutil_cases();
    cd_cases();
    ls_cases();
    mkdir_cases();
    cat_cases();
    fs_test_sum(&ctx, "shell");
    return ctx.failed == 0 ? 0 : 1;
}
