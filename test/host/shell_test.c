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

static void rm_cases(void) {
    uint32_t docs = child_inode(SHELL_ROOT_INODE, "docs");
    uint32_t box = child_inode(SHELL_ROOT_INODE, "box");
    uint32_t before;

    write_file(SHELL_ROOT_INODE, "gone", "x", 1);
    check("H32", "rm a file", run_real("rm gone") == 0 && mock_output_len == 0 &&
          child_inode(SHELL_ROOT_INODE, "gone") == 0);
    write_file(box, "f1", "1", 1);
    write_file(box, "f2", "2", 1);
    run_real("rm box/f1 /box/f2");
    check("H32", "several paths", child_inode(box, "f1") == 0 && child_inode(box, "f2") == 0);
    write_file(SHELL_ROOT_INODE, "gone", "y", 1);
    check("H32", "name can be used again", child_inode(SHELL_ROOT_INODE, "gone") != 0);
    run_real("rm gone");

    // box holds inner/ and deeper/; fill it so rm -r has to go two levels down
    write_file(child_inode(box, "inner"), "leaf", "leaf", 4);
    run_real("mkdir box/inner/sub");
    write_file(child_inode(child_inode(box, "inner"), "sub"), "leaf2", "leaf2", 5);
    check("H33", "folder needs -r", run_real("rm box") != 0 &&
          strcmp(mock_output, "rm: box: is a directory (use rm -r)\n") == 0 &&
          child_inode(SHELL_ROOT_INODE, "box") == box);
    before = child_inode(SHELL_ROOT_INODE, "box2");
    check("H33", "rm -r removes the whole tree", run_real("rm -r box box2") == 0 &&
          mock_output_len == 0 && child_inode(SHELL_ROOT_INODE, "box") == 0 &&
          before != 0 && child_inode(SHELL_ROOT_INODE, "box2") == 0);
    check("H33", "removed folder is gone for ls", run_real("ls box") != 0);
    run_real("mkdir box");
    check("H33", "name free again", child_inode(SHELL_ROOT_INODE, "box") != 0);

    shell_cwd_set(child_inode(docs, "a"), "/docs/a");
    run_real("rm -r /docs");
    check_output("H34", "folder above the CWD", "rm: /docs: is the current directory or above it\n");
    run_real("rm -r .");
    check_output("H34", "dot", "rm: .: cannot remove '.' or '..'\n");
    run_real("rm -r ..");
    check_output("H34", "dot dot", "rm: ..: cannot remove '.' or '..'\n");
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    run_real("rm -r /");
    check_output("H34", "root", "rm: /: cannot remove the root directory\n");
    run_real("rm nothing");
    check_output("H34", "missing", "rm: nothing: not found\n");
    run_real("rm nothing notes");
    check("H34", "error does not stop the rest", child_inode(SHELL_ROOT_INODE, "notes") == 0 &&
          strcmp(mock_output, "rm: nothing: not found\n") == 0);
    write_file(SHELL_ROOT_INODE, "notes", "alpha\nbeta x\ngamma\n\nx last", 25);
    run_real("rm");
    check_output("H34", "no argument", "usage: rm [-r] PATH...\n");
    run_real("rm -r");
    check_output("H34", "only the flag", "usage: rm [-r] PATH...\n");
    run_real("rm notes | typo");
    check("H34", "unknown later stage deletes nothing", child_inode(SHELL_ROOT_INODE, "notes") != 0);
}

/* Reads a whole file by hand for comparisons; returns its size or -1. */
static int file_bytes(uint32_t parent, const char *name, char *out, uint32_t capacity) {
    const char *data;
    uint32_t size;

    if (fsu_read_file(parent, name, &data, &size) != 0 || size > capacity) {
        return -1;
    }
    memcpy(out, data, size);
    return (int)size;
}

