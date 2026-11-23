"""Milestone 4 end to end: processes, exec/ps/kill, clock, Hello World (spec WP-D3, P01-P12).
python3 m4_keys.py <iso> <disk> <out dir>"""
import re
import sys
import time

from qemu_shell import Checks, QemuShell

CLOCK = re.compile(r"\d\d:\d\d:\d\d$")
STARTED = re.compile(r"exec: started (\S+) \(PID (\d+)\)")

shell = QemuShell(*sys.argv[1:4])
t = Checks()


def ps_rows():
    """Rows of ps split into words, or None when ps did not print its table."""
    rows = shell.run("ps") or []
    if "PID  STATE    NAME" not in rows:
        return None
    return [r.split() for r in rows if r.split() and r.split()[0].isdigit()]


def ps_has(name):
    """True or False from a real ps table; None when ps failed, so 'not listed' never passes by accident."""
    rows = ps_rows()
    return None if rows is None else any(len(row) == 3 and row[2] == name for row in rows)


def exec_pid(name):
    """Runs exec name; returns the PID from the message, or None."""
    for row in shell.run("exec " + name) or []:
        match = STARTED.search(row)
        if match and match.group(1) == name:
            return int(match.group(2))
    return None


def clock_text():
    return shell.status_row()[72:80]


def clock_ticks(seconds):
    """True when the status row shows HH:MM:SS and it changes within the given time."""
    first = clock_text()
    end = time.time() + seconds
    while time.time() < end:
        now = clock_text()
        if CLOCK.match(now) and CLOCK.match(first) and now != first:
            return True
        if not CLOCK.match(first):
            first = now
        time.sleep(0.2)
    return False


def no_panic():
    return not any("CPU EXCEPTION" in row for row in shell.screen())


try:
    t.check("P01", "boot shows the prompt", shell.wait_for("tapOS:/$", 20))
    t.check("P02", "ps lists the shell as PID 1", ["1", "running", "shell"] in (ps_rows() or []))

    hello = exec_pid("hello")
    t.check("P03", "exec hello starts it", hello is not None)
    t.check("P03", "hello prints its greeting", shell.wait_for("Hello, World!", 3))
    time.sleep(0.5)
    t.check("P03", "hello is gone after it exits", ps_has("hello") is False)

    clock = exec_pid("clock")
    t.check("P04", "exec clock starts it", clock is not None)
    t.check("P04", "clock shows HH:MM:SS and ticks", clock_ticks(2.5))

    t.check("P05", "shell works while the clock runs", shell.run("grep beta notes") == ["beta x"])
    t.check("P05", "clock keeps ticking", clock_ticks(2.5))

    shell.run("kill %d" % (clock or 0))
    t.check("P06", "killed clock leaves ps", ps_has("clock") is False)
    frozen = clock_text()
    time.sleep(2)
    t.check("P06", "status row stops changing", CLOCK.match(frozen) is not None and clock_text() == frozen)

    clock = exec_pid("clock")
    t.check("P07", "clock starts again", clock is not None and clock_ticks(2.5))

    t.check("P08", "kill 1 refused", shell.run("kill 1") == ["kill: cannot kill init (PID 1)"])
    t.check("P08", "kill unknown PID", shell.run("kill 99") == ["kill: 99: no such process"])
    t.check("P08", "kill bad number", shell.run("kill abc") == ["usage: kill PID"])

    t.check("P09", "exec missing file", shell.run("exec nothing") == ["exec: nothing: not found"])
    t.check("P09", "exec of a plain file", shell.run("exec notes") == ["exec: notes: not an executable"])

    started = sum(exec_pid("hello") is not None for _ in range(20))
    t.check("P10", "20 x exec hello all start (%d)" % started, started == 20)

    shell.run("kill %d" % (clock or 0))
    clocks = []
    refused = False
    SLOW = 0.25  # up to 16 busy processes share the CPU, so the shell reads a key only every ~160 ms
    for _ in range(16):
        rows = shell.run("exec clock", pause=SLOW) or []
        pid = next((int(m.group(2)) for m in map(STARTED.search, rows) if m), None)
        if pid is None:
            refused = rows == ["exec: too many processes"]
            break
        clocks.append(pid)
    t.check("P11", "shell + 15 clocks fit, the 16th is refused (%d started)" % len(clocks),
            len(clocks) == 15 and refused)
    for pid in clocks:
        shell.run("kill %d" % pid, pause=SLOW)
    t.check("P11", "all clocks killed", ps_has("clock") is False)

    exec_pid("clock")
    ticking = all(clock_ticks(2.5) for _ in range(20))
    t.check("P12", "clock ticks for 60 s without a panic", ticking and no_panic())
finally:
    shell.save_screen()
    shell.close()

sys.exit(t.result("m4-qemu"))
