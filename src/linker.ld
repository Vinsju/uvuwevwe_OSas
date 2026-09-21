ENTRY(loader) /* Nama entry label */
SECTIONS {
    . = 0x00100000; /* kode di-load mulai 1 MB */
   .multiboot ALIGN (0x1000) :  /* Align pada 4 KB */
    { 
        *(.multiboot) /* GRUB Multiboot header */
    }
    .text ALIGN (0x1000) :  
    { 
        *(.text)  /* Text section */
    }
    .rodata ALIGN (0x1000) : 
    { 
        *(.rodata*)  /* Read-only data section */
    }
    .data ALIGN (0x1000) : 
    { 
        *(.data) /* Data section */
    }
    .bss ALIGN (0x1000) : 
    { 
        *(COMMON)  /* Common section */
        *(.bss)  /* bss section */
    }
}