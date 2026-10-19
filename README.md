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
- [Kontrol Keyboard](#kontrol-keyboard)
- [Hasil Pengujian Milestone 1](#hasil-pengujian-milestone-1)
- [Batasan Saat Ini](#batasan-saat-ini)
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
  <img src="maskot.gif" alt="Maskot tapOS" width="100%">
</p>

```text
+++++++++++++++++++++++++++++++++++++++++*++++****+*++*+*******************************************************+++*++++
++++++++++++++++++++++++++**+++++++++++++*++********************+***************************************+*********+++++
++++++++++++++++++++*++++++++*++*********++*****+******************************************************************+***
+++++++++++++++++++++++++++++++++**+++*********************************************************************************
+++++++++++++++++++*+**+*+****+**+*************************************************************************************
++++++++++++******+******++++++*+**************************************************************************************
++++++++++++*+********++***+*******************************************************************************************
++++++++++++++**+***+**************************************************************************************************
++++++++++*++************+*+*******************************************************************************************
*++*+***++++*++*************************************=::::::=+**********************+++*********************************
*****+******************************************=-:::::::::::::=****+=:::::::::::::::::::-*****************************
*+***********-=##*******++==--==***********==::::::::::::::--:::::::::::::::::::::::::::::::=**************************
*********:.**%%@==-==---------------===-::::-----::::-:-:=--::::::::::::::::::::::::::::::::::*************************
********-.%%*---------------=***+-::::::::--------::-------::::::::::::::::::----::::::::::---+*****#*###*#************
*********.*%@*##+++=========----::::--:----=======---+--:::::::::::::::::-:::----::::::::--===+*++*########%%#*********
********#+-@@@@@@@%%##**+=======-=-===--=======-===++-+----------=---------------:::::::--==+=+***#*#%#####%#%#********
****######.%@%@@@@@@@%#%%%####*=--==-========++===-===+=========+++++===+=-:-------:-----=++****########%%#%%%%#*******
*******###:#@%@@@%@@@%#########%++++*++++*+*++*+++++*+**************##*+-------------===+**++++++****###%%%%%%%%*******
*******####-=%%@@%%%%###########*++**++***********++*###%%%%#######+----====------===+*************+#####%%%##%%*******
******#*###########################***************#############%%*--===============+++******#%#**+****####%##%@#*******
******#****#############################%#######################=====---====+++++++*#*#******#*#*********++************
*****##***#########################%#%#######%#########%#######=========+++****##*+=###*####****####**+++**************
**********#**#########################%###########%##%########=-========++*######*+=############*###*******************
**************##################################%#%#########+====+*+++++*#%%######+=#####*#####*#*###**#***************
**************#############################################==========+*#####%%%#%#+==######*#######******#**#***#******
*********#*##*#*#*####*#################################%##+=++====+#########%%%%%*+++++==+##########***#***#*#**#*****
************#**##***###*####################################*++====+############%%%*=+*++*===#*#####*###*##**##**#*****
***************#**####*##*#*#################################*++++=+################%=##%#+**=#*#*####*#**###*#********
************#****###**##*##*#*#######################%#######**+++++###################################**###*#***#*****
*************##**##*#*#######*#####*##########################**+===*############################*######**#####****#***
***********#***#**#****##**##*#*###*#*##########################*+===*#############################*#############*#*##*
***********#*#*****#**##**#*##****###*###*####*#############%###**+===*#########*############*###*#####***##*#*##**#***
****#**#******#******#***##***#*#*##**##**####################%#%%*====+++=--############*#####################*#*#****
**************#******##*##*****#***######**#***##################%%+=+===+==*############################*###*##*#*****
*************#**#******##********#**#**#*#*#**#*########*##*########*=+=====++=+*########################*###*#**#***#*
*************#####*****#******#*#***#***##***##**#*######*####*#########==#-=*==####*#########################*********
+*********************#***#***#**#****#*#**#*############*###**#####*#####+##**##############*##*#*##*###*###*##*#*****
***+*****************#**********##*******#****###**###**#*##*###*#*###########*###*######*###*##########****#***#******
*****+*****************************##**#******##**#*##**###***#**###**#####*########*#####*######*##*#**#*#******##****
************##*****###**#*******###*#*#**#***#****####*####**##****###############*##########*######*#*#*******##*#****
```

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
| Tampilan input keyboard | Menampilkan karakter, menangani Enter dan Backspace, serta berpindah baris otomatis saat mencapai kolom terakhir. |
| Scrolling | Menggeser isi layar ke atas dan membersihkan baris terakhir saat output melewati batas bawah layar. Atribut warna ikut disalin. |
| Shift dan Caps Lock | Mendukung Shift kiri/kanan, huruf kapital, kombinasi Caps Lock dengan Shift, dan simbol pada layout US. |
| Tombol panah | Memindahkan kursor ke atas, bawah, kiri, dan kanan dengan pembatasan pada tepi layar. |
| Penanganan exception | Menampilkan nomor vector, error code, dan EIP dalam delapan digit heksadesimal, lalu menghentikan kernel. |
| Perlindungan akses buffer | Menyimpan dan memulihkan EFLAGS untuk melindungi operasi baca dan pengosongan buffer dari interupsi IRQ. |

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
  -boot order=d \
  -no-reboot \
  -no-shutdown
```

Perintah ini menjalankan OS tanpa menunggu debugger. Klik jendela QEMU sebelum mengetik agar input keyboard diterima. Opsi `-no-reboot` dan `-no-shutdown` membantu mengamati kondisi ketika guest mengalami kegagalan.

Target `make run` menjalankan QEMU dengan `-s -S`, sehingga CPU menunggu debugger. Gunakan perintah di atas untuk pengujian manual tanpa debugger.

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

## Kontrol Keyboard

| Tombol | Perilaku |
| --- | --- |
| Huruf, angka, dan simbol | Menulis karakter pada posisi kursor. |
| Enter | Memindahkan kursor ke kolom pertama baris berikutnya. |
| Backspace | Mundur satu sel dan menghapus karakter pada sel tersebut. Pada kolom pertama, berpindah ke kolom terakhir baris sebelumnya. Pada posisi kiri atas, tidak melakukan perubahan. |
| Shift kiri atau kanan | Mengubah huruf menjadi kapital saat Caps Lock mati dan memilih simbol Shift pada layout US. |
| Caps Lock | Mengaktifkan atau menonaktifkan kapitalisasi huruf. |
| Caps Lock + Shift | Menghasilkan huruf kecil. Simbol tetap mengikuti Shift. |
| Panah atas/bawah/kiri/kanan | Memindahkan kursor satu sel sesuai arah. Kursor berhenti pada tepi layar. |

Mengetik di tengah tulisan akan menimpa karakter pada sel tersebut. Saat pengetikan melewati kolom terakhir, kursor berpindah ke baris berikutnya. Saat output melewati baris terakhir, layar melakukan scrolling.

## Hasil Pengujian Milestone 1

Seluruh pengujian manual berikut dinyatakan lulus  pada **19 Oktober 2026** melalui pengujian di QEMU. Hasil ini mencakup skenario yang tercantum, bukan seluruh kemungkinan input atau exception CPU.

| Pengujian | Hasil yang diperiksa | Status |
| --- | --- | --- |
| Build bersih | `make clean` dan `make build` berhasil menghasilkan kernel serta ISO. | Lulus |
| Boot dan integrasi GDT | Kernel berhasil berjalan setelah memuat GDT dan menerima input keyboard. | Lulus |
| Tampilan awal | Layar dibersihkan dan kursor berada di kiri atas. | Lulus |
| Huruf dan angka | Karakter yang diketik tampil sesuai tombol. | Lulus |
| Shift kiri dan kanan | Kapitalisasi bekerja dan kembali sesuai status setelah Shift dilepas. | Lulus |
| Dua tombol Shift | Melepas salah satu Shift tidak membatalkan Shift lainnya yang masih ditekan. | Lulus |
| Caps Lock | Kapitalisasi dapat diaktifkan dan dinonaktifkan. | Lulus |
| Caps Lock + Shift | Huruf menjadi kecil dan simbol tetap mengikuti Shift. | Lulus |
| Simbol Shift | Simbol layout US, termasuk `!`, `@`, `#`, `{`, `}`, dan `\|`, tampil sesuai pemetaan. | Lulus |
| Enter dan Backspace | Pergantian baris, penghapusan sel, serta pembatasan di kiri atas bekerja. | Lulus |
| Tombol panah | Navigasi bekerja dan tidak melewati keempat tepi layar. | Lulus |
| Penimpaan karakter | Mengetik setelah memindahkan kursor menimpa sel yang dituju. | Lulus |
| Pergantian baris otomatis | Pengetikan melewati kolom ke-80 berlanjut pada baris berikutnya. | Lulus |
| Scrolling dan input berulang | Isi layar bergeser saat melewati baris ke-25 dan keyboard tetap menerima input. | Lulus |
| Exception breakpoint | Pemicu sementara `int3` menampilkan vector `00000003`, error code `00000000`, dan EIP delapan digit, lalu menghentikan kernel. | Lulus |
| Kembali ke operasi normal | Setelah pemicu exception dihapus dan kernel dibangun ulang, OS kembali menerima input. | Lulus |

## Batasan Saat Ini

- Input keyboard masih memakai buffer satu karakter. Input baru dapat menimpa input sebelumnya yang belum diambil. Antrean keyboard atau ring buffer belum diimplementasikan.
- Tombol panah mengatur posisi kursor layar. Penyisipan teks yang menggeser karakter setelah kursor belum tersedia.
- Backspace bekerja berdasarkan posisi sel, sehingga belum mencari akhir teks pada baris sebelumnya.
- Tab belum ditangani oleh loop tampilan kernel.
- Penanganan tombol extended berfokus pada empat tombol panah. Tombol lain belum didukung secara lengkap.
- Penanganan exception menghentikan kernel dan belum menyediakan mekanisme pemulihan.
- Filesystem, user mode, shell, dan multitasking berada di luar cakupan implementasi M1 yang didokumentasikan di sini.

## Referensi

- IF2130 - Hub - 2026.
- IF2130 - Book - 2026.
- [Template OS IF2130 2026](https://github.com/Nathan-Marpaung/os-template-2026).
- [OSDev Wiki](https://wiki.osdev.org/).
