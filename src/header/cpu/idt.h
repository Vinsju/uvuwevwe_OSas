#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

#define IDT_MAX_ENTRY_COUNT 256

struct IDTGate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  reserved;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct IDTR {
    uint16_t size;
    struct IDTGate *address;
} __attribute__((packed));

void idt_init(void);
void isr_default_handler(void);

#endif