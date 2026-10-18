# tapOS

tapOS adalah sistem operasi sederhana untuk platform **x86 32-bit Protected Mode**, dikembangkan sebagai Tugas Besar IF2130 Sistem Operasi tahun 2026.

Proyek ini menggunakan bahasa C dan assembly NASM serta dijalankan melalui emulator QEMU. Dokumentasi ini berfokus pada Milestone 1: kernel dasar, GDT, interrupt, framebuffer, dan keyboard.

## Daftar Isi

- [Anggota Kelompok](#anggota-kelompok)
- [Maskot](#maskot)
- [Fitur Milestone 1](#fitur-milestone-1)
- [Kebutuhan Pengembangan](#kebutuhan-pengembangan)
- [Cara Menjalankan](#cara-menjalankan)
- [Debugging dengan Visual Studio Code](#debugging-dengan-visual-studio-code)
- [Struktur Proyek](#struktur-proyek)
- [Pengujian Dasar](#pengujian-dasar)
- [Referensi](#referensi)

## Anggota Kelompok

| NIM | Nama |
| --- | --- |
| 13525077 | Muhammad Abduh |
| 13525096 | Muhammad Ridwan Nasir Firdaus |
| 13525029 | Muhammad Naufal Hilmi |
| 13525039 | Aditya Rasyid |
| 13525044 | Steven Vanako |

## Maskot

<p align="center">
  <img src="maskot.gif" alt="Maskot tapOS" width="320">
</p>

## Fitur Milestone 1

| Komponen | Fungsi |
| --- | --- |
| Kernel dasar | Menyediakan entrypoint kernel dan stack awal melalui assembly. |
| Global Descriptor Table (GDT) | Menyediakan null descriptor, kernel code segment, dan kernel data segment. |
| Port I/O | Menyediakan fungsi untuk berkomunikasi dengan perangkat melalui port I/O. |
| Framebuffer | Menulis karakter dan warna, membersihkan layar, serta mengatur posisi kursor pada mode teks 80 × 25. |
| PIC remapping | Memindahkan vektor interrupt perangkat agar tidak bertabrakan dengan vektor exception CPU. |
| Interrupt Descriptor Table (IDT) | Menyimpan alamat handler interrupt dan memuatnya melalui instruksi `lidt`. |
| Interrupt Service Routine (ISR) | Menyimpan konteks register, memanggil handler C, lalu mengembalikan eksekusi melalui `iret`. |
| Driver keyboard | Membaca scancode keyboard, memetakan tombol ke karakter, dan menyediakan buffer input. |
| Tampilan input keyboard | Membaca buffer keyboard dan menampilkan karakter melalui framebuffer. |

## Kebutuhan Pengembangan

Gunakan Linux atau WSL dengan perangkat berikut:

- GNU Make
- GCC yang mendukung kompilasi 32-bit
- GNU Binutils, termasuk `ld`
- NASM
- QEMU untuk emulasi x86
- `qemu-img`
- `genisoimage`
- GDB untuk debugging

Visual Studio Code dapat digunakan dengan ekstensi C/C++ untuk menjalankan konfigurasi debugging proyek.

## Cara Menjalankan

Jalankan seluruh perintah berikut dari folder utama proyek.

### 1. Mengambil Repository

```bash
git clone https://github.com/nighttbott/tapOS.git
cd tapOS
```

### 2. Membangun Kernel dan ISO

```bash
make build
```

Proses ini menghasilkan:

- `bin/kernel`: executable kernel.
- `bin/OS2025.iso`: image ISO untuk boot melalui GRUB.

### 3. Menyiapkan Disk

Jika `bin/storage.bin` belum tersedia, jalankan:

```bash
make disk
```

Perintah ini membuat disk image berformat raw dengan ukuran 4 MiB.

Jalankan perintah tersebut hanya saat membuat disk baru. Membuat ulang disk pada path yang sama dapat menghapus data yang sebelumnya tersimpan.

### 4. Menjalankan Langsung di QEMU

```bash
qemu-system-i386 \
  -drive file=bin/storage.bin,format=raw,if=ide,index=0,media=disk \
  -cdrom bin/OS2025.iso \
  -boot order=d
```

Perintah ini menjalankan OS tanpa menunggu debugger. Klik jendela QEMU sebelum mengetik agar input keyboard diterima.

### 5. Membersihkan Hasil Build

```bash
make clean
```

Target ini membersihkan object, executable kernel, dan ISO sesuai konfigurasi Makefile. Disk image `bin/storage.bin` tetap disimpan.

## Debugging dengan Visual Studio Code

1. Buka folder proyek melalui Visual Studio Code pada lingkungan Linux atau WSL.
2. Pastikan `bin/storage.bin` sudah tersedia.
3. Buka panel **Run and Debug**.
4. Pilih konfigurasi debugging yang tersedia pada `.vscode/launch.json`.
5. Tekan **F5**.

Task `Launch QEMU` menjalankan QEMU dengan flag:

```text
-s -S
```

- `-s` membuka server GDB pada port `1234`.
- `-S` menghentikan CPU saat awal peluncuran sampai debugger melanjutkan eksekusi.

Jika QEMU masih menunggu debugger, layar kosong belum menunjukkan kegagalan boot. Hubungkan debugger dan lanjutkan eksekusi dengan **Continue**.

## Struktur Proyek

| Lokasi | Isi |
| --- | --- |
| `.kit/` | Kit dan referensi awal pengembangan setiap chapter. |
| `.vscode/` | Konfigurasi build, peluncuran QEMU, dan debugging. |
| `src/kernel.c` | Inisialisasi komponen kernel dan loop utama. |
| `src/kernel-entrypoint.s` | Multiboot header, stack awal, entrypoint, dan prosedur pemuatan GDT. |
| `src/cpu/` | Implementasi GDT, IDT, interrupt, dan port I/O. |
| `src/driver/` | Driver perangkat, termasuk keyboard dan disk. |
| `src/text/` | Implementasi framebuffer mode teks. |
| `src/stdlib/` | Fungsi bantu manipulasi memori. |
| `src/header/` | Deklarasi fungsi, struktur data, dan konstanta. |
| `src/filesystem/` | Pengembangan filesystem untuk milestone berikutnya. |
| `src/linker.ld` | Pengaturan tata letak executable kernel. |
| `src/menu.lst` | Konfigurasi boot GRUB. |
| `other/grub1` | Bootloader GRUB yang digunakan untuk pembuatan ISO. |
| `bin/` | Hasil build dan disk image. |
| `Makefile` | Perintah kompilasi, linking, pembuatan ISO, dan peluncuran QEMU. |

## Pengujian Dasar

Gunakan pemeriksaan berikut untuk memvalidasi Milestone 1:

- Kernel berhasil boot tanpa reboot berulang.
- GDT berhasil dimuat.
- Framebuffer dapat menampilkan karakter dengan warna yang sesuai.
- Layar dapat dibersihkan dan posisi kursor dapat diatur.
- Interrupt keyboard diteruskan ke `keyboard_isr()`.
- Ketikan huruf dan angka tampil pada layar.
- Pelepasan tombol tidak menghasilkan karakter tambahan.
- Keyboard tetap menerima input setelah penekanan tombol berulang.
- Setiap IRQ keyboard mendapat ACK.
- Buffer keyboard dikosongkan setelah dibaca.

Daftar ini merupakan panduan pengujian, bukan catatan bahwa seluruh pengujian telah lulus.

## Referensi

- IF2130 - Hub - 2026.
- IF2130 - Book - 2026.
- [Template OS IF2130 2026](https://github.com/Nathan-Marpaung/os-template-2026).
- [OSDev Wiki](https://wiki.osdev.org/).
