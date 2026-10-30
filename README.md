# tapOS

**Nama kelompok: tapOS**

Sistem operasi sederhana untuk platform **x86 32-bit Protected Mode**, dikembangkan menggunakan C dan Assembly NASM sebagai Tugas Besar **IF2130 Sistem Operasi 2026/2027**.

Implementasi saat ini mencakup **Milestone 1 dan Milestone 2**.

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

- Disk driver ATA PIO dengan disk image raw berukuran 4 MiB.
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

Buat disk hanya jika belum tersedia:

```bash
test -f bin/storage.bin || make disk
```

Disk disimpan pada `bin/storage.bin`. Filesystem diinisialisasi otomatis ketika disk baru digunakan.

`make disk` membuat disk 5 MiB: 4 MiB untuk filesystem dan 1 MiB untuk swap. Disk 4 MiB yang sudah ada dapat diperbesar tanpa menghapus isinya dengan `truncate -s 5M bin/storage.bin`.

**Jangan menjalankan `make disk` kembali pada disk yang ingin dipertahankan karena perintah tersebut membuat ulang disk image.**

### 4. Menjalankan QEMU

```bash
qemu-system-i386 \
    -drive file=bin/storage.bin,format=raw,if=ide,index=0,media=disk \
    -cdrom bin/OS2025.iso
```

Kernel menginisialisasi filesystem, kemudian menerima input keyboard.

| Input | Fungsi |
| --- | --- |
| Karakter yang dapat dicetak | Menampilkan karakter pada layar |
| Enter | Memindahkan kursor ke baris berikutnya |
| Backspace | Menghapus karakter pada sel sebelumnya |
| Tombol panah | Memindahkan posisi kursor |

Layar melakukan scrolling ketika penulisan melewati baris terakhir.

### 5. Menjalankan dengan debugger

```bash
make run
```

Target ini menjalankan QEMU dengan opsi `-s -S`. GDB server tersedia pada port `1234`, dan CPU menunggu debugger sebelum melanjutkan eksekusi.

Gunakan perintah QEMU pada langkah 4 untuk menjalankan OS langsung tanpa menunggu debugger.

### 6. Membersihkan hasil build

```bash
make clean
```

Perintah ini membersihkan object files, kernel, dan ISO tanpa menghapus `bin/storage.bin`.

## Pengujian

Jalankan pengujian dari folder utama repository.

### Host

```bash
bash test/host/run_host_tests.sh
```

Pengujian menjalankan implementasi filesystem pada RAM disk, memeriksa konsistensi image menggunakan FSCK, dan memastikan pembacaan fixture legacy tidak mengubah isinya.

Jika LeakSanitizer tidak didukung oleh lingkungan eksekusi:

```bash
ASAN_OPTIONS=detect_leaks=0 bash test/host/run_host_tests.sh
```

Hasil akhir yang diharapkan:

```text
ALL HOST TESTS PASSED
```

### QEMU

```bash
bash test/qemu/run_qemu_tests.sh
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
python3 test/host/fsck.py \
    bin/qemu-test/boundary.bin \
    bin/qemu-test/delete.bin \
    bin/qemu-test/blank.bin \
    bin/qemu-test/persist.bin
```

Setiap image harus menghasilkan `FSCK OK`.

Skrip pengujian menggunakan image terpisah dan tidak menimpa `bin/storage.bin`. Detail pengujian tersedia di [test/README.md](test/README.md).

## Batasan Saat Ini

- Input keyboard menggunakan buffer satu karakter.
- Filesystem diakses melalui interface kernel dan kernel pengujian; command shell belum tersedia.
- User mode, shell, process management, dan scheduler merupakan pengembangan milestone berikutnya.
- Filesystem mengikuti format EXT2 IF2130 Edition dan tidak ditujukan untuk dipasang menggunakan driver EXT2 standar Linux.