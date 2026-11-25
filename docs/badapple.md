# Bad Apple

Video 128x96, 20 fps, one bit per pixel, kept as run lengths of the change from the previous frame (format in `src/header/badapple/codec.h`). The player reads it piece by piece with `READ_RANGE`, draws each frame with `BLIT`, and keeps time with `GET_MS`. A frame that is more than one frame late is decoded but not drawn, so the video stays in sync. Any key stops it.

## Make the data

```
python3 tools/badapple_encode.py bad_apple.mp4 bin/apple.dat      (needs ffmpeg)
```

The video itself is not in the repo. Insert `apple.dat` in the root of the disk with the inserter (`make apple-insert VIDEO=bad_apple.mp4`).

## Test

```
make test-badapple
```

Checks the header, that every drawn frame equals its source frame, the 20 fps pace, dropping when drawing is slow, stopping on a key, and cut off or corrupt data. It uses a made-up clip and fake system calls, so it needs no QEMU.

## Not done yet

The app `src/external/badapple.c` only compiles against the syscalls and the large program format from the DOOM branch (`GET_MS`, `BLIT`, `READ_RANGE`, `YIELD`, `REDRAW`, TAPF). It is not in `USER_PROGRAMS` and has not run in QEMU. The frame buffer is 48 KiB, more than the 28 KiB ELF limit. Real FPS and the size of the real data file are not measured.
