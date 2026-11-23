"""Drives tapOS in QEMU: types through the monitor (sendkey) and reads the text screen (pmemsave).
The kernel draws its 80x25 text cells in graphics mode, so the cells are read from the kernel's
framebuffer_text_shadow (found with nm); the BIOS text memory 0xB8000 is the fallback.
Shared by shell_keys.py and m4_keys.py."""
import os
import select
import subprocess
import time

KEYS = {" ": "spc", "\n": "ret", "\b": "backspace", "|": "shift-backslash", "/": "slash",
        ".": "dot", "-": "minus", "_": "shift-minus"}
CONSOLE_ROWS = 24  # row 24 is the status row (clock)


def screen_address(iso):
    """Physical address of the text cells: the kernel's shadow buffer, else the BIOS text memory."""
    kernel = os.path.join(os.path.dirname(iso), "kernel")
    try:
        symbols = subprocess.run(["nm", kernel], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return 0xB8000
    for line in symbols.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "framebuffer_text_shadow":
            return int(parts[0], 16) - 0xC0000000
    return 0xB8000


class QemuShell:
    def __init__(self, iso, disk, out, memory_mb=128, console_rows=CONSOLE_ROWS):
        self.out = out
        self.console_rows = console_rows
        self.screen_path = os.path.join(out, "screen.bin")
        self.screen_base = screen_address(iso)
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
        self.command("pmemsave 0x%x 4000 %s" % (self.screen_base, self.screen_path))
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

    def type_text(self, text, pause=0.05):
        """The keyboard holds one character, so with many busy processes (each shell turn comes
        around every 10 ms times their number) the pause between keys has to grow with them."""
        for ch in text:
            key = KEYS.get(ch)
            if key is None:
                key = ch if not ch.isupper() else "shift-" + ch.lower()
            self.command("sendkey " + key + " 35")
            time.sleep(pause)

    def run(self, line, settle=0.5, timeout=5.0, pause=0.05):
        """Types line and returns the console rows printed after it, up to the last row (the next prompt).
        The typed row is found by its ending, because output of another process may take the prompt's place.
        The screen is read again until a prompt follows the typed row, so a read taken mid-scroll
        or before a slow command finishes is not mistaken for the answer."""
        self.type_text(line + "\n", pause)
        time.sleep(settle)
        end = time.time() + timeout
        while True:
            rows = [r for r in self.screen()[:self.console_rows] if r]
            typed = [i for i, r in enumerate(rows) if r.endswith(line)]
            if not typed:
                if time.time() >= end:
                    return None
            elif typed[-1] < len(rows) - 1 and rows[-1].startswith("tapOS:"):
                return rows[typed[-1] + 1:-1]
            elif time.time() >= end:
                return rows[typed[-1] + 1:-1]
            time.sleep(0.2)

    def pixels(self):
        """The displayed picture as (width, height, rows of (r, g, b)) read from a QEMU screendump."""
        path = os.path.join(self.out, "picture.ppm")
        for _ in range(10):           # QEMU repaints the dump surface lazily
            self.command("screendump %s" % path)
            raw = open(path, "rb").read()
            if raw.count(b"\xff\xff\xff") > 8:
                break
            time.sleep(0.3)
        fields = raw.split(None, 4)           # P6 width height maxval, then the pixel bytes
        width, height = int(fields[1]), int(fields[2])
        body = raw[len(raw) - width * height * 3:]
        rows = [[tuple(body[(y * width + x) * 3:(y * width + x) * 3 + 3]) for x in range(width)]
                for y in range(height)]
        return width, height, rows

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
