"""Boots tapOS with the real shell, types commands, and checks the screen.
python3 shell_keys.py <iso> <disk> <out dir> <RAM in MiB>"""
import re
import sys
import time

from qemu_shell import Checks, QemuShell

PROMPT = re.compile(r"^tapOS:\S*\$$")

def fs_run(shell, line, timeout=30.0):
    """run() for filesystem commands, which may read the disk for a while. The keyboard keeps
    only one key, so nothing is typed until the command is done and an empty prompt is back.
    Reads all 25 rows: until the status row exists the console scrolls through row 24 too."""
    shell.type_text(line + "\n")
    end = time.time() + timeout
    while time.time() < end:
        rows = [r for r in shell.screen() if r]
        typed = [i for i, r in enumerate(rows) if r.endswith(line)]
        prompts = [i for i, r in enumerate(rows) if PROMPT.match(r)]
        if typed and prompts and prompts[-1] > typed[-1]:
            return rows[typed[-1] + 1:prompts[-1]]
        time.sleep(0.2)
    return None

shell = QemuShell(*sys.argv[1:4], memory_mb=int(sys.argv[4]), console_rows=25)
t = Checks()
try:
    t.check("Q01", "boot shows the prompt", shell.wait_for("tapOS:/$", 20))

    shell.type_text("abx\bc\n")
    time.sleep(0.5)
    rows = [r for r in shell.screen() if r]
    t.check("Q02", "backspace edits screen and line",
            rows[-3].endswith("$ abc") and rows[-2] == "abc: command not found")

    t.check("Q03", "unknown command", shell.run("xyz") == ["xyz: command not found"])
    t.check("Q04", "grep reads a real file", shell.run("grep beta notes") == ["beta x"])
    t.check("Q05", "pipeline", shell.run("grep a notes | grep l") == ["alpha", "x last"])
    t.check("Q06", "grep error", shell.run("grep x nothing") == ["grep: nothing: not found"])
    t.check("Q07", "parser error", shell.run("grep a |") == ["shell: empty command in pipeline"])
    t.check("Q08", "prompt is back", shell.wait_for("tapOS:/$", 2))

    t.check("Q09", "mkdir then ls", fs_run(shell, "mkdir docs") == [] and
            fs_run(shell, "ls") == ["shell", "notes", "imported/", "docs/"])
    fs_run(shell, "cd docs")
    t.check("Q10", "cd changes the prompt", shell.wait_for("tapOS:/docs$", 2))
    t.check("Q11", "cp then cat | grep", fs_run(shell, "cp /notes n2") == [] and
            fs_run(shell, "cat n2 | grep beta") == ["beta x"])
    t.check("Q12", "mv renames", fs_run(shell, "mv n2 moved") == [] and
            fs_run(shell, "ls") == ["moved"])
    fs_run(shell, "cd ..")
    t.check("Q13", "find searches the whole disk", fs_run(shell, "find moved") == ["/docs/moved"])
    t.check("Q14", "rm -r removes the folder", fs_run(shell, "rm -r docs") == [] and
            fs_run(shell, "ls") == ["shell", "notes", "imported/"] and fs_run(shell, "find moved") == [])
    t.check("Q15", "command errors",
            fs_run(shell, "cd nothing") == ["cd: nothing: no such directory"] and
            fs_run(shell, "rm notes/x") == ["rm: notes/x: not found"])
    fs_run(shell, "mkdir keep")
    fs_run(shell, "cp notes keep/n")
finally:
    shell.save_screen()
    shell.close()

shell = QemuShell(*sys.argv[1:4], memory_mb=int(sys.argv[4]), console_rows=25)
try:
    t.check("Q16", "boot again", shell.wait_for("tapOS:/$", 20))
    t.check("Q17", "files survive a reboot", fs_run(shell, "ls keep") == ["n"] and
            fs_run(shell, "grep gamma keep/n") == ["gamma"])
    t.check("Q18", "inserter copies a directory recursively",
            fs_run(shell, "grep nested imported/child/seed") == ["nested seed"])
finally:
    shell.save_screen("screen-reboot.txt")
    shell.close()

sys.exit(t.result("shell-qemu"))