static void cp_cases(void) {
    static char big[40000];
    static char back[65536];
    static const char tail[] = { 'a', 'b', 0, 0, 0 };
    uint32_t box = child_inode(SHELL_ROOT_INODE, "box");
    uint32_t tree;
    uint32_t copy;

    check("H35", "cp file to a new name", run_real("cp notes notes2") == 0 && mock_output_len == 0 &&
          file_bytes(1, "notes2", back, sizeof(back)) == 25 &&
          memcmp(back, "alpha\nbeta x\ngamma\n\nx last", 25) == 0);
    run_real("cp tail box");
    check("H35", "into a folder keeps the name, exact bytes",
          file_bytes(box, "tail", back, sizeof(back)) == sizeof(tail) &&
          memcmp(back, tail, sizeof(tail)) == 0);
    run_real("cp /docs/inner notes2");
    check("H35", "existing file is replaced", file_bytes(1, "notes2", back, sizeof(back)) == 10 &&
          memcmp(back, "one\ntwo x\n", 10) == 0);
    run_real("cp notes empty box/");
    check("H35", "several sources into a folder", child_inode(box, "notes") != 0 &&
          child_inode(box, "empty") != 0);
    for (uint32_t i = 0; i < sizeof(big); i++) {
        big[i] = (char)('a' + i % 26);
    }
    write_file(SHELL_ROOT_INODE, "large", big, sizeof(big));
    run_real("cp large box/large");
    check("H35", "40000 bytes (indirect blocks) copied",
          file_bytes(box, "large", back, sizeof(back)) == (int)sizeof(big) &&
          memcmp(back, big, sizeof(big)) == 0);
    run_real("cp empty empty2");
    check("H35", "empty file", file_bytes(1, "empty2", back, sizeof(back)) == 0);

    run_real("mkdir tree tree/sub tree/sub/deep");
    tree = child_inode(SHELL_ROOT_INODE, "tree");
    write_file(tree, "top", "top", 3);
    write_file(child_inode(child_inode(tree, "sub"), "deep"), "leaf", "leaf!", 5);
    check("H36", "folder needs -r", run_real("cp tree t2") != 0 &&
          strcmp(mock_output, "cp: tree: is a directory (use cp -r)\n") == 0 &&
          child_inode(1, "t2") == 0);
    check("H36", "cp -r copies the tree", run_real("cp -r tree t2") == 0 && mock_output_len == 0);
    copy = child_inode(1, "t2");
    check("H36", "copy is a new folder with the content", copy != 0 && copy != tree &&
          file_bytes(copy, "top", back, sizeof(back)) == 3 &&
          file_bytes(child_inode(child_inode(copy, "sub"), "deep"), "leaf", back, sizeof(back)) == 5 &&
          memcmp(back, "leaf!", 5) == 0);
    run_real("cd t2/sub/deep");
    run_real("cd ../..");
    check("H36", ".. inside the copy points into the copy", shell_cwd_inode() == copy);
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    check("H36", "original untouched", child_inode(tree, "top") != 0 &&
          child_inode(child_inode(tree, "sub"), "deep") != 0);
    run_real("cp -r tree box");
    check("H36", "cp -r into a folder", child_inode(child_inode(box, "tree"), "sub") != 0);

    run_real("cp -r tree tree/sub");
    check_output("H37", "into itself", "cp: tree: cannot put a directory inside itself\n");
    check("H37", "nothing created", child_inode(child_inode(tree, "sub"), "tree") == 0);
    check("H37", "existing folder receives the copy", run_real("cp -r tree t2") == 0 &&
          child_inode(child_inode(copy, "tree"), "top") != 0);
    run_real("cp -r tree t2");
    check_output("H37", "folder exists", "cp: t2/tree: already exists\n");
    run_real("cp -r tree box");
    check_output("H37", "folder in the way", "cp: box/tree: already exists\n");
    run_real("cp notes notes");
    check_output("H37", "same file", "cp: notes: source and destination are the same file\n");
    run_real("cp notes .");
    check_output("H37", "same file through the folder", "cp: notes: source and destination are the same file\n");
    run_real("cp nothing x");
    check_output("H37", "missing source", "cp: nothing: not found\n");
    run_real("cp notes nothing/x");
    check_output("H37", "missing destination folder", "cp: nothing/x: no such directory\n");
    run_real("cp notes tail x");
    check_output("H37", "several sources need a folder", "cp: x: no such directory\n");
    run_real("cp notes tail empty");
    check_output("H37", "several sources onto a file", "cp: empty: not a directory\n");
    run_real("cp notes");
    check_output("H37", "one operand", "usage: cp [-r] SOURCE... DEST\n");
    run_real("cp -r .. x");
    check_output("H37", "dot dot", "cp: ..: cannot copy '.' or '..'\n");
    run_real("cp -r / x");
    check_output("H37", "root", "cp: /: cannot copy the root directory\n");
}

