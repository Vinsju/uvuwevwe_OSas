![Credit: 朧月](mascot.jpg)

IF2130 Sistem Operasi - 2026/2027
Tugas Besar IF2130 - Sistem Operasi 2026/2027

## README 

- Nama Kelompok : uvuwevwe_OSas
  
- Daftar Isi

## Struktur Direktori 
└── src/
   ├── cpu/
    │   ├── gdt.c
    │   ├── idt.c
    │   ├── isr.c
    │   ├── isr.s
    │   ├── keyboard.c
    │   ├── pic.c
    │   └── portio.c        
    │
    ├── header/
    │   ├── cpu/
    │   ├── text/
    │   ├── stdlib/
    │   └── kernel-entrypoint.h
    │
    ├── stdlib.c/
    │   └── string.c
    │
    ├── framebuffer.c
    ├── kernel.c
    ├── kernel-entrypoint.s
    ├── linker.ld
    └── menu.lst
  
- Cara Run :
  1. make clean
  2. make
  3. qemu-system-i386 -cdrom bin/uvuwevwe_OSas.iso
     
- Fitur yang Dibuat :
  -

- Maskot Kelompok : 
