![Credit: 朧月](mascot_ossas.jpg)

# uvuwevwe_OSas

## Tugas Besar IF2130 Sistem Operasi - 2026/2027

### Nama Kelompok: uvuwevwe_OSas
Repository ini dibuat untuk memenuhi tugas besar mata kuliah IF2130 Sistem Operasi. Sistem Operasi dibuat menggunakan arsitektur x86 32-bit Protected Mode yang dapat dijalankan dengan virtual machine QEMU. Sistem Operasi ini memiliki beberapa fitur utama, diantaranya:
<ol>
  <li>Toolchain, Kernel, GDT</li>
  <li>Interrupt, Driver</li>
  <li>Filesystem</li>
  <li>User Mode, Shell</li>
  <li>Process, Scheduler, Multitasking</li>
</ol>

## Kontributor

| Nama | NIM | Bagian | Tanggung Jawab |
|---|---|---|---|
| Mulky Siraj Firizqi | 13525069 | A | 0.3, 2.6.2, 2.6.3, 3.4, 4.1.0, 4.1.1, 4.3.3 (memori virtual, CMOS, clock) |
| Fritz | 13525105 | B | 0.1, 0.2, 2.4, 2.6.1.2, 3.2, 4.1.2, 4.1.3, 4.1.4, 4.3.2 (PCB, process creation, process init, syscall kernel) |
| Juan | 13525093 | C | 1.1, 2.5, 2.6.1.1, 3.3, 4.2.1, 4.2.2, 4.2.3 (timer, Round Robin, context switch) |
| Gie | 13525144 | D | 1.2, 1.3, 2.1, 2.2, 3.1, 4.3.1, 4.3.2 (shell), 4.3.4, 4.3.7 (crt0, exec/ps/kill, Hello World, README) |

## Struktur Direktori
```
src/
├── cpu/
│   ├── cmos.c
│   ├── context-switch.s
│   ├── gdt.c
│   ├── idt.c
│   ├── interrupt.c
│   ├── intsetup.s
│   ├── isr.c
│   ├── isr.s
│   ├── keyboard.c
│   ├── pic.c
│   ├── portio.c
│   ├── speaker.c
│   └── syscall.c
├── external/
│   ├── args-demo.c
│   ├── beep.c
│   ├── clock.c
│   ├── crash.c
│   ├── demo.sh
│   ├── external-inserter.c
│   ├── grow.c
│   └── hello-world.c
├── header/
│   ├── cpu/
│   ├── filesystem/
│   ├── memory/
│   ├── process/
│   ├── stdlib/
│   ├── syscall/
│   ├── text/
│   ├── disk.h
│   └── kernel-entrypoint.h
├── process/
│   ├── process.c
│   └── scheduler.c
├── stdlib/
│   └── string.c
├── text/
│   └── terminal.c
├── crt0.s
├── disk.c
├── ext2.c
├── framebuffer.c
├── kernel-entrypoint.s
├── kernel.c
├── linker.ld
├── menu.lst
├── paging.c
├── swap.c
├── user-linker.ld
└── user-shell.c
Makefile
README.md
IF2130 - Jurnal - 2026 uvuwevwe_OSas.pdf
```

## Cara Menjalankan Virtual Machine QEMU

### Prasyarat

Pastikan perangkat telah menginstal:
- Git
- Make
- NASM
- Clang/LLVM
- LLD
- QEMU
- `mkisofs`

### Setup Pertama Kali

1. Clone repository:

   ```bash
   git clone https://github.com/Vinsju/uvuwevwe_OSas.git
   cd uvuwevwe_OSas
   ```

### Cara Setup Pertama Kali

```bash
make setup   # disk baru + masukkan semua program (shell, hello, clock, args-demo, grow, beep, demo.sh) + build kernel
make run     # jalankan OS (atau cukup: make)
```

> `make setup` membuat ulang `bin/storage.bin`, jadi semua file di disk lama akan terhapus.

Langkah manual yang setara:
  1. `make clean` (bersihkan build sebelumnya)
  2. `make disk` (buat disk image kosong)
  3. `make insert-all` (masukkan shell, hello, clock, args-demo, grow, beep, demo.sh ke file system)
  4. `make run` (build kernel + ISO lalu jalankan OS)

### Cara Run Virtual Machine QEMU

```bash
make run     # atau cukup: make
```

## Contoh Penggunaan Shell

| Perintah | Fungsi |
|---|---|
| `exec hello` | Menjalankan program Hello World (foreground) |
| `exec args-demo satu dua` | Menjalankan program dengan argumen (argc/argv) |
| `exec clock &` | Menjalankan jam di background, jam tampil di pojok kanan bawah dan shell tetap bisa dipakai |
| `exec clock` lalu `Ctrl+C` | Program foreground ditunggu shell sampai selesai, `Ctrl+C` untuk menghentikannya |
| `exec beep` | Menjalankan program yang memainkan suara do-re-mi-fa-so-la-si-do do-sol-mi-do |
| `exec grow` | Program yang membesarkan memorinya sendiri saat berjalan (lihat kolom MEM di `ps`) |
| `sh demo.sh` atau `./demo.sh` | Menjalankan file script shell (satu command per baris, baris `#` = komentar) |
| `ps` | Menampilkan daftar process aktif (PID, nama, state, memori) |
| `kill <pid>` | Menghentikan process dengan PID tertentu (contoh: `kill 1`) |
| `cd`, `ls`, `mkdir`, `cat`, `cp [-r]`, `rm [-r]`, `mv`, `find`, `grep` | Command file system, mendukung pipeline `\|` dan redirect `>` / `>>` |
| `help` | Menampilkan semua command |

`Ctrl+C` di prompt membatalkan baris yang sedang diketik.

| Tombol | Fungsi |
|---|---|
| `Tab` | Melengkapi nama command (kata pertama atau setelah `\|`) dan nama file/folder, termasuk path seperti `cat folder/da<Tab>`. Jika ada beberapa kandidat, dilengkapi sampai bagian yang sama lalu semua kandidat ditampilkan |
| `↑` / `↓` | Menelusuri riwayat command (maksimal 16 command terakhir, command berulang tidak disimpan dua kali). `↓` sampai paling bawah mengembalikan baris yang sedang diketik |

## Link Jurnal
https://docs.google.com/document/d/1bl7CNVy5vfpVLgzTN86kc9caABWwsXccOR462dNC_GM/edit?usp=sharing