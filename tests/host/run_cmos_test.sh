set -u
cd "$(dirname "$0")/../.."

mkdir -p bin/host
gcc -std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -Isrc \
    src/driver/cmos.c tests/host/cmos_test.c -o bin/host/test_cmos || exit 2
bin/host/test_cmos
