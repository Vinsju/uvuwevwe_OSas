#include "header/cpu/isr.h"
#include "header/cpu/gdt.h"

extern uint8_t ring3_interrupt_stack_top[];

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    _interrupt_tss_entry.esp0 = (uint32_t) ring3_interrupt_stack_top;
}

void isr_init(void) {
}