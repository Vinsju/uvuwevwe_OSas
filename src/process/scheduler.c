#include "header/process/scheduler.h"
#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"

void scheduler_init(void) {
    activate_timer_interrupt();

}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current == NULL) return;  // IRQ0 bisa masuk sebelum switch pertama
    current->context = ctx;
}

// Round Robin: cari process READY/RUNNING berikutnya setelah current_idx
static int32_t scheduler_pick_next_round_robin(int32_t current_idx) {
    for (int32_t i = 1; i <= PROCESS_COUNT_MAX; i++) {
        int32_t idx = (current_idx + i) % PROCESS_COUNT_MAX;
        PROCESS_STATE state = _process_list[idx].metadata.state;
        if (state == PROCESS_STATE_READY || state == PROCESS_STATE_RUNNING)
            return idx;
    }
    return -1;
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    int32_t current_idx = (current == NULL) ? -1 : (int32_t) (current - _process_list);

    int32_t next = scheduler_pick_next_round_robin(current_idx);
    if (next < 0) {
        for (;;) __asm__ volatile("hlt");  // tidak ada process yang bisa jalan
    }

    if (current != NULL && current->metadata.state == PROCESS_STATE_RUNNING)
        current->metadata.state = PROCESS_STATE_READY;
    _process_list[next].metadata.state = PROCESS_STATE_RUNNING;

    paging_use_page_directory(_process_list[next].context.page_directory_virtual_addr);
    process_context_switch(_process_list[next].context);
}