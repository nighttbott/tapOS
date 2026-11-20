# laptop tests for the shell. run from the repo root:  bash tests/host/run_shell_tests.sh
# builds the real src/user code with tests/host/shell_mock_syscall.c instead of int 0x30,
# on the real src/filesystem/ext2.c with a fake RAM disk. Never touches bin/storage.bin.
set -u
cd "$(dirname "$0")/../.."

OUT=bin/host
mkdir -p "$OUT"

echo "== freestanding build"
make --no-print-directory user-shell OUTPUT_FOLDER="$OUT/freestanding" || exit 2

# read/write/delete are also C library names; rename ours so printf keeps working
RENAME="-Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete"
# alignment check is off for the same reason as run_host_tests.sh
FLAGS="-std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer -Isrc -Itests/common -Itests/host"

echo "== host build"
# Rename the real main so the host test can provide its own.
shell_sources=$(make --no-print-directory -s shell-module-sources) || exit 2
gcc $FLAGS $RENAME -Dmain=shell_main -c src/user-shell.c -o "$OUT/shell.o" || { echo "BUILD FAILED"; exit 2; }
gcc $FLAGS $RENAME \
    "$OUT/shell.o" $shell_sources src/filesystem/ext2.c \
    tests/common/fs_test.c tests/host/ram_disk.c \
    tests/host/shell_mock_syscall.c tests/host/shell_test.c \
    -o "$OUT/test_shell" || { echo "BUILD FAILED"; exit 2; }

echo "== fixtures"
python3 tests/fixtures/make_fixture.py "$OUT/shell.img" || exit 2

echo
"$OUT/test_shell" "$OUT/shell.img"
status=$?

echo
echo "== fsck after every shell command"
python3 tests/host/fsck.py "$OUT/shell_after.img" || status=1

echo
[ $status -eq 0 ] && echo "ALL SHELL TESTS PASSED" || echo "SOME SHELL TESTS FAILED"
exit $status
