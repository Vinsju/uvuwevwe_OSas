#include "header/cpu/idt.h"
#include "header/cpu/isr.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"

#define IDT_GATE_KERNEL 0x8E // present, DPL 0, 32-bit interrupt gate
#define IDT_GATE_USER   0xEE // present, DPL 3, 32-bit interrupt gate (boleh dipanggil dari ring 3)

static struct IDTGate idt[IDT_MAX_ENTRY_COUNT];
static struct IDTR idtr;

static void idt_set_gate(uint8_t index, void *handler, uint8_t type_attr) {
    uint32_t addr = (uint32_t) handler;
    idt[index].offset_low  = addr & 0xFFFF;
    idt[index].selector    = GDT_KERNEL_CODE_SEGMENT_SELECTOR;
    idt[index].reserved    = 0;
    idt[index].type_attr   = type_attr;
    idt[index].offset_high = (addr >> 16) & 0xFFFF;
}

void idt_init(void) {
    // 0x00 - 0x3F: stub dari cpu/intsetup.s -> main_interrupt_handler()
    for (int i = 0; i < IDT_MAX_ENTRY_COUNT; i++) {
        if (i < ISR_STUB_TABLE_LIMIT) {
            idt_set_gate(i, isr_stub_table[i], IDT_GATE_KERNEL);
        } else {
            idt_set_gate(i, (void *) isr_default_handler, IDT_GATE_KERNEL);
        }
    }

    // System call: interrupt 0x30 harus DPL 3 agar bisa dipanggil dari user mode
    idt_set_gate(INT_SYSCALL, isr_stub_table[INT_SYSCALL], IDT_GATE_USER);

    idtr.size = sizeof(idt) - 1;
    idtr.address = idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
