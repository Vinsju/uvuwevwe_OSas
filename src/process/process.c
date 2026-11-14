#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

// DATA GLOBAL
struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

static struct ProcessManagerState process_manager_state = {
    .active_process_count = 0,
    .next_pid             = 0
};

// HELPER

// Index slot PCB pertama yang kosong, atau -1 kalau penuh
static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_TERMINATED)
            return i;
    }
    return -1;
}

// Buat PID baru yang selalu naik
static uint32_t process_generate_new_pid(void) {
    return process_manager_state.next_pid++;
}

// Cari PCB aktif berdasarkan PID (NULL jika tak ada)
static struct ProcessControlBlock *process_find_by_pid(uint32_t pid) {
    for (int32_t i=0; i<PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_STATE_TERMINATED && pcb->metadata.pid == pid) {
            return pcb;
        }
    }

    return NULL;
}

// Matikan interrupt sementara untuk pergantian CR3
static inline uint32_t process_interrupt_save_and_disable(void) {
    uint32_t eflags;
    __asm__ volatile("pushf\n\tpop %0\n\tcli" : "=r"(eflags) : : "memory");
    return eflags;
}

static inline void process_interrupt_restore(uint32_t eflags) {
    __asm__ volatile("push %0\n\tpopf" : : "r"(eflags) : "memory", "cc");
}

// Alokasi 1 halaman yang sedang aktif & catat di PCB
static bool process_allocate_page(struct ProcessControlBlock *pcb, struct PageDirectory *page_dir, uint32_t virtual_adr) {
    if (pcb->memory.page_frame_used_count >= PROCESS_PAGE_FRAME_COUNT_MAX) return false;
    if (!paging_allocate_user_page_frame(page_dir, (void *) virtual_adr)) return false;

    pcb->memory.virtual_addr_used[pcb->memory.page_frame_used_count++] = (void *) virtual_adr;
    return true;
}

// API PUBLIK

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_RUNNING)
            return &_process_list[i];
    }
    return NULL;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;
    uint32_t eflags = process_interrupt_save_and_disable();
    struct PageDirectory *old_page_dir = paging_get_current_page_directory_addr();
    struct PageDirectory *new_page_dir = NULL;
    struct ProcessControlBlock *new_pcb = NULL;


    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    if (((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) || (((uint32_t) request.buf & (PAGE_FRAME_SIZE-1)) != 0)) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    char name[PROCESS_NAME_LENGTH_MAX];
    if (request.name == NULL || request.name_len == 0 || request.name_len >= PROCESS_NAME_LENGTH_MAX) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }
    memcpy(name, request.name, request.name_len);
    name[request.name_len] = '\0';
    request.name = name;

    //Ukuran file sebenarnya
    uint32_t file_size = 0;
    if (get_file_size(request, &file_size) != 0 || file_size == 0 || file_size > request.buffer_size) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    //Halaman kode/data
    uint32_t code_page_count = ceil_div(file_size, PAGE_FRAME_SIZE);
    uint32_t page_frame_count_needed = code_page_count+ 1 ;

    // Check whether memory is enough for the executable and additional frame for user stack
    if (!paging_allocate_check(page_frame_count_needed * PAGE_FRAME_SIZE) || page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Process PCB 
    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    new_pcb = &(_process_list[p_index]);
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    new_page_dir = paging_create_new_page_directory();
    if (new_page_dir == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    paging_use_page_directory(new_page_dir);
    for (uint32_t i=0; i<code_page_count; i++) {
        if (!process_allocate_page(new_pcb, new_page_dir, (uint32_t) request.buf + i*PAGE_FRAME_SIZE)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
    }

    if (!process_allocate_page(new_pcb, new_page_dir, PROCESS_USER_STACK_PAGE)) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    //Kalau ada halaman baru ter-swap, masukkan dulu sebelum ATA menulis ke buffer
    swap_prefault(request.buf, file_size);
    
    if (read(request) != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // CONTEXT AWAL
    struct Context *ctx = &new_pcb->context;
    ctx->cpu.segment.ds = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    ctx->cpu.segment.es = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    ctx->cpu.segment.fs = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    ctx->cpu.segment.gs = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    ctx->cpu.stack.esp = PROCESS_USER_STACK_TOP;
    ctx->cpu.stack.ebp = PROCESS_USER_STACK_TOP;
    ctx->eip = (uint32_t) request.buf;
    ctx->eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    ctx->page_directory_virtual_addr = new_page_dir;

    // METADATA
    new_pcb->metadata.pid = process_generate_new_pid();
    memcpy(new_pcb->metadata.name, name, request.name_len+1);
    new_pcb->metadata.state = PROCESS_STATE_READY;
    process_manager_state.active_process_count++;

exit_cleanup:
    paging_use_page_directory(old_page_dir);

    // Kembalikan CR3
    if (retcode != PROCESS_CREATE_SUCCESS) {
        if (new_page_dir != NULL) {
            paging_free_page_directory(new_page_dir);
        }
        if (new_pcb != NULL) {
            memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
        }
    }

    process_interrupt_restore(eflags);
    return retcode;
}

bool process_destroy(uint32_t pid) {
    struct ProcessControlBlock *pcb = process_find_by_pid(pid);
    if (pcb == NULL) return false;

    uint32_t eflags = process_interrupt_save_and_disable();

    // Bebaskan memori user process
    paging_free_page_directory(pcb->context.page_directory_virtual_addr);

    memset(pcb, 0, sizeof(struct ProcessControlBlock));
    process_manager_state.active_process_count--;

    process_interrupt_restore(eflags);
    return true;
}

uint32_t process_get_info(struct ProcessInfo *info, uint32_t max_count) {
    uint32_t count = 0;
    for (int32_t i=0; i<PROCESS_COUNT_MAX && count<max_count; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state == PROCESS_STATE_TERMINATED) continue;

        info[count].pid = pcb->metadata.pid;
        memcpy(info[count].name, pcb->metadata.name, PROCESS_INFO_NAME_LENGTH);
        info[count].name[PROCESS_INFO_NAME_LENGTH-1] = '\0';
        info[count].state = (uint8_t) pcb->metadata.state;
        count++;
    }
    return count;
}
