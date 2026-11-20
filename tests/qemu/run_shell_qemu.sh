# Boots the real kernel and shell in QEMU and types commands. Run from the repo root:
#   bash tests/qemu/run_shell_qemu.sh
# Uses its own disk image; never touches bin/storage.bin.
set -u
cd "$(dirname "$0")/../.."

OUT=bin/shell-qemu
rm -rf "$OUT" && mkdir -p "$OUT"

make iso user-shell inserter >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; echo "BUILD FAILED"; exit 2; }

truncate -s 5M "$OUT/disk.bin"
printf 'alpha\nbeta x\ngamma\n\nx last' > "$OUT/notes"
bin/inserter bin/shell 1 "$OUT/disk.bin" >/dev/null || exit 2
bin/inserter "$OUT/notes" 1 "$OUT/disk.bin" >/dev/null || exit 2

python3 tests/qemu/shell_keys.py bin/OS2025.iso "$OUT/disk.bin" "$OUT"
status=$?
echo "screen at the end: $OUT/screen.txt"
exit $status
