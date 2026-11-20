set -u
cd "$(dirname "$0")/../.."

OUT=bin/host
mkdir -p "$OUT"

RENAME="-Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete"
FLAGS="-std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer -Isrc -Itests/common -Itests/host"

echo "== build"
gcc $FLAGS $RENAME \
    src/filesystem/ext2.c \
    tests/common/fs_test.c tests/common/fs_cases.c \
    tests/host/ram_disk.c tests/host/test_main.c \
    -o "$OUT/test_fs" || { echo "BUILD FAILED"; exit 2; }

echo "== fixtures"
python3 tests/fixtures/make_fixture.py "$OUT/boundary.img" || exit 2

status=0
run() {   # run <suite> <image|--blank> <saved image>
    echo
    "$OUT/test_fs" "$1" "$2" --save "$3" || status=1
}
run team     tests/fixtures/sample-image.bin "$OUT/after_team.img"
run boundary "$OUT/boundary.img"            "$OUT/after_boundary.img"
run existing "$OUT/boundary.img"            "$OUT/after_existing.img"
run delete   "$OUT/boundary.img"            "$OUT/after_delete.img"
run blank    --blank                        "$OUT/after_blank.img"

echo
echo "== health check (fsck)"

cmp tests/fixtures/sample-image.bin "$OUT/after_team.img" || status=1

python3 tests/host/fsck.py \
    "$OUT/boundary.img" \
    "$OUT/after_boundary.img" \
    "$OUT/after_existing.img" \
    "$OUT/after_delete.img" \
    "$OUT/after_blank.img" || status=1

echo
[ $status -eq 0 ] && echo "ALL HOST TESTS PASSED" || echo "SOME HOST TESTS FAILED"
exit $status
