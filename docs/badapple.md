# Bad Apple

Video 128x96, 20 fps, one bit per pixel, kept as run lengths of the change from the previous frame (format in `src/header/badapple/codec.h`). The player `badapple` reads the data file `/apple.dat` piece by piece, draws each frame, and keeps time with the PIT tick count. A frame that is more than one frame late is decoded but not drawn, so the video stays in sync. Any key stops it.

## Make the data

```
python3 tools/badapple_encode.py bad_apple.mp4 bin/apple.dat      (needs ffmpeg)
make apple-insert VIDEO=bad_apple.mp4                              (puts it in the root of the disk)
```

The video itself is not in the repo. Then, in the OS, `run badapple`.

## What the kernel got

| Syscall | Number | Use |
| --- | --- | --- |
| `GET_TICKS` | 24 | PIT ticks since boot (100 per second) |
| `READ_RANGE` | 25 | read from a byte offset (`ext2_read_range`), so a 1 MB file is read in 2 KB pieces |
| `BLIT_BITS` | 26 | draw a 1 bit per pixel image, scaled by the largest whole factor and centred (128x96 becomes 512x384) |
| `REDRAW` | 27 | paint the text screen again |

The shell got `run PATH`: like `exec`, but it waits until the program ends (so the keys go to the program) and then repaints the text screen.

## Tests

```
make test-badapple                      codec and player with fake system calls
bash tests/host/run_host_tests.sh       includes range reads Q01-Q05
bash tests/qemu/run_badapple_qemu.sh    boots QEMU, plays a made-up 10 second clip
```

The QEMU test checks that the picture is drawn, that all frames play in about 10 s (20 fps), that the prompt returns and that a key stops the video. The real video file and real FPS with it are not measured.