static void mv_cases(void) {
    static char back[65536];
    uint32_t box = child_inode(SHELL_ROOT_INODE, "box");
    uint32_t moved;

    check("H38", "rename a file", run_real("mv notes2 renamed") == 0 && mock_output_len == 0 &&
          child_inode(1, "notes2") == 0 && file_bytes(1, "renamed", back, sizeof(back)) == 10 &&
          memcmp(back, "one\ntwo x\n", 10) == 0);
    run_real("mv renamed box");
    check("H38", "move into a folder", child_inode(1, "renamed") == 0 &&
          child_inode(box, "renamed") != 0);
    run_real("mv box/renamed /docs/a/again");
    check("H38", "move and rename across folders", child_inode(box, "renamed") == 0 &&
          file_bytes(child_inode(child_inode(1, "docs"), "a"), "again", back, sizeof(back)) == 10);
    write_file(SHELL_ROOT_INODE, "m1", "m1", 2);
    write_file(SHELL_ROOT_INODE, "m2", "m2", 2);
    run_real("mv m1 m2 box");
    check("H38", "several sources", child_inode(box, "m1") != 0 && child_inode(box, "m2") != 0 &&
          child_inode(1, "m1") == 0 && child_inode(1, "m2") == 0);
    write_file(SHELL_ROOT_INODE, "m1", "new", 3);
    run_real("mv m1 box/m1");
    check("H38", "replaces an existing file", file_bytes(box, "m1", back, sizeof(back)) == 3 &&
          child_inode(1, "m1") == 0);

    check("H39", "rename a folder", run_real("mv tree moved") == 0 && mock_output_len == 0 &&
          child_inode(1, "tree") == 0);
    moved = child_inode(1, "moved");
    check("H39", "content follows", moved != 0 && child_inode(moved, "top") != 0 &&
          child_inode(child_inode(child_inode(moved, "sub"), "deep"), "leaf") != 0);
    run_real("mv moved box");
    check("H39", "folder into a folder", child_inode(1, "moved") == 0 &&
          child_inode(child_inode(box, "moved"), "top") != 0);
    run_real("cd box/moved/sub");
    check("H39", "moved folder can be entered", strcmp(shell_cwd_path(), "/box/moved/sub") == 0);
    run_real("cd ../..");
    check("H39", ".. of the moved folder is its new parent", shell_cwd_inode() == box);
    shell_cwd_set(SHELL_ROOT_INODE, "/");

    run_real("mv box box/moved");
    check_output("H40", "into itself", "mv: box: cannot put a directory inside itself\n");
    check("H40", "source kept", child_inode(1, "box") == box);
    shell_cwd_set(box, "/box");
    run_real("mv /box /other");
    check_output("H40", "folder holding the CWD", "mv: /box: is the current directory or above it\n");
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    run_real("mv empty .");
    check_output("H40", "same place", "mv: empty: source and destination are the same\n");
    run_real("mv box docs/a/again");
    check_output("H40", "folder onto a file", "mv: docs/a/again: already exists\n");
    run_real("mv empty docs");
    check("H40", "file moves into folder docs", child_inode(child_inode(1, "docs"), "empty") != 0 &&
          child_inode(1, "empty") == 0);
    run_real("mv nothing x");
    check_output("H40", "missing source", "mv: nothing: not found\n");
    run_real("mv tail nothing/x");
    check_output("H40", "missing destination", "mv: nothing/x: no such directory\n");
    run_real("mv / x");
    check_output("H40", "root", "mv: /: cannot move the root directory\n");
    run_real("mv tail");
    check_output("H40", "one operand", "usage: mv SOURCE... DEST\n");
}

