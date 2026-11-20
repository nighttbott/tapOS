#!/usr/bin/env bash
# Filesystem and disk tests inside the real OS (QEMU, real ATA driver).
# Run from the repo root:  bash tests/qemu/run_qemu_tests.sh
# Every test boots its own test kernel on a COPY of a disk image.
# bin/storage.bin and the normal Makefile build are never touched.
set -u
cd "$(dirname "$0")/../.."

kernel_asm_sources=$(make --no-print-directory -s kernel-asm-sources) || exit 2
kernel_c_sources=$(make --no-print-directory -s kernel-c-sources) || exit 2

OUT=bin/qemu-test
TIMEOUT=30
CFLAGS="-fshort-wchar -g -Wall -Wextra -Werror -nostdlib -fno-stack-protector -nostartfiles \
        -nodefaultlibs -ffreestanding -m32 -c -Isrc -Itests/common -Itests/qemu -DFS_TEST_SKIP_FULL_DISK"
rm -rf "$OUT" && mkdir -p "$OUT"

# Build a test kernel + ISO that runs one suite.  build <name> <suite function>
build() {
    local name=$1 suite=$2 dir=$OUT/$1
    mkdir -p "$dir/obj" "$dir/iso/boot/grub"
    for s in $kernel_asm_sources; do
        nasm -f elf32 -g -F dwarf "$s" -o "$dir/obj/$(echo "$s" | tr / _).o" || return 1
    done
    # every C file of the OS except src/kernel.c (test_kernel.c has its own kernel_setup)
    for c in $kernel_c_sources tests/common/*.c tests/qemu/*.c; do
        [ "$c" = src/kernel.c ] && continue
        gcc $CFLAGS -DTAPOS_TEST_SUITE="$suite" -DTAPOS_TEST_NAME="\"$name\"" \
            "$c" -o "$dir/obj/$(echo "$c" | tr / _).o" || return 1
    done
    ld -T src/linker.ld -melf_i386 -z noexecstack "$dir"/obj/*.o -o "$dir/kernel" || return 1
    cp "$dir/kernel" "$dir/iso/boot/kernel"
    cp other/grub1 src/menu.lst "$dir/iso/boot/grub/"
    genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os \
        -input-charset utf8 -quiet -boot-info-table -o "$dir/test.iso" "$dir/iso" || return 1
}

# Boot a test kernel with a disk image and wait for its result.  boot <name> <disk image> [seconds]
status=0
boot() {
    local name=$1 disk=$2 limit=${3:-$TIMEOUT} log=$OUT/$1/serial.log
    rm -f "$log"
    qemu-system-i386 -display none -no-reboot -no-shutdown -serial "file:$log" \
        -drive "file=$disk,format=raw,if=ide,index=0,media=disk" \
        -cdrom "$OUT/$name/test.iso" -boot order=d >"$OUT/$name/qemu.log" 2>&1 &
    local pid=$!
    for _ in $(seq $((limit * 4))); do
        grep -q TAPOS_TEST_DONE "$log" 2>/dev/null && break
        sleep 0.25
    done
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    echo
    if grep -q TAPOS_TEST_DONE "$log" 2>/dev/null; then
        grep -v TAPOS_TEST_DONE "$log"
        grep -q " 0 FAIL$" "$log" || status=1
    else
        echo "FAIL $name: no result after ${limit}s (hang, crash or too slow). Output so far:"
        cat "$log" 2>/dev/null
        status=1
    fi
}

blank_disk() { truncate -s 0 "$1" && truncate -s 4M "$1"; }

echo "== build test kernels"
python3 tests/fixtures/make_fixture.py "$OUT/boundary.img" >/dev/null || exit 2
for pair in disk1:fs_suite_disk_write disk2:fs_suite_disk_read team:fs_suite_team_fixture \
            boundary:fs_suite_boundary delete:fs_suite_delete blank:fs_suite_blank \
            persist1:fs_suite_persist_write persist2:fs_suite_persist_read; do
    build "${pair%%:*}" "${pair##*:}" || { echo "BUILD FAILED for ${pair%%:*}"; exit 2; }
done

echo "== raw disk driver, two boots on the same disk (A)"
blank_disk "$OUT/disk.bin"
boot disk1 "$OUT/disk.bin"
boot disk2 "$OUT/disk.bin"

echo "== read tests (C)"
cp tests/fixtures/sample-image.bin "$OUT/team.bin"
boot team "$OUT/team.bin"
cp "$OUT/boundary.img" "$OUT/boundary.bin"
boot boundary "$OUT/boundary.bin"

echo "== delete on the boundary fixture"
cp "$OUT/boundary.img" "$OUT/delete.bin"
boot delete "$OUT/delete.bin"

echo "== write / delete on a blank disk (B, E)"
blank_disk "$OUT/blank.bin"
boot blank "$OUT/blank.bin" 120

echo "== persistence: write, power off, boot again, read"
blank_disk "$OUT/persist.bin"
boot persist1 "$OUT/persist.bin"
boot persist2 "$OUT/persist.bin"

echo
[ $status -eq 0 ] && echo "ALL QEMU TESTS PASSED" || echo "SOME QEMU TESTS FAILED"
exit $status
