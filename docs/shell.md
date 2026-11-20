## Daftar file

| File | Isi |
| --- | --- |
| `src/user/shell.h` | `ShellIO`, tipe command, API CWD, dan resolusi path |
| `src/user-shell.c` | REPL, pembaca baris, tabel command, CWD, resolusi path |
| `src/user/shell-parser.c` | Memecah baris menjadi argumen dan tahap pipeline |
| `src/user/shell-pipeline.c` | Dispatch dan buffer keluaran antar-tahap |
| `src/user/command/` | Navigasi, operasi file, pencarian, dan command proses |
| `src/user/fsutil.c` | Fungsi filesystem yang dipakai bersama oleh perintah shell |
| `src/user/syscall.h`, `syscall.c` | Enam wrapper `int 0x30`, hanya dipakai di build user |
| `tests/host/shell_mock_syscall.c` | Wrapper versi laptop, memanggil `ext2.c` di RAM disk |
| `tests/host/shell_test.c` | Test H01–H46 |
| `tests/host/run_shell_tests.sh` | Build freestanding, build host, lalu menjalankan test |
| `src/crt0.s`, `src/user-linker.ld` | Titik masuk dan layout binary user di alamat 0 |
| `src/external/external-inserter.c` | Memasukkan file ke `storage.bin` dari laptop |
| `tests/qemu/run_shell_qemu.sh`, `shell_keys.py` | Test shell di QEMU dengan input keyboard |

## Alur satu perintah

```
main()
 └─ loop:
     shell_print_prompt()      "tapOS:/docs$ "
     shell_read_line(line)     sys_read_char berulang, echo, Backspace
     shell_execute_line(line)
      └─ shell_execute_commands(line, commands, n)
          1. shell_parse       memecah line menjadi stage dan argv
          2. find_command      mencari SEMUA stage dulu
          3. jalankan stage    keluaran stage i menjadi masukan stage i+1
          4. sys_write_text    mencetak keluaran stage terakhir
```

Contoh `cat a | grep x`:

| Langkah | `in` | `out` |
| --- | --- | --- |
| Stage 0, `cat a` | `NULL` | `buffers[0]` |
| Stage 1, `grep x` | `buffers[0]` | `buffers[1]` |
| Cetak | | `buffers[1]` ke layar |

## Bagian-bagiannya

### Pembaca baris

`sys_read_char` tidak menunggu, jadi `shell_read_line` mengulang panggilan sampai hasilnya 1.

| Karakter | Perlakuan |
| --- | --- |
| `0x20`–`0x7E` | Disimpan dan di-echo, sampai 127 karakter. Masukan yang melewati batas dibatalkan saat Enter. |
| `'\b'` | Menghapus satu karakter bila baris tidak kosong |
| `'\n'` | Mengakhiri baris |
| Lainnya (panah, Tab, Esc) | Diabaikan |

### Parser

`shell_parse` berjalan satu kali di atas `line` dan mengganti spasi serta `|` dengan `'\0'`, sehingga `argv` menunjuk langsung ke `line` tanpa salinan. Fungsi ini mengembalikan `NULL` bila berhasil, atau pesan error.

| Masukan | Hasil |
| --- | --- |
| Baris kosong atau hanya spasi | `count = 0`, tidak ada yang dijalankan |
| `ls \|`, `\| grep a`, `a \|\| b` | `shell: empty command in pipeline` |
| Lebih dari 16 argumen | `shell: too many arguments` |
| Lebih dari 4 stage | `shell: too many commands in pipeline` |

Tanda kutip tidak didukung.

### Dispatch dan pipeline

Semua stage dicari di tabel **sebelum** ada yang dijalankan. Jadi `rm a | typo` hanya mencetak `typo: command not found` dan `a` tidak terhapus.

Dua buffer statis 16 KiB (`SHELL_IO_BUFFER`) dipakai bergantian, sehingga file 16 KiB bisa lewat pipeline. Setelah sebuah stage selesai:

| Kondisi | Tindakan |
| --- | --- |
| `io.overflow` | Cetak `pipeline: output too large`, buang keluaran, berhenti |
| Return bukan 0 | Cetak keluaran stage itu (berisi pesan error), berhenti |
| Stage terakhir | Cetak keluaran |
| Selain itu | Keluaran menjadi masukan stage berikutnya |

Tidak ada stderr terpisah. Pesan error ditulis lewat `shell_out`, dan return bukan 0 membuatnya tampil di layar, bukan masuk ke stage berikutnya.

### CWD dan resolusi path

State CWD adalah `cwd_inode` dan `cwd_path`, diawali root (inode 1, `/`).

