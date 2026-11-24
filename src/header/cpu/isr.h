#ifndef _ISR_H
#define _ISR_H

#include <stdint.h>

extern struct TSSEntry _interrupt_tss_entry;

/**
 * TSSEntry, Task State Segment. Used when jumping back to ring 0 / kernel
 */
struct TSSEntry {
    uint32_t prev_tss; // Previous TSS 
    uint32_t esp0;     // Stack pointer to load when changing to kernel mode
    uint32_t ss0;      // Stack segment to load when changing to kernel mode
    // Unused variables
    uint32_t unused_register[23];
} __attribute__((packed));


/* Handler kosong untuk vektor 0x40 - 0xFF. Vektor 0x00 - 0x3F memakai isr_stub_table (header/cpu/interrupt.h) */
void isr_default_handler(void);
void isr_init(void);

void set_tss_kernel_current_stack(void);


#endif