# tapOS tests

| Command                            | What it does                                                                                             | Time  |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------- | ----- |
| `bash test/host/run_host_tests.sh` | Runs the real `src/filesystem/ext2.c` on laptop with a fake RAM disk, then health-checks the disk images | ~5 s  |
| `bash test/qemu/run_qemu_tests.sh` | Boots small test kernels in QEMU with the real ATA driver; includes two-boot persistence                 | ~15 s |

Needs: `gcc`, `nasm`, `ld`, `genisoimage`, `qemu-system-i386`, `python3`.

## Reading the output

```
PASS R01 read /laur                                   check passed
FAIL D04 ... | expected = 3 actual = 1                the code returned 1, the contract says 3
RESULT team: 13/14 PASS, 1 FAIL                       summary of one suite
FSCK OK / FSCK FAIL                                   health check of a disk image
```

A FAIL goes to the owner of the function with: test ID, input, expected, actual, and commit hash.

## Files

| File                        | Purpose                                                                                                   |
| --------------------------- | --------------------------------------------------------------------------------------------------------- |
| `common/fs_test.c/.h`       | Mini test framework: PASS/FAIL lines and the file content pattern                                         |
| `common/fs_cases.c/.h`      | All test cases, shared by the laptop and QEMU runs                                                        |
| `host/ram_disk.c/.h`        | Fake disk in memory: same `read_blocks` / `write_blocks` as `src/driver/disk.c`                           |
| `host/test_main.c`          | Laptop runner: `test_fs <suite> <image or --blank> [--save out.img]`                                      |
| `host/fsck.py`              | Disk health checker, independent of `ext2.c`                                                              |
| `fixtures/make_fixture.py`  | Builds `boundary.img`: edge-size files, deep folders, inode 112/113, a file and a folder both named `dup` |
| `fixtures/sample-image.bin` | Team fixture (note: its layout overlaps group 1 metadata, see fsck)                                       |
| `qemu/test_kernel.c`        | Kernel that runs one suite and prints to the serial port                                                  |
| `qemu/serial.c/.h`          | COM1 output so the script can read results                                                                |

## Memory tests

Run from the repo root. `<MiB>` is the RAM size, use 5 or 6. `<suite>` is `mem_suite_frames`, `mem_suite_fault`, `mem_suite_swap` or `mem_suite_load`.

```bash
bash test/qemu/run_mem_tests.sh <MiB> <suite>
```
