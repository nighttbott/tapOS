# Boots the real kernel and shell in QEMU and types commands. Run from the repo root:
#   bash tests/qemu/run_shell_qemu.sh
# Uses its own disk image; never touches bin/storage.bin.
set -u
cd "$(dirname "$0")/../.."

MB=${1:-128}
OUT=bin/shell-qemu-$MB
rm -rf "$OUT" && mkdir -p "$OUT"

make iso user-shell inserter OUTPUT_FOLDER="$OUT/build" SYSTEM_MEMORY_MB="$MB" >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; echo "BUILD FAILED"; exit 2; }

truncate -s 5M "$OUT/disk.bin"
mkdir -p "$OUT/imported/child"
printf 'nested seed\n' > "$OUT/imported/child/seed"
printf 'alpha\nbeta x\ngamma\n\nx last' > "$OUT/notes"
"$OUT/build/inserter" "$OUT/build/shell" 1 "$OUT/disk.bin" >/dev/null || exit 2
"$OUT/build/inserter" "$OUT/notes" 1 "$OUT/disk.bin" >/dev/null || exit 2
"$OUT/build/inserter" "$OUT/imported/" 1 "$OUT/disk.bin" >/dev/null || exit 2

python3 -B tests/qemu/shell_keys.py "$OUT/build/OS2025.iso" "$OUT/disk.bin" "$OUT" "$MB"
status=$?
python3 tests/host/fsck.py "$OUT/disk.bin" || status=1
echo "screen at the end: $OUT/screen.txt"
exit $status
