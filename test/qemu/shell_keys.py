"""Boots tapOS with the real shell, types commands, and checks the screen.
python3 shell_keys.py <iso> <disk> <out dir>"""
import sys
import time

from qemu_shell import Checks, QemuShell

shell = QemuShell(*sys.argv[1:4])
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
finally:
    shell.save_screen()
    shell.close()

sys.exit(t.result("shell-qemu"))
