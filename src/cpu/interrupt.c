#include "header/cpu/interrupt.h"
#include "header/cpu/pic.h"
#include "header/cpu/keyboard.h"
#include "header/syscall/syscall.h"
#include "header/memory/swap.h"
#include "header/cpu/portio.h"
#include "header/process/scheduler.h"

/* Didefinisikan di kernel-entrypoint.s: kembali ke kernel_setup() setelah SYSCALL_EXIT */
extern void kernel_return_from_user(void) __attribute__((noreturn));

static inline uint32_t read_cr2(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void irq_handler(uint32_t irq, struct InterruptFrame *frame) {
    switch (irq) {
    case IRQ_KEYBOARD:
        keyboard_handler(); // keyboard_handler() sudah mengirim EOI sendiri
        break;

    case IRQ_TIMER:
        // Bagian 4.2 (Scheduler): context saving & context switch dilakukan di sini.
        // Untuk sekarang cukup ACK supaya interrupt lain tidak terblokir.
        pic_send_eoi(IRQ_TIMER);
        if ((frame->int_stack.cs & 0x3) == 0x3) {
            struct Context ctx = {
                .cpu    = frame->cpu,
                .eip    = frame->int_stack.eip,
                .eflags = frame->int_stack.eflags,
                .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
            };
            scheduler_save_context_to_current_running_pcb(ctx);
            scheduler_switch_to_next_process();
        }
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
    if (result == SYSCALL_RESULT_RETURN_TO_KERNEL) {
        kernel_return_from_user();
    }
    else if (result == SYSCALL_RESULT_SWITCH_PROCESS) {
        scheduler_switch_to_next_process();
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    uint32_t int_number = frame.int_number;

    if (int_number == INT_PAGE_FAULT) {
        page_fault_handler(read_cr2(), frame.int_stack.error_code);
    } else if (int_number == INT_SYSCALL) {
        syscall_dispatch(&frame);
    } else if (int_number >= PIC1_OFFSET && int_number < PIC2_OFFSET + 8) {
        irq_handler(int_number - PIC1_OFFSET, &frame);
    }
    // Exception CPU lain belum ditangani, perilaku sama dengan isr_default_handler lama
}


void activate_timer_interrupt(void) {
    __asm__ volatile("cli");

    // Atur seberapa sering PIT menembakkan IRQ0
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    // Unmask IRQ0 di PIC master
    pic_unmask_irq(IRQ_TIMER);
}
