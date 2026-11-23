set -u
cd "$(dirname "$0")/../.."

mkdir -p bin/host
gcc -std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize=alignment -Isrc -Itests/common -Itests/host \
    -Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete -DINDEX_POOL_SIZE=64u \
    src/filesystem/ext2.c src/filesystem/index.c tests/common/fs_test.c tests/host/ram_disk.c \
    tests/host/index_test.c -o bin/host/test_index || { echo "BUILD FAILED"; exit 2; }
bin/host/test_index
