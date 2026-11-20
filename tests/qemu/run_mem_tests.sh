#!/usr/bin/env bash
# Memory tests on low RAM. Run from the repo root:  bash tests/qemu/run_mem_tests.sh [MiB] [suite]
set -u
cd "$(dirname "$0")/../.."

kernel_asm_sources=$(make --no-print-directory -s kernel-asm-sources) || exit 2
kernel_c_sources=$(make --no-print-directory -s kernel-c-sources) || exit 2

MB=${1:-6}
SUITE=${2:-mem_suite_frames}
OUT=bin/mem-test-$MB
CFLAGS="-fshort-wchar -g -Wall -Wextra -Werror -nostdlib -fno-stack-protector -nostartfiles \
        -nodefaultlibs -ffreestanding -m32 -c -Isrc -Itests/common -Itests/qemu -DFS_TEST_SKIP_FULL_DISK \
        -DSYSTEM_MEMORY_MB=$MB -DTAPOS_TEST_SUITE=$SUITE -DTAPOS_TEST_NAME=\"mem$MB\""
rm -rf "$OUT" && mkdir -p "$OUT/obj" "$OUT/iso/boot/grub"

for s in $kernel_asm_sources; do
    nasm -f elf32 -g -F dwarf "$s" -o "$OUT/obj/$(echo "$s" | tr / _).o" || exit 2
done
for c in $kernel_c_sources tests/common/*.c tests/qemu/*.c; do
    [ "$c" = src/kernel.c ] && continue
    gcc $CFLAGS "$c" -o "$OUT/obj/$(echo "$c" | tr / _).o" || exit 2
done
ld -T src/linker.ld -melf_i386 -z noexecstack "$OUT"/obj/*.o -o "$OUT/kernel" || exit 2
cp "$OUT/kernel" "$OUT/iso/boot/kernel"
cp other/grub1 src/menu.lst "$OUT/iso/boot/grub/"
genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os \
    -input-charset utf8 -quiet -boot-info-table -o "$OUT/test.iso" "$OUT/iso" || exit 2

truncate -s 5M "$OUT/disk.bin"
log=$OUT/serial.log
qemu-system-i386 -m "$MB" -display none -no-reboot -no-shutdown -serial "file:$log" \
    -drive "file=$OUT/disk.bin,format=raw,if=ide,index=0,media=disk" \
    -cdrom "$OUT/test.iso" -boot order=d >"$OUT/qemu.log" 2>&1 &
pid=$!
for _ in $(seq 240); do
    grep -q TAPOS_TEST_DONE "$log" 2>/dev/null && break
    sleep 0.25
done
kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null

grep -v TAPOS_TEST_DONE "$log"
if grep -q TAPOS_TEST_DONE "$log" && grep -q " 0 FAIL$" "$log"; then
    echo "MEM TESTS PASSED ($SUITE, $MB MiB)"
else
    echo "MEM TESTS FAILED ($SUITE, $MB MiB)"
    exit 1
fi