/* Output checks that do not depend on the order of folder entries. */
static bool has_line(const char *line) {
    char wanted[SHELL_PATH_MAX + 2];
    const char *at = mock_output;
    size_t len;

    snprintf(wanted, sizeof(wanted), "%s\n", line);
    len = strlen(wanted);
    while ((at = strstr(at, wanted)) != NULL) {
        if (at == mock_output || at[-1] == '\n') {
            return true;
        }
        at += len;
    }
    return false;
}

static int line_count(void) {
    int lines = 0;

    for (const char *c = mock_output; *c != '\0'; c++) {
        lines += *c == '\n';
    }
    return lines;
}

static void find_cases(void) {
    uint32_t box = child_inode(SHELL_ROOT_INODE, "box");

    write_file(child_inode(box, "moved"), "needle", "n", 1);
    run_real("mkdir needle");
    check("H41", "file and folder anywhere", run_real("find needle") == 0 && line_count() == 2 &&
          has_line("/box/moved/needle") && has_line("/needle/"));
    run_real("find deep.txt");
    check_output("H41", "deep in the fixture", "/docs/a/b/deep.txt\n");
    run_real("find leaf");
    check("H41", "every copy of a name", line_count() == 4 && has_line("/t2/sub/deep/leaf") &&
          has_line("/t2/tree/sub/deep/leaf") && has_line("/box/tree/sub/deep/leaf") &&
          has_line("/box/moved/sub/deep/leaf"));
    shell_cwd_set(child_inode(child_inode(1, "docs"), "a"), "/docs/a");
    run_real("find tail");
    check("H41", "searches from the root, not the CWD", has_line("/tail") && has_line("/box/tail"));
    shell_cwd_set(SHELL_ROOT_INODE, "/");
    run_real("find leaf | grep box");
    check("H41", "find through a pipe", line_count() == 2 && has_line("/box/tree/sub/deep/leaf") &&
          has_line("/box/moved/sub/deep/leaf"));
    run_real("find nothing");
    check("H41", "no match prints nothing", mock_output_len == 0);

    run_real("find ..");
    check_output("H42", "dot entries are not matched", "");
    run_real("find");
    check_output("H42", "no argument", "usage: find NAME\n");
    run_real("find a b");
    check_output("H42", "two arguments", "usage: find NAME\n");
}

/* Disk full in the middle of cp -r / mv: the partial copy is removed and the source stays. */
static void full_disk_cases(void) {
    static char chunk[60000];
    char name[16];
    int files = 0;
    uint32_t box = child_inode(SHELL_ROOT_INODE, "box");

    run_real("mkdir fill");
    memset(chunk, 'f', sizeof(chunk));
    for (; files < 200; files++) {
        struct EXT2DriverRequest request = {
            .buf = chunk, .name = name, .parent_inode = child_inode(1, "fill"),
            .buffer_size = sizeof(chunk),
        };

        snprintf(name, sizeof(name), "f%d", files);
        request.name_len = (uint8_t)strlen(name);
        if (sys_write_file(&request) != 0) {
            break;
        }
    }
    check("H43", "disk can be filled", files > 0 && files < 200);

    check("H43", "cp -r fails on a full disk", run_real("cp -r box boxcopy") != 0 &&
          strcmp(mock_output, "cp: box: copy failed (disk full?)\n") == 0);
    check("H43", "no partial copy left", child_inode(1, "boxcopy") == 0);
    check("H43", "mv fails and keeps the source", run_real("mv box boxmoved") != 0 &&
          child_inode(1, "boxmoved") == 0 && child_inode(1, "box") == box &&
          child_inode(box, "moved") != 0);

    check("H43", "space comes back with rm -r", run_real("rm -r fill") == 0 &&
          run_real("cp -r box boxcopy") == 0 && child_inode(child_inode(1, "boxcopy"), "moved") != 0);
    run_real("rm -r boxcopy");
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
    rm_cases();
    cp_cases();
    mv_cases();
    find_cases();
    full_disk_cases();
    fs_test_sum(&ctx, "shell");
    // run_shell_tests.sh runs fsck on this image: cp -r, rm -r and mv must not leak blocks or inodes
    if (ram_disk_save("bin/host/shell_after.img") != 0) {
        fprintf(stderr, "cannot save bin/host/shell_after.img\n");
        return 1;
    }
    return ctx.failed == 0 ? 0 : 1;
}
