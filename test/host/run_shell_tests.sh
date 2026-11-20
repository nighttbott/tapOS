# laptop tests for the shell. run from the repo root:  bash test/host/run_shell_tests.sh
# builds the real src/user code with test/host/shell_mock_syscall.c instead of int 0x30,
# on the real src/filesystem/ext2.c with a fake RAM disk. Never touches bin/storage.bin.
set -u
cd "$(dirname "$0")/../.."

OUT=bin/host
mkdir -p "$OUT"

echo "== freestanding build"
for file in syscall shell grep proc fsutil nav crt0; do
    gcc -m32 -ffreestanding -nostdlib -fno-stack-protector -Wall -Wextra -Werror -Isrc \
        -c "src/user/$file.c" -o "$OUT/user_$file.o" || { echo "BUILD FAILED"; exit 2; }
done

# read/write/delete are also C library names; rename ours so printf keeps working
RENAME="-Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete"
# alignment check is off for the same reason as run_host_tests.sh
FLAGS="-std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer -Isrc -Itest/common -Itest/host"

echo "== host build"
# shell.c has the real main(); the test needs its own
gcc $FLAGS $RENAME -Dmain=shell_main -c src/user/shell.c -o "$OUT/shell.o" || { echo "BUILD FAILED"; exit 2; }
gcc $FLAGS $RENAME \
    "$OUT/shell.o" src/user/grep.c src/user/proc.c src/user/fsutil.c src/user/nav.c src/filesystem/ext2.c \
    test/common/fs_test.c test/host/ram_disk.c \
    test/host/shell_mock_syscall.c test/host/shell_test.c \
    -o "$OUT/test_shell" || { echo "BUILD FAILED"; exit 2; }

echo "== fixtures"
python3 test/fixtures/make_fixture.py "$OUT/shell.img" || exit 2

echo
"$OUT/test_shell" "$OUT/shell.img"
status=$?

echo
[ $status -eq 0 ] && echo "ALL SHELL TESTS PASSED" || echo "SOME SHELL TESTS FAILED"
exit $status
