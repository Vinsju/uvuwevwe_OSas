#include "header/cpu/interrupt.h"
#include "header/cpu/pic.h"
#include "header/cpu/keyboard.h"
#include "header/syscall/syscall.h"
#include "header/memory/swap.h"

/* Didefinisikan di kernel-entrypoint.s: kembali ke kernel_setup() setelah SYSCALL_EXIT */
extern void kernel_return_from_user(void) __attribute__((noreturn));

static inline uint32_t read_cr2(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void irq_handler(uint32_t irq) {
    switch (irq) {
    case IRQ_KEYBOARD:
        keyboard_handler(); // keyboard_handler() sudah mengirim EOI sendiri
        break;

    case IRQ_TIMER:
        // Bagian 4.2 (Scheduler): context saving & context switch dilakukan di sini.
        // Untuk sekarang cukup ACK supaya interrupt lain tidak terblokir.
        pic_send_eoi(IRQ_TIMER);
        break;

    default:
        pic_send_eoi((uint8_t) irq);
        break;
    }
}

static void syscall_dispatch(struct InterruptFrame *frame) {
    uint32_t result = syscall_handler(
        frame->cpu.general.eax,
        frame->cpu.general.ebx,
        frame->cpu.general.ecx,
        frame->cpu.general.edx
    );

    // syscall_handler() mengembalikan 1 untuk SYSCALL_EXIT (perilaku sama dengan isr.s lama)
    if (result == 1) {
        kernel_return_from_user();
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    uint32_t int_number = frame.int_number;

    if (int_number == INT_PAGE_FAULT) {
        page_fault_handler(read_cr2(), frame.int_stack.error_code);
    } else if (int_number == INT_SYSCALL) {
        syscall_dispatch(&frame);
    } else if (int_number >= PIC1_OFFSET && int_number < PIC2_OFFSET + 8) {
        irq_handler(int_number - PIC1_OFFSET);
    }
    // Exception CPU lain belum ditangani, perilaku sama dengan isr_default_handler lama
}
