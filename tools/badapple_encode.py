#!/usr/bin/env python3
"""Turns a video into the Bad Apple file the player reads (format: src/header/badapple/codec.h).

  python3 tools/badapple_encode.py bad_apple.mp4 bin/badapple.bin
  python3 tools/badapple_encode.py frames.gray out.bin --raw     (8-bit gray frames, no ffmpeg)
"""
import argparse, itertools, struct, subprocess, sys


def varint(n):
    out = bytearray()
    while True:
        low = n & 0x7F
        n >>= 7
        out.append(low | (0x80 if n else 0))
        if not n:
            return bytes(out)


def encode_frame(delta):
    """delta: bytes of 0/1 per pixel. Runs alternate 0,1,0.. starting with 0."""
    out = bytearray()
    for i, (value, group) in enumerate(itertools.groupby(delta)):
        if i == 0 and value == 1:
            out += varint(0)
        out += varint(sum(1 for _ in group))
    return bytes(out)


def frames_from(source, width, height, fps, raw):
    size = width * height
    if raw:
        stream = open(source, "rb")
        proc = None
    else:
        cmd = ["ffmpeg", "-v", "error", "-i", source, "-vf", f"fps={fps},scale={width}:{height}:flags=area",
               "-pix_fmt", "gray", "-f", "rawvideo", "-"]
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        except FileNotFoundError:
            sys.exit("ffmpeg not found: install it (winget install Gyan.FFmpeg) and open a new terminal")
        stream = proc.stdout
    while True:
        data = stream.read(size)
        if len(data) < size:
            break
        yield data
    if proc and proc.wait() != 0:
        sys.exit("ffmpeg failed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("output")
    ap.add_argument("--size", default="128x96", help="WxH, keep 4:3 (default 128x96)")
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--raw", action="store_true", help="source is raw 8-bit gray frames")
    args = ap.parse_args()
    width, height = (int(v) for v in args.size.split("x"))
    table = bytes(0 if i < args.threshold else 1 for i in range(256))

    records, prev = [], bytes(width * height)
    for gray in frames_from(args.source, width, height, args.fps, args.raw):
        bits = gray.translate(table)
        delta = (int.from_bytes(bits, "big") ^ int.from_bytes(prev, "big")).to_bytes(len(bits), "big")
        rle = encode_frame(delta)
        if len(rle) > 0xFFFF:
            sys.exit("frame does not fit in 64 KiB")
        records.append(rle)
        prev = bits
    if not records:
        sys.exit("no frames read")

    max_len = max(len(r) for r in records)
    if max_len > 2046:
        sys.exit(f"longest frame is {max_len} bytes, the player buffer holds 2046: use a smaller --size or a higher --threshold")
    with open(args.output, "wb") as f:
        f.write(b"BAPL" + struct.pack("<HHHHI", width, height, args.fps, max_len, len(records)))
        for r in records:
            f.write(struct.pack("<H", len(r)) + r)
    total = 16 + sum(2 + len(r) for r in records)
    print(f"{len(records)} frames, {width}x{height}, {args.fps} fps, {total} bytes, longest frame {max_len}")


if __name__ == "__main__":
    main()
