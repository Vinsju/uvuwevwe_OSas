#include "header/cpu/idt.h"
#include "header/cpu/isr.h"

static struct IDTGate idt[IDT_MAX_ENTRY_COUNT];
static struct IDTR idtr;

void idt_init(void) {
    for (int i = 0; i < IDT_MAX_ENTRY_COUNT; i++) {
        idt[i].offset_low = (uint16_t)((uint32_t)isr_default_handler & 0xFFFF);
        idt[i].selector = 0x08;
        idt[i].reserved = 0;
        idt[i].type_attr = 0x8E;
        idt[i].offset_high = (uint16_t)(((uint32_t)isr_default_handler >> 16) & 0xFFFF);
    }

    uint32_t timer_handler = (uint32_t)isr_timer_handler;

    idt[32].offset_low = timer_handler & 0xFFFF;
    idt[32].offset_high = (timer_handler >> 16) & 0xFFFF;

    uint32_t keyboard_handler = (uint32_t)isr_keyboard_handler;

    idt[33].offset_low = keyboard_handler & 0xFFFF;
    idt[33].offset_high = (keyboard_handler >> 16) & 0xFFFF;

    idtr.size = sizeof(idt) - 1;
    idtr.address = idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}