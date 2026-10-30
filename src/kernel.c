#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/cpu/pic.h"
#include "header/cpu/idt.h"
#include "header/cpu/isr.h"
#include "header/text/framebuffer.h"
#include "header/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"


void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    paging_init();
    uint32_t a;
    uint32_t volatile b = 0x0000BABE;
   __asm__("mov $0xCAFE0000, %0" : "=r"(a));
    pic_remap();
    pic_unmask_irq(1);
    idt_init();
    isr_init();

    framebuffer_clear();
    framebuffer_write(0, 0, 'A', 0x0F, 0x00);

    initialize_filesystem_ext2();

    __asm__ volatile ("sti");

    framebuffer_set_cursor(3, 12);
    while (true) b += 1;
}