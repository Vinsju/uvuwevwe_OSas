#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/pic.h"
#include "header/kernel-entrypoint.h"
#include "header/cpu/idt.h"
#include "header/cpu/isr.h"

void kernel_setup(void) {
   uint32_t a;
    uint32_t volatile b = 0x0000BABE;
   __asm__("mov $0xCAFE0000, %0" : "=r"(a));
    pic_remap();
    idt_init();
    isr_init();
    __asm__ volatile ("sti");
    while (true) b += 1;
}