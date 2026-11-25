set -u
cd "$(dirname "$0")/../.."

OUT=bin/badapple-qemu
FRAMES=200
rm -rf "$OUT" && mkdir -p "$OUT"

python3 - "$OUT/clip.gray" "$FRAMES" <<'PY' || exit 2
import sys
W, H, N = 128, 96, int(sys.argv[2])
with open(sys.argv[1], "wb") as f:
    for n in range(N):
        rows = bytearray()
        for y in range(H):
            for x in range(W):
                inside = 20 + n % 60 <= x < 60 + n % 60 and 20 <= y < 70
                rows.append(255 if inside or x == n % W else 0)
        f.write(rows)
PY
python3 tools/badapple_encode.py "$OUT/clip.gray" "$OUT/apple.dat" --raw || exit 2
make iso user-apps inserter >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; echo "BUILD FAILED"; exit 2; }

truncate -s 5M "$OUT/disk.bin"
for file in bin/shell bin/badapple "$OUT/apple.dat"; do
    bin/inserter "$file" 1 "$OUT/disk.bin" >/dev/null || exit 2
done

python3 tests/qemu/badapple_keys.py bin/OS2025.iso "$OUT/disk.bin" "$OUT" "$FRAMES"
status=$?
echo "screen at the end: $OUT/screen.txt"
exit $status