`dir_child` membaca direktori per blok dan mencari entri berdasarkan namanya. Nama `..` dibaca dari blok pertama inode direktori saat ini.

`walk` mengikuti path per komponen, mulai dari root bila path diawali `/`, atau dari CWD bila tidak:

- Komponen kosong dan `.` dilewati, jadi `//docs///a/./b/` sama dengan `/docs/a/b`.
- `..` memakai data filesystem, bukan sekadar memotong string. Di root hasilnya tetap root.
- Folder yang tidak ada, atau file di tengah path, menghasilkan -1.

`shell_resolve_dir` mengikuti seluruh path dan mengisi path absolut. `shell_resolve_parent` hanya mengikuti sampai sebelum komponen terakhir, sehingga komponen terakhir boleh belum ada (untuk `mkdir`, atau `cp` ke nama baru).

### grep

| Bentuk | Perilaku |
| --- | --- |
| `grep POLA` | Membaca keluaran stage sebelumnya; stage pertama: `grep: no input` |
| `grep POLA FILE` | Membaca file (maksimal 64 KiB), relatif terhadap CWD |
| Jumlah argumen lain | `usage: grep PATTERN [FILE]` |

Bila FILE diberikan, masukan dari pipeline diabaikan. Pencocokan memakai substring biasa dan case-sensitive. Baris yang cocok ditulis utuh dengan `'\n'`, termasuk baris terakhir yang tidak punya `'\n'`. Tidak ada baris yang cocok bukan error.

| Kode `read` | Pesan |
| --- | --- |
| 1 | `grep: FILE: is a directory` |
| 2 | `grep: FILE: file too large` |
| 3, atau path gagal di-resolve | `grep: FILE: not found` |
| Lainnya | `grep: FILE: read failed` |

`grep` memakai pembaca file yang sama dengan `cat` dan mempertahankan panjang file sesuai data EXT2. Hasil yang cocok tetap tunduk pada batas 16 KiB per tahap pipeline.

## Panduan menulis command (untuk E)

```c
// src/user/commands.h
int cmd_cat(int argc, char **argv, struct ShellIO *io);

// src/user-shell.c, tambahkan satu baris di tabel commands
{ "cat", cmd_cat },
```

Contoh dengan path relatif terhadap CWD:

```c
int cmd_cat(int argc, char **argv, struct ShellIO *io) {
    static char data[16384];
    char name[SHELL_NAME_MAX + 1];
    uint32_t parent;

    if (argc != 2) {
        shell_out_str(io, "usage: cat FILE\n");
        return 1;
    }
    if (shell_resolve_parent(argv[1], &parent, name) != 0) {
        shell_out_str(io, "cat: not found\n");
        return 1;
    }
    // Isi struct EXT2DriverRequest, panggil sys_read_file, lalu shell_out.
    return 0;
}
```

Aturan:

1. Selalu menulis lewat `shell_out` atau `shell_out_str`. `sys_write_text` langsung akan melewati pipeline.
2. Return 0 bila sukses. Bila gagal, tulis pesan error lalu return bukan 0.
3. `io->in` bernilai `NULL` pada stage pertama, dan tidak berakhir `'\0'`. Pakai `io->in_len`.
4. Tidak ada libc: pakai `shell_strlen`, `shell_strcmp`, dan `memcpy`/`memset`/`memcmp` dari `header/stdlib/string.h`. Buffer besar harus `static`.
5. `cd` memanggil `shell_resolve_dir`, lalu `shell_cwd_set(inode, path)`.
6. Saat membaca `read_directory`, kosongkan buffer dulu dan berhenti pada `rec_len == 0`. Lewati entri dengan `inode == 0`. Buffer harus muat seluruh folder, kalau tidak hasilnya -1.
7. `read` tidak mengembalikan ukuran, jadi `cat` memerlukan cara yang sama seperti `grep`.

## ABI syscall

`int 0x30`, `EAX` = nomor, `EBX`/`ECX`/`EDX` = argumen 1–3, hasil di `EAX`. Nomornya diambil dari `enum SyscallNumber`, dan berbeda dari penomoran di buku.

