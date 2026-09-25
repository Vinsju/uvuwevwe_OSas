#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/cpu/pic.h"
#include "header/cpu/idt.h"
#include "header/cpu/isr.h"
#include "header/text/framebuffer.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    uint32_t a;
    uint32_t volatile b = 0x0000BABE;
   __asm__("mov $0xCAFE0000, %0" : "=r"(a));
    pic_remap();
    pic_unmask_irq(1);
    idt_init();
    isr_init();

    framebuffer_clear();

    __asm__ volatile ("sti");

    framebuffer_set_cursor(3, 12);
    while (true) b += 1;
}