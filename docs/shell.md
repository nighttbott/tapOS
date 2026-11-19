# Shell tapOS (Milestone 3, Anggota D)

Dokumen ini menjelaskan isi `src/user/`, cara kerjanya, kontrak untuk command milik E, dan hal yang masih menunggu anggota lain.

**Status:** logika shell selesai dan lulus test host. Shell belum pernah berjalan di Ring 3, karena loader, build user, dan penyambungan syscall belum ada (lihat [Yang masih dibutuhkan](#yang-masih-dibutuhkan-dari-anggota-lain)).

## Daftar file

| File | Isi |
| --- | --- |
| `src/user/shell.h` | Kontrak untuk E: `ShellIO`, tipe command, API CWD, resolusi path |
| `src/user/shell.c` | REPL, pembaca baris, parser, dispatch, pipeline, CWD, resolusi path |
| `src/user/grep.c` | Command `grep` |
| `src/user/syscall.h`, `syscall.c` | Enam wrapper `int 0x30`, hanya dipakai di build user |
| `test/host/shell_mock_syscall.c` | Wrapper versi laptop, memanggil `ext2.c` di RAM disk |
| `test/host/shell_test.c` | Test H01–H17 |
| `test/host/run_shell_tests.sh` | Build freestanding, build host, lalu menjalankan test |

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
| `0x20`–`0x7E` | Disimpan dan di-echo, sampai 127 karakter |
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

Dua buffer statis 4096 byte dipakai bergantian. Setelah sebuah stage selesai:

| Kondisi | Tindakan |
| --- | --- |
| `io.overflow` | Cetak `pipeline: output too large`, buang keluaran, berhenti |
| Return bukan 0 | Cetak keluaran stage itu (berisi pesan error), berhenti |
| Stage terakhir | Cetak keluaran |
| Selain itu | Keluaran menjadi masukan stage berikutnya |

Tidak ada stderr terpisah. Pesan error ditulis lewat `shell_out`, dan return bukan 0 membuatnya tampil di layar, bukan masuk ke stage berikutnya.

### CWD dan resolusi path

State CWD adalah `cwd_inode` dan `cwd_path`, diawali root (inode 1, `/`).

Tidak ada syscall untuk mencari inode, jadi `dir_child` memakai sifat filesystem: `read_directory` dengan `name` dan `parent_inode` membaca folder tujuan, dan entri pertama folder selalu `.` yang berisi inode folder itu sendiri. Nama `..` bekerja dengan cara yang sama.

`walk` mengikuti path per komponen, mulai dari root bila path diawali `/`, atau dari CWD bila tidak:

- Komponen kosong dan `.` dilewati, jadi `//docs///a/./b/` sama dengan `/docs/a/b`.
- `..` memakai data filesystem, bukan sekadar memotong string. Di root hasilnya tetap root.
- Folder yang tidak ada, atau file di tengah path, menghasilkan -1.

`shell_resolve_dir` mengikuti seluruh path dan mengisi path absolut. `shell_resolve_parent` hanya mengikuti sampai sebelum komponen terakhir, sehingga komponen terakhir boleh belum ada (untuk `mkdir`, atau `cp` ke nama baru).

### grep

| Bentuk | Perilaku |
| --- | --- |
| `grep POLA` | Membaca keluaran stage sebelumnya; stage pertama: `grep: no input` |
| `grep POLA FILE` | Membaca file (maksimal 16 KiB), relatif terhadap CWD |
| Jumlah argumen lain | `usage: grep PATTERN [FILE]` |

Bila FILE diberikan, masukan dari pipeline diabaikan. Pencocokan memakai substring biasa dan case-sensitive. Baris yang cocok ditulis utuh dengan `'\n'`, termasuk baris terakhir yang tidak punya `'\n'`. Tidak ada baris yang cocok bukan error.

| Kode `read` | Pesan |
| --- | --- |
| 1 | `grep: FILE: not a file` |
| 2 | `grep: FILE: file too large` |
| 3, atau path gagal di-resolve | `grep: FILE: not found` |
| Lainnya | `grep: FILE: read failed` |

`read` tidak mengembalikan ukuran file. Karena itu buffer dikosongkan dulu, lalu panjangnya dihitung dari byte bukan nol terakhir. Akibatnya, byte NUL di akhir file ikut terbuang.

## Panduan menulis command (untuk E)

```c
// src/user/commands.h
int cmd_cat(int argc, char **argv, struct ShellIO *io);

// src/user/shell.c, tambahkan satu baris di tabel commands
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

`int 0x30`, `EAX` = nomor, `EBX` = argumen 1, `ECX` = argumen 2, hasil di `EAX`. Nomornya diambil dari `enum SyscallNumber`, dan berbeda dari penomoran di buku.

| No | Wrapper | Hasil |
| --- | --- | --- |
| 1 | `sys_write_text(buf, len)` | `len`, atau -1 |
| 2 | `sys_read_char(out)` | 1 ada tombol, 0 belum ada, -1 gagal |
| 3 | `sys_read_file(req)` | Kode hasil `read` |
| 4 | `sys_read_dir(req)` | Kode hasil `read_directory` |
| 5 | `sys_write_file(req)` | Kode hasil `write` |
| 6 | `sys_delete(req)` | Kode hasil `delete` |

## Menjalankan test

```bash
bash test/host/run_shell_tests.sh
```

Test memakai `ext2.c` asli di RAM disk, jadi resolusi path dan `grep` diuji terhadap filesystem sungguhan. Yang diganti hanya batas syscall. Command uji (`fruit`, `prefix`, dan lainnya) hanya ada di `shell_test.c` dan masuk lewat parameter tabel `shell_execute_commands`.

Di checkout Windows dengan `core.autocrlf=true`, semua script `.sh` memakai CRLF dan gagal saat dijalankan dari WSL. Jalankan dari clone di dalam WSL.

## Batasan

| Batasan | Nilai | Akibat |
| --- | --- | --- |
| Panjang baris | 127 karakter | Ketikan berikutnya diabaikan |
| Keluaran per stage | 4096 byte | Lebih dari itu: `pipeline: output too large`, tanpa keluaran parsial. `cat` pada file besar akan gagal |
| Ukuran file `grep` | 16 KiB | `file too large` |
| Ukuran folder yang bisa dimasuki | 16 KiB (32 blok) | Folder lebih besar dianggap tidak ada |
| Panjang path | 255 karakter | `shell_resolve_dir` gagal |
| Tanda kutip | Tidak didukung | Argumen tidak bisa berisi spasi atau `\|` |
| Riwayat dan edit dengan panah | Tidak ada | Tombol panah diabaikan |

## Yang masih dibutuhkan dari anggota lain

Shell tidak bisa berjalan di QEMU sampai ini selesai.

**C (syscall dan user mode):**

1. Menambahkan `src/cpu/syscall.c` ke target `kernel` di `Makefile`.
2. Memasang gate `0x30` dengan DPL 3 di `src/cpu/idt.c`.
3. Menambahkan case `SYSCALL_VECTOR` di `main_interrupt_handler` yang memanggil `syscall_dispatch`.
4. Memanggil `initialize_tss()` di `kernel_setup`.
5. Mempertahankan `keyboard_state_activate()` saat loop keyboard di `kernel_setup` diganti dengan masuk user mode, karena tidak ada syscall untuk mengaktifkannya.
6. `'\b'` di kolom 0 tidak naik ke baris sebelumnya (`syscall_write_char`). Baris panjang yang terbungkus ke baris berikutnya akan tampil tidak sesuai saat dihapus, walaupun isi buffer tetap benar.

**A (build user dan loader):**

1. Menyediakan `crt0` (`_start` memanggil `main`), linker script user, target Makefile, dan host inserter.
2. Link `shell.c`, `grep.c`, `syscall.c`, dan `src/stdlib/string.c`. Uji link dengan `ld -Ttext 0` berhasil tanpa simbol yang hilang.
3. Memetakan seluruh binary dan mengosongkan `.bss`. Ukurannya sekitar 6 KB kode dan 41 KB `.bss`. `.bss` harus sudah present, karena `syscall_validate_user_range` menolak buffer pada halaman yang belum dipetakan, walaupun ada page fault handler.
4. Menyediakan stack user, lalu memanggil `enter_user_mode(entry, stack_top)`.
5. Compile dengan `-fno-pie`. Tanpa itu, GCC di Ubuntu menghasilkan referensi `_GLOBAL_OFFSET_TABLE_`.