| No | Wrapper | Hasil |
| --- | --- | --- |
| 1 | `sys_write_text(buf, len)` | `len`, atau -1 |
| 2 | `sys_read_char(out)` | 1 ada tombol, 0 belum ada, -1 gagal |
| 3 | `sys_read_file(req)` | Kode hasil `read` |
| 4 | `sys_read_dir(req)` | Kode hasil `read_directory` |
| 5 | `sys_write_file(req)` | Kode hasil `write` |
| 6 | `sys_delete(req)` | Kode hasil `delete` |
| 7 | `sys_exit(code)` | Tidak kembali |
| 8 | `sys_exec(req, pid_out)` | Kode `PROCESS_CREATE_*` (0 sukses), -1 request tidak valid |
| 9 | `sys_kill(pid)` | 0 berhasil, 1 tidak ada, 2 ditolak (init) |
| 10 | `sys_process_list(buf, capacity)` | Jumlah baris `struct SyscallProcessInfo`, -1 buffer tidak valid |
| 11 | `sys_get_time(out)` | 0, atau -1 |
| 12 | `sys_write_status(buf, len, col)` | Jumlah karakter di baris status, atau -1 |

**Status per 19 November:** nomor 7–12, `struct SyscallProcessInfo`, dan wrapper-nya sudah ada. Kernel belum menangani nomor 7–12, karena case di `syscall_dispatch` memanggil API proses (A), scheduler (C), konsol baris status (C), dan CMOS (E) yang belum dibuat. Sampai saat itu, kernel menjawab -1, dan `crt0` berputar di tempat setelah `sys_exit`.

## Command proses (`src/user/command/process.c`)

| Command | Perilaku | Pesan |
| --- | --- | --- |
| `exec PATH` | Path relatif terhadap CWD (`exec /hello` dari folder lain). Shell tidak menunggu programnya selesai | `exec: started NAME (PID n)`; kode 1 `exec: too many processes`; 3 `exec: not enough memory`; 4 atau path gagal `exec: PATH: not found`; lainnya `exec: invalid request` |
| `ps` | Tabel `PID  STATE    NAME`, satu baris per proses. Lebar kolom PID 5, STATE 9 | Kernel gagal: `ps: cannot read the process list` |
| `kill PID` | Desimal saja. Berhasil tanpa pesan | `kill: PID: no such process`; `kill: cannot kill init (PID 1)`; angka tidak valid `usage: kill PID` |

Path yang gagal di-resolve dan PID yang tidak valid tidak pernah dikirim ke kernel. Ketiganya bisa dipakai di pipeline, misalnya `ps | grep clock`.

## Menjalankan test

```bash
bash tests/host/run_shell_tests.sh
```

Test memakai `ext2.c` asli di RAM disk, jadi resolusi path dan `grep` diuji terhadap filesystem sungguhan. Yang diganti hanya batas syscall. Command uji (`fruit`, `prefix`, dan lainnya) hanya ada di `shell_test.c` dan masuk lewat parameter tabel `shell_execute_commands`.

Di checkout Windows dengan `core.autocrlf=true`, semua script `.sh` memakai CRLF dan gagal saat dijalankan dari WSL. Jalankan dari clone di dalam WSL.

## Batasan

| Batasan | Nilai | Akibat |
| --- | --- | --- |
| Panjang baris | 127 karakter | Shell menolak seluruh baris bila batas terlewati |
| Keluaran per stage | 16 KiB | Lebih dari itu: `pipeline: output too large`, tanpa keluaran parsial |
| Ukuran file `grep` | 64 KiB | `file too large` |
| Direktori untuk `cp -r` dan `rm -r` | 16 KiB dan kedalaman 64 | Operasi rekursif memiliki batas buffer dan kedalaman |
| Panjang path | 255 karakter | `shell_resolve_dir` gagal |
| Tanda kutip | Tidak didukung | Argumen tidak bisa berisi spasi atau `\|` |
| Riwayat dan edit dengan panah | Tidak ada | Tombol panah diabaikan |

## Menjalankan shell di Ring 3

Bagian ini awalnya milik A (build user, loader) dan C (penyambungan syscall). Karena anggota lain tidak bisa dihubungi, bagian ini dikerjakan oleh D.

```bash
make all insert-shell
qemu-system-i386 -m 128M -drive file=bin/storage.bin,format=raw,if=ide,index=0,media=disk -cdrom bin/OS2025.iso -boot order=d
```

| Target `make` | Hasil |
| --- | --- |
| `user-shell` | `bin/shell` untuk dimuat di alamat 0 dan `bin/shell_elf` untuk simbol GDB. Objeknya ada di `bin/user/`. |
| `inserter` | `bin/inserter`, program laptop yang memakai `ext2.c` asli |
| `insert-shell` | Memasukkan `bin/shell` ke root `bin/storage.bin`, menimpa versi lama |
| `run` | Sekarang juga menjalankan `insert-shell` |

