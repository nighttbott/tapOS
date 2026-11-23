"""Drives tapOS in QEMU: types through the monitor (sendkey) and reads the
text screen at 0xB8000 (pmemsave). Shared by shell_keys.py and m4_keys.py."""
import os
import select
import subprocess
import time

KEYS = {" ": "spc", "\n": "ret", "\b": "backspace", "|": "shift-backslash", "/": "slash",
        ".": "dot", "-": "minus", "_": "shift-minus"}
CONSOLE_ROWS = 24  # row 24 is the status row (clock)


class QemuShell:
    def __init__(self, iso, disk, out, memory_mb=128, console_rows=CONSOLE_ROWS):
        self.out = out
        self.console_rows = console_rows
        self.screen_path = os.path.join(out, "screen.bin")
        self.log_path = os.path.join(out, "qemu.log")
        with open(self.log_path, "w") as log:
            self.qemu = subprocess.Popen(
                ["qemu-system-i386", "-m", str(memory_mb), "-display", "none", "-no-reboot",
                 "-monitor", "stdio", "-serial", "none",
                 "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % disk,
                 "-cdrom", iso, "-boot", "order=d"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
        try:
            self._read_prompt()
        except BaseException:
            self.close()
            raise

    def _read_prompt(self):
        data = b""
        deadline = time.monotonic() + 10
        while not data.endswith(b"(qemu) "):
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.qemu.stdout], [], [], remaining)[0]:
                raise RuntimeError(f"Timed out waiting for QEMU monitor. See {self.log_path}")
            chunk = os.read(self.qemu.stdout.fileno(), 4096)
            if not chunk:
                raise RuntimeError(f"QEMU monitor closed unexpectedly. See {self.log_path}")
            data += chunk

    def command(self, text):
        self.qemu.stdin.write((text + "\n").encode())
        self.qemu.stdin.flush()
        self._read_prompt()

    def screen(self):
        """All 25 rows, trailing spaces removed."""
        self.command("pmemsave 0xb8000 4000 %s" % self.screen_path)
        raw = open(self.screen_path, "rb").read()
        chars = "".join(chr(b) if 32 <= b < 127 else " " for b in raw[0::2])
        return [chars[i:i + 80].rstrip() for i in range(0, 2000, 80)]

    def status_row(self):
        return self.screen()[CONSOLE_ROWS]

    def wait_for(self, text, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if any(text in row for row in self.screen()[:self.console_rows]):
                return True
            time.sleep(0.2)
        return False

    def type_text(self, text):
        for ch in text:
            key = KEYS.get(ch)
            if key is None:
                key = ch if not ch.isupper() else "shift-" + ch.lower()
            self.command("sendkey " + key + " 35")
            time.sleep(0.05)

    def run(self, line, settle=0.5):
        """Types line and returns the console rows printed after it, up to the last row (the next prompt).
        The typed row is found by its ending, because output of another process may take the prompt's place."""
        self.type_text(line + "\n")
        time.sleep(settle)
        rows = [r for r in self.screen()[:self.console_rows] if r]
        typed = [i for i, r in enumerate(rows) if r.endswith(line)]
        if not typed:
            return None
        return rows[typed[-1] + 1:-1]

    def save_screen(self, name="screen.txt"):
        with open(os.path.join(self.out, name), "w") as f:
            f.write("\n".join(self.screen()) + "\n")

    def close(self):
        if self.qemu.poll() is None:
            try:
                self.qemu.communicate(b"quit\n", timeout=3)
            except subprocess.TimeoutExpired:
                self.qemu.kill()
                self.qemu.communicate()
        self.qemu.stdin.close()
        self.qemu.stdout.close()


class Checks:
    def __init__(self):
        self.failed = 0

    def check(self, case_id, what, ok):
        self.failed += not ok
        print("%s %s %s" % ("PASS" if ok else "FAIL", case_id, what), flush=True)

    def result(self, suite):
        print("RESULT %s: %s" % (suite, "0 FAIL" if self.failed == 0 else "%d FAIL" % self.failed))
        return 1 if self.failed else 0
