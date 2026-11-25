"""Bad Apple end to end: `run badapple` draws the video, keeps 20 fps, stops on a key.
python3 badapple_keys.py <iso> <disk> <out dir> <frames>"""
import re
import sys
import time

from qemu_shell import Checks, QemuShell

frames = int(sys.argv[4])
shell = QemuShell(*sys.argv[1:4])
t = Checks()
DONE = re.compile(r"badapple: (\d+) of (\d+) frames in (\d+) ms")


def result():
    found = None
    for row in shell.screen():
        match = DONE.search(row)
        if match:
            found = tuple(int(v) for v in match.groups())
    return found


def white(r, g, b):
    return r > 200 and g > 200 and b > 200


t.check("B01", "shell is up", bool(shell.wait_for("tapOS:", 30)))

shell.type_text("run badapple\n")
t.check("B02", "video is drawn (white pixels on screen)", shell.picture_has(white, 2000, 10))
expected_ms = frames * 50
end = time.time() + expected_ms / 1000 * 4 + 15
while result() is None and time.time() < end:
    time.sleep(0.5)
done = result()
time.sleep(1)
t.check("B03", "text screen comes back with a prompt", done is not None and any(r.startswith("tapOS:") for r in shell.screen()))
if done:
    shown, total, elapsed = done
    t.check("B04", "every frame is played", total == frames and shown >= frames * 0.9)
    t.check("B05", "plays at about 20 fps", 0.9 * expected_ms <= elapsed <= 1.3 * expected_ms)
    print("shown %d of %d in %d ms (expected %d)" % (shown, total, elapsed, expected_ms))

shell.type_text("ls\n")
t.check("B06", "shell still answers afterwards", shell.picture_has(lambda r, g, b: g > 150, 100, 5) and
        any("badapple" in row for row in shell.screen()))

shell.type_text("run badapple\n")
time.sleep(2.5)
shell.command("sendkey q 35")
end = time.time() + 15
while time.time() < end and result() == done:
    time.sleep(0.5)
stopped = result()
t.check("B07", "a key stops the video early", stopped is not None and stopped != done and stopped[0] < frames * 0.6)
no_panic = not any("CPU EXCEPTION" in row for row in shell.screen())
t.check("B08", "no kernel exception", no_panic)

shell.save_screen()
shell.close()
sys.exit(t.result("badapple"))