`inserter <file-atau-folder> <parent_inode> <storage.bin>` menggunakan nama dasar input sebagai nama entri. Folder disalin rekursif ke folder baru. Folder tujuan yang sudah ada dan symbolic link ditolak. Inserter bekerja pada 4 MiB pertama ketika QEMU berhenti.

### Alur boot

```
kernel_setup
 ├─ GDT, paging, PIC, IDT (gate 0x30 dengan DPL 3), keyboard, filesystem
 ├─ initialize_tss          esp0 = stack kernel 64 KiB untuk syscall
 ├─ keyboard_state_activate tidak ada syscall untuk ini
 ├─ load_shell
 │   ├─ petakan 0x0–0x3FFFF (256 KiB) dan 4 halaman stack di bawah 0xC0000000
 │   └─ read("shell") ke alamat 0
 └─ enter_user_mode(0, 0xC0000000)
      └─ _start (crt0.s) → main (user-shell.c)

int 0x30 → main_interrupt_handler → case SYSCALL_VECTOR → syscall_dispatch
```

| File | Perubahan |
| --- | --- |
| `src/kernel.c` | Loop echo keyboard diganti dengan `load_shell` dan `enter_user_mode` |
| `src/cpu/idt.c` | Gate `0x30` dengan DPL 3. Dengan DPL 0, `int 0x30` dari Ring 3 memicu #GP |
| `src/cpu/interrupt.c` | Case `SYSCALL_VECTOR` memanggil `syscall_dispatch` |
| `src/cpu/gdt.c` | Stack TSS dari 4 KiB menjadi 64 KiB, karena `ext2.c` memakai buffer blok di stack |
| `Makefile` | `syscall.c` ikut kernel; target `user-shell`, `inserter`, `insert-shell` |
| `src/crt0.s` | `_start` di section `.text.entry`, lalu memanggil `main` |
| `src/user-linker.ld` | Binary flat di alamat 0, dengan `ASSERT` ukuran |
| `src/external/external-inserter.c` | Inserter host. Hanya target `inserter` yang mengompilasinya; kernel dan kernel uji memakai daftar source kernel |

Hal yang perlu diingat:

- **Semua halaman dipetakan di awal.** `syscall_validate_user_range` hanya menerima halaman yang present, jadi buffer `.bss` tidak bisa menunggu page fault handler. Halaman baru selalu dikosongkan, sehingga `.bss` otomatis nol.
- **Ukuran gambar program tetap 256 KiB.** `USER_IMAGE_SIZE` di `src/kernel.c` dan `ASSERT` di `user-linker.ld` harus sama. Build gagal bila program melewati batas ini. Periksa ukuran aktual dengan `size bin/shell_elf`.
- **Kalau `main` selesai, program berputar di `_start`**, karena syscall `exit` baru ada di Milestone 4.
- **Kalau `/shell` tidak ada**, kernel menulis `tapOS: /shell not found, run: make insert-shell` lalu berhenti.

### Test QEMU

```bash
bash tests/qemu/run_shell_qemu.sh
```

Script ini membuat disk sendiri, memasukkan `shell` dan file `notes`, lalu `tests/qemu/shell_keys.py` mengetik lewat monitor QEMU (`sendkey`) dan membaca layar teks dari `0xB8000` (`pmemsave`).

| ID | Kasus |
| --- | --- |
| Q01 | Prompt `tapOS:/$` muncul setelah boot |
| Q02 | Backspace mengubah tampilan dan isi baris |
| Q03 | Command tidak dikenal |
| Q04 | `grep` membaca file nyata dari disk |
| Q05 | Pipeline `grep a notes \| grep l` |
| Q06 | Pesan error `grep` |
| Q07 | Pesan error parser |
| Q08 | Prompt kembali setelah semua perintah |

Q09–Q18 mencakup CRUD, pipeline `cat | grep`, perubahan CWD, persistensi setelah reboot, dan hasil impor folder. Layar dan log disimpan di `bin/shell-qemu-128/` secara default. Gunakan `bash tests/qemu/run_shell_qemu.sh 5` untuk menjalankan suite dengan RAM 5 MiB.

### Catatan untuk kode kernel

- `'\b'` di kolom 0 tidak naik ke baris sebelumnya (`syscall_write_char`). Baris panjang yang terbungkus akan tampil tidak sesuai saat dihapus, walaupun isi buffer tetap benar.
- Halaman user bisa dipindahkan ke swap saat RAM penuh. Setelah itu syscall yang memakai buffer di halaman tersebut akan gagal, karena validasi hanya menerima halaman present. Dengan RAM default 128 MiB, hal ini tidak terjadi.
