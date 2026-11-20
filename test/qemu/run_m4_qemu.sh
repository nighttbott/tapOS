# Milestone 4 end to end (P01-P12): boots the real kernel with shell, hello and clock,
# types commands and checks the screen. Run from the repo root:  bash test/qemu/run_m4_qemu.sh
# Uses its own disk image; never touches bin/storage.bin.
set -u
cd "$(dirname "$0")/../.."

OUT=bin/m4-qemu
rm -rf "$OUT" && mkdir -p "$OUT"

make iso user-apps inserter >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; echo "BUILD FAILED"; exit 2; }

truncate -s 5M "$OUT/disk.bin"
printf 'alpha\nbeta x\ngamma\n\nx last' > "$OUT/notes"
for file in bin/shell bin/hello bin/clock "$OUT/notes"; do
    bin/inserter "$file" 1 "$OUT/disk.bin" >/dev/null || exit 2
done

python3 test/qemu/m4_keys.py bin/OS2025.iso "$OUT/disk.bin" "$OUT"
status=$?
echo "screen at the end: $OUT/screen.txt"
exit $status
