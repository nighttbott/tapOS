set -u
cd "$(dirname "$0")/../.."

mkdir -p bin/host
python3 - <<'PY' || exit 2
import sys
W, H, N = 128, 96, 60
with open("bin/host/ba_test.gray", "wb") as f:
    for n in range(N):
        rows = bytearray()
        for y in range(H):
            for x in range(W):
                square = 10 + n <= x < 40 + n and 20 <= y < 60
                bar = x == 3 * n % W
                rows.append(255 if (square or bar or (n > 40 and y == 90)) else 0)
        f.write(rows)
PY
python3 tools/badapple_encode.py bin/host/ba_test.gray bin/host/ba_test.bin --raw || exit 2
gcc -std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -Isrc \
    src/badapple/player.c tests/host/badapple_test.c -o bin/host/test_badapple || { echo "BUILD FAILED"; exit 2; }
bin/host/test_badapple bin/host/ba_test.bin bin/host/ba_test.gray
