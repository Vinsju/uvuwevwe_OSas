ENTRY(loader)

KERNEL_VIRTUAL_BASE = 0xC0000000;
KERNEL_PHYSICAL_BASE = 0x00100000;

SECTIONS
{
    . = KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE;

    .multiboot ALIGN(0x1000) : AT(ADDR(.multiboot) - KERNEL_VIRTUAL_BASE)
    {
        *(.multiboot)
    }

    .text ALIGN(0x1000) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
    {
        *(.setup.text)
        *(.text)
    }

    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
    {
        *(.rodata*)
    }

    .data ALIGN(0x1000) : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE)
    {
        *(.data)
    }

    .bss ALIGN(0x1000) : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE)
    {
        *(COMMON)
        *(.bss)
    }

    /* Kernel page table hanya memetakan 4 MiB fisik pertama (KERNEL_RESERVED_PAGE_FRAME_COUNT),
       termasuk page_directory_list milik Multi Virtual Address Space */
    _linker_kernel_end = .;
    ASSERT ((_linker_kernel_end <= KERNEL_VIRTUAL_BASE + 0x400000), "Error: Kernel lebih dari 4 MiB fisik, tidak termapping kernel page table")
}