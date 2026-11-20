# tapOS

**Nama kelompok: tapOS**

Sistem operasi sederhana untuk platform **x86 32-bit Protected Mode**, dikembangkan menggunakan C dan Assembly NASM sebagai Tugas Besar **IF2130 Sistem Operasi 2026/2027**.

Implementasi ini mencakup kernel dan driver M1, EXT2 M2, serta paging, swap, Ring 3, syscall dan shell M3.

## Daftar Isi

- [Anggota Kelompok](#anggota-kelompok)
- [Maskot Kelompok](#maskot-kelompok)
- [Fitur yang Dibuat](#fitur-yang-dibuat)
- [Lingkungan Pengembangan](#lingkungan-pengembangan)
- [Cara Run](#cara-run)
- [Pengujian](#pengujian)
- [Batasan Saat Ini](#batasan-saat-ini)

## Anggota Kelompok

| NIM | Nama |
| --- | --- |
| 13525077 | Muhammad Abduh |
| 13525096 | Muhammad Ridwan Nasir Firdaus |
| 13525029 | Muhammad Naufal Hilmi |
| 13525039 | Aditya Rasyid |
| 13525044 | Steven Vanako |

## Maskot Kelompok

![Maskot Kelompok tapOS](maskot.gif)

## Fitur yang Dibuat

### Milestone 1 — Kernel, GDT, Interrupt, dan Driver

- Kernel dasar untuk x86 32-bit Protected Mode.
- Global Descriptor Table (GDT).
- Interrupt Descriptor Table (IDT), konfigurasi PIC, dan penanganan interrupt.
- Text framebuffer, kursor, dan scrolling.
- Input keyboard, Enter, Backspace, dan tombol panah.

### Milestone 2 — Filesystem

- Driver disk ATA PIO. Area filesystem menggunakan 4 MiB pertama dari disk image raw.
- Filesystem EXT2 IF2130 Edition dengan ukuran blok 512 byte.
- Inisialisasi filesystem baru dan pemuatan metadata filesystem yang sudah ada.
- Operasi `read()`, `read_directory()`, `write()`, dan `delete()`.
- Alokasi blok menggunakan First Fit global.
- Direct, single-indirect, dan double-indirect block pointers.
- Penolakan nama duplikat dalam satu direktori.
- Perlindungan entry `.` dan `..` serta root directory.
- Penolakan penghapusan direktori yang masih berisi.
- Pembatalan alokasi parsial jika penulisan gagal karena ruang tidak mencukupi.

### Milestone 3 — Memory Manager

- Paging 4 KiB dengan higher half kernel.
- Frame allocator dan deallocator.
- Page fault handler dengan alokasi page saat pertama diakses.
- Swap space 1 MiB setelah area EXT2 dan page replacement Clock.

### Milestone 3 — Shell

- REPL dan CWD di `src/user-shell.c`; parser, pipeline, dan command ada di `src/user/`.
- Command `cd`, `ls`, `mkdir`, `cat`, `cp`, `rm`, `mv`, `find`, dan `grep`. Shell mendukung pipeline serta `cp -r` dan `rm -r` untuk folder.
- Shell berjalan di Ring 3: syscall `int 0x30` tersambung, TSS aktif, dan kernel memuat `/shell` ke alamat 0.
- Target `make user-shell` menghasilkan `bin/shell` dan `bin/shell_elf`. Target `make insert-shell` memasukkan shell melalui inserter host di `src/external/`.
- Logika shell diuji di laptop (`bash tests/host/run_shell_tests.sh`) dan di QEMU dengan input keyboard (`bash tests/qemu/run_shell_qemu.sh`).
- Cara kerja, kontrak command, dan batasannya tersedia di [docs/shell.md](docs/shell.md).


## Lingkungan Pengembangan

| Tool | Versi yang digunakan |
| --- | --- |
| GCC | 11.4.0 |
| GNU ld / Binutils | 2.38 |
| NASM | 2.15.05 |
| genisoimage | 1.1.11 |
| qemu-system-i386 | 6.2.0 |
| qemu-img | 6.2.0 |

Diperlukan juga **Git**, **GNU Make**, dan **Python 3**. Compiler harus mendukung kompilasi target 32-bit dengan opsi `-m32`.

## Cara Run

Jalankan seluruh perintah melalui terminal Linux atau WSL.

### 1. Mengambil repository

```bash
git clone https://github.com/nighttbott/tapOS.git
cd tapOS
```

### 2. Membangun kernel dan ISO

```bash
make build
```

Hasil build:

| File | Keterangan |
| --- | --- |
| `bin/kernel` | Kernel ELF 32-bit |
| `bin/OS2025.iso` | Bootable ISO |

### 3. Menyiapkan disk

```bash
make disk
```

Target ini membuat `bin/storage.bin` dengan kapasitas 5 MiB jika file belum tersedia. Filesystem menggunakan 4 MiB pertama dan swap menggunakan 1 MiB berikutnya. Untuk migrasi image M2 berukuran 4 MiB yang sudah diverifikasi, buat salinan cadangan, lalu perluas image dengan `truncate -s 5M bin/storage.bin`. Target `make insert-shell` juga menyiapkan atau memeriksa disk sebelum menulis.

### 4. Memasukkan shell ke disk

```bash
make insert-shell
```

Perintah ini membangun `bin/shell` dan memasukkannya ke root `bin/storage.bin`. Jalankan kembali setiap kali kode shell berubah. Target ini memakai `inserter -f` supaya binary shell lama bisa diganti.

Untuk memasukkan file atau direktori lain, gunakan `bin/inserter <path> <parent_inode> bin/storage.bin`. Root memakai inode 1. Opsi `-f` mengizinkan penggantian file yang sudah ada. Direktori yang namanya bertabrakan ditolak. Konfigurasi **Inserter** di VS Code tersedia setelah `make disk user-shell`.

### 5. Menjalankan QEMU

```bash
qemu-system-i386 \
    -m 128M \
    -drive file=bin/storage.bin,format=raw,if=ide,index=0,media=disk \
    -cdrom bin/OS2025.iso -boot order=d
```

Kernel memuat `/shell` ke alamat 0, lalu menjalankannya di Ring 3. Prompt `tapOS:/$ ` akan muncul. Command M3 mencakup `cd`, `ls`, `mkdir`, `cat`, `cp`, `rm`, `mv`, `find`, dan `grep`, dengan dukungan pipeline `|`. `exec`, `ps`, dan `kill` masih menunggu layanan proses kernel M4. Detailnya ada di [docs/shell.md](docs/shell.md).

### 6. Menjalankan dengan debugger

```bash
make run
```

Target ini menunggu debugger pada `127.0.0.1:1234` sebelum menjalankan CPU.

Gunakan perintah QEMU pada langkah 5 untuk menjalankan OS langsung. Jika RAM diubah, gunakan nilai `SYSTEM_MEMORY_MB` yang sama pada build dan QEMU.

### 7. Membersihkan hasil build

```bash
make clean
```

Perintah ini membersihkan object files, kernel, dan ISO tanpa menghapus `bin/storage.bin`.

## Pengujian

Jalankan pengujian dari folder utama repository.

### Host

```bash
bash tests/host/run_host_tests.sh
```

Pengujian menjalankan implementasi filesystem pada RAM disk, memeriksa konsistensi image menggunakan FSCK, dan memastikan pembacaan fixture legacy tidak mengubah isinya.

Jika LeakSanitizer tidak didukung oleh lingkungan eksekusi:

```bash
ASAN_OPTIONS=detect_leaks=0 bash tests/host/run_host_tests.sh
```

Hasil akhir yang diharapkan:

```text
ALL HOST TESTS PASSED
```

### QEMU

```bash
bash tests/qemu/run_qemu_tests.sh
```

Pengujian mencakup disk driver ATA, pembacaan filesystem, operasi tulis/hapus, dan persistensi melalui dua kali boot.

QEMU dijalankan tanpa jendela, dengan hasil pengujian dicatat melalui serial port.

Hasil akhir yang diharapkan:

```text
ALL QEMU TESTS PASSED
```

Log setiap suite tersedia pada:

```text
bin/qemu-test/<suite>/serial.log
bin/qemu-test/<suite>/qemu.log
```

### Konsistensi image hasil QEMU

Setelah pengujian QEMU selesai:

```bash
python3 tests/host/fsck.py \
    bin/qemu-test/boundary.bin \
    bin/qemu-test/delete.bin \
    bin/qemu-test/blank.bin \
    bin/qemu-test/persist.bin
```

Setiap image harus menghasilkan `FSCK OK`.

Skrip pengujian menggunakan image terpisah dan tidak menimpa `bin/storage.bin`. Detail pengujian tersedia di [tests/README.md](tests/README.md).

## Batasan Saat Ini

- Input keyboard menggunakan buffer satu karakter.
- Kernel hanya menjalankan satu program user, yaitu `/shell`, dengan ukuran maksimal 256 KiB. Belum ada syscall `exit`.
- `cat`, `cp`, dan `grep` membaca file sampai 64 KiB. Keluaran tiap tahap dibatasi 16 KiB. `mv` mempertahankan inode dan data. Direktori tujuan mungkin memerlukan blok baru untuk entri namanya.
- Parser shell menerima paling banyak 127 karakter per baris dan belum mendukung tanda kutip.
- Process management dan scheduler merupakan pengembangan milestone berikutnya.
- Filesystem mengikuti format EXT2 IF2130 Edition dan tidak ditujukan untuk dipasang menggunakan driver EXT2 standar Linux.
