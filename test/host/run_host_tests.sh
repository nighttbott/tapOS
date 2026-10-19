# laptop tests for the filesystem. run from the repo root:  bash test/host/run_host_tests.sh
# builds the real src/filesystem/ext2.c with a fake RAM disk, runs every suite,
# then checks every disk image with fsck_tapos.py. Never touches bin/storage.bin.
set -u
cd "$(dirname "$0")/../.."

OUT=bin/host
mkdir -p "$OUT"

# read/write/delete are also C library names; rename ours so printf keeps working
RENAME="-Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete"
FLAGS="-std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Itest/common -Itest/host"

echo "== build"
gcc $FLAGS $RENAME \
    src/filesystem/ext2.c \
    test/common/fs_test.c test/common/fs_cases.c test/common/fs_stubs.c \
    test/host/ram_disk.c test/host/test_main.c \
    -o "$OUT/test_fs" || { echo "BUILD FAILED"; exit 2; }

echo "== fixtures"
python3 test/fixtures/make_fixture.py "$OUT/boundary.img" || exit 2

status=0
run() {   # run <suite> <image|--blank> <saved image>
    echo
    "$OUT/test_fs" "$1" "$2" --save "$3" || status=1
}
run team     test/fixtures/sample-image.bin "$OUT/after_team.img"
run boundary "$OUT/boundary.img"            "$OUT/after_boundary.img"
run existing "$OUT/boundary.img"            "$OUT/after_existing.img"
run blank    --blank                        "$OUT/after_blank.img"

echo
echo "== health check (fsck)"
python3 test/host/fsck_tapos.py "$OUT/boundary.img" "$OUT/after_blank.img" || status=1

echo
[ $status -eq 0 ] && echo "ALL HOST TESTS PASSED" || echo "SOME HOST TESTS FAILED OR ARE NOT IMPLEMENTED YET"
exit $status
