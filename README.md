![Credit: 朧月](mascot_ossas.jpg)

# Tugas Besar IF2130 Sistem Operasi - 2026/2027

## Nama Kelompok: uvuwevwe_OSas
Repository ini dibuat untuk memenuhi tugas besar mata kuliah IF2130 Sistem Operasi. Sistem Operasi dibuat menggunakan arsitektur x86 32-bit Protected Mode yang dapat dijalankan dengan virtual machine QEMU. Sistem Operasi ini memiliki beberapa fitur utama, diantaranya:
<ol>
  <li>Toolchain, Kernel, GDT</li>
  <li>Interrupt, Driver</li>
  <li>Filesystem</li>
  <li>User Mode, Shell</li>
  <li>Process, Scheduler, Multitasking</li>
</ol>

## Struktur Direktori 
```
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
    │   │   ├── gdt.h
    │   │   ├── idt.h
    │   │   ├── isr.h
    │   │   ├── keyboard.h
    │   │   ├── pic.h
    │   │   └── portio.h
    │   ├── filesystem/
    │   │   └── ext2.h
    │   ├── stdlib/
    │   │   └── string.h
    │   ├── text/
    │   │   └── framebuffer.h
    │   ├── disk.h
    │   └── kernel-entrypoint.h
    │
    ├── stdlib/
    │   └── string.c
    │
    ├── disk.c
    ├── ext2.c
    ├── framebuffer.c
    ├── kernel.c
    ├── kernel-entrypoint.s
    ├── linker.ld
    └── menu.lst
```
  
## Cara Run Virtual Machine QEMU:
  1. make clean
  2. make build
  3. make run
  4. qemu-system-i386 -cdrom bin/uvuwevwe_OSas.iso
     
