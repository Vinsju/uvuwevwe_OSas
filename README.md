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
</ol>`

## Kontributor

| Nama | NIM | Bagian | Tanggung Jawab |
|---|---|---|---|
| Mulky Siraj Firizqi | 13525069 | A | 4.1.0, 4.1.1, 4.3.3 (memori virtual, CMOS, clock) |
| Fritz | TODO | B | 4.1.2, 4.1.3, 4.1.4, 4.3.2 (PCB, process creation, process init, syscall kernel) |
| Juan | TODO | C | 4.2.1, 4.2.2, 4.2.3 (timer, Round Robin, context switch) |
| Gie | TODO | D | 4.3.1, 4.3.2 (shell), 4.3.4, 4.3.7 (crt0, exec/ps/kill, Hello World, README) |

## Struktur Direktori
```
src/
├── cpu/
│   ├── cmos.c
│   ├── context-switch.s
│   ├── gdt.c
│   ├── idt.c
│   ├── interrupt.c
│   ├── isr.c
│   ├── isr.s
│   ├── keyboard.c
│   ├── pic.c
│   └── portio.c
├── external/
│   ├── args-demo.c
│   ├── clock.c
│   ├── crash.c
│   ├── external-inserter.c
│   └── grow.c
│   └── hello-world.c
├── header/
│   ├── cpu/
│   ├── filesystem/
│   ├── memory/
│   ├── process/
│   ├── stdlib/
│   ├── syscall/
│   ├── text/
│   └── user/
├── process/
├── stdlib/
├── text/
├── crt0.s
├── disk.c
├── ext2.c
├── fsutil.c
├── kernel-entrypoint.s
├── kernel.c
├── linker.ld
├── paging.c
├── swap.c
├── user-linker.ld
└── user-shell.c
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
make setup   # disk baru + masukkan shell, hello, clock, args-demo + build kernel
make run     # jalankan OS (atau cukup: make)
```

> `make setup` membuat ulang `bin/storage.bin`, jadi semua file di disk lama akan terhapus.

Langkah manual yang setara:
  1. `make clean` (bersihkan build sebelumnya)
  2. `make disk` (buat disk image kosong)
  3. `make insert-all` (masukkan shell, hello, clock, args-demo ke file system)
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
| `ps` | Menampilkan daftar process aktif (PID, nama, state) |
| `kill <pid>` | Menghentikan process dengan PID tertentu (contoh: `kill 1`) |
| `cd`, `ls`, `mkdir`, `cat`, `cp [-r]`, `rm [-r]`, `mv`, `find`, `grep` | Command file system, mendukung pipeline `\|` dan redirect `>` / `>>` |
| `help` | Menampilkan semua command |

`Ctrl+C` di prompt membatalkan baris yang sedang diketik.

| Tombol | Fungsi |
|---|---|
| `Tab` | Melengkapi nama command (kata pertama atau setelah `\|`) dan nama file/folder, termasuk path seperti `cat folder/da<Tab>`. Jika ada beberapa kandidat, dilengkapi sampai bagian yang sama lalu semua kandidat ditampilkan |
| `↑` / `↓` | Menelusuri riwayat command (maksimal 16 command terakhir, command berulang tidak disimpan dua kali). `↓` sampai paling bawah mengembalikan baris yang sedang diketik |