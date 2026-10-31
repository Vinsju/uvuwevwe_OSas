#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"

#define KERNEL_VIRTUAL_BASE 0xC0000000u

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

/**
 * Global page frame manager state tracking free/busy frames.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = false
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

void paging_init(void) {
    struct PageDirectoryEntry *recursive = &_paging_kernel_page_directory.table[RECURSIVE_PAGE_DIRECTORY_INDEX];
    uint32_t page_dir_phys = (uint32_t) &_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE;

    memset(recursive, 0, sizeof(*recursive));
    recursive->flag.present_bit = true;
    recursive->flag.write_bit = true;
    recursive->page_table_addr = page_dir_phys >> 12;

    // Tulis ulang CR3 = flush seluruh TLB supaya mapping baru berlaku
    __asm__ volatile (
        "mov %%cr3, %%eax\n\t"
        "mov %%eax, %%cr3"
        : : : "eax", "memory"
    );
}

bool paging_get_free_frame(uint32_t *frame_number) {
    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT; page_frame_number < PAGE_FRAME_MAX_COUNT; page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;
            *frame_number = page_frame_number;
            return true;
        }
    }
    return swap_evict_one(frame_number);
}

struct PageTable *paging_get_page_table(
    struct PageDirectory *page_dir,
    uint32_t page_directory_index
) {
    if (
        page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX ||
        page_dir != paging_get_current_page_directory_addr()
    ) {
        return NULL;
    }

    struct PageDirectoryEntry *dir_entry =
        &page_dir->table[page_directory_index];

    if (dir_entry->flag.present_bit) {
        return (struct PageTable *)(
            RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS
            + page_directory_index * PAGE_FRAME_SIZE
        );
    }

    uint32_t page_frame_num;
    if (!paging_get_free_frame(&page_frame_num)) {
        return NULL;
    }

    memset(dir_entry, 0, sizeof(*dir_entry));

    dir_entry->flag.present_bit = true;
    dir_entry->flag.write_bit = true;
    dir_entry->flag.user_access_bit = true;
    dir_entry->page_table_addr = page_frame_num;

    /* Page table baru berisi sisa data frame lama: kosongkan lewat recursive mapping */
    struct PageTable *new_table = (struct PageTable *)(
        RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS
        + page_directory_index * PAGE_FRAME_SIZE
    );
    flush_single_tlb(new_table);
    memset(new_table, 0, sizeof(struct PageTable));

    return new_table;
}

struct PageTableEntry *paging_get_page_table_entry(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index = ((uint32_t) virtual_addr >> 12) & 0x3FF;

    if (page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX || page_dir != paging_get_current_page_directory_addr() || !page_dir->table[page_directory_index].flag.present_bit) {
        return NULL;
    }

    struct PageTable *page_table = (struct PageTable *)(RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
    return &page_table->table[page_table_index];
}

struct PageDirectory *paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;

    __asm__ volatile(
        "mov %%cr3, %0"
        : "=r"(current_page_directory_phys_addr)
    );

    uint32_t virtual_addr_page_dir =
        current_page_directory_phys_addr + 0xC0000000;

    return (struct PageDirectory *)virtual_addr_page_dir;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t)virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index = ((uint32_t)virtual_addr >> 12) & 0x3FF;

    struct PageTable *page_table =
        paging_get_page_table(page_dir, page_directory_index);

    if (page_table == NULL) return;

    memset(&page_table->table[page_table_index], 0, sizeof(struct PageTableEntry));

    page_table->table[page_table_index].flag = flag;

    page_table->table[page_table_index].frame_addr =
    ((uint32_t) physical_addr >> 12) & 0xFFFFF;

    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    asm volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr): "memory");
}

bool paging_allocate_check(uint32_t amount) {
    uint32_t required_page_count =
        (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;

    return page_manager_state.free_page_frame_count + swap_free_slot_count() >= required_page_count;
}


bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_dir_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    if (paging_get_page_table(page_dir, page_dir_index) == NULL) {
        return false;
    }

    uint32_t page_frame_number;
    if (!paging_get_free_frame(&page_frame_number)) {
        return false;
    }

    struct PageTableEntryFlag flag = {
        .present_bit = true,
        .write_bit = true,
        .user_access_bit = true,
    };

    update_page_directory_entry(page_dir, (void *)(page_frame_number * PAGE_FRAME_SIZE), virtual_addr, flag);

    memset((void *)((uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1)), 0, PAGE_FRAME_SIZE);

    swap_track_page(virtual_addr);
    return true;
}

bool paging_allocate_user_region(struct PageDirectory *page_dir, void *start, uint32_t page_count) {
    for (uint32_t i = 0; i < page_count; i++) {
        void *addr = (void *)((uint32_t) start + i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(page_dir, addr)) {
            return false;
        }
    }
    return true;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {


    struct PageTableEntry *page_entry =
        paging_get_page_table_entry(page_dir, virtual_addr);

    if (page_entry == NULL) {
        return false;
    }


    if (!page_entry->flag.present_bit) {
        if (page_entry->ignored & PTE_AVAILABLE_SWAPPED) {
            swap_free_slot(page_entry->frame_addr);
            memset(page_entry, 0, sizeof(struct PageTableEntry));
            return true;
        }
        return false;
    }

    uint32_t page_frame_number = page_entry->frame_addr;

    if (
        page_frame_number < KERNEL_RESERVED_PAGE_FRAME_COUNT ||
        page_frame_number >= PAGE_FRAME_MAX_COUNT
    ) {
        return false;
    }

    page_manager_state.page_frame_map[page_frame_number] = false;
    page_manager_state.free_page_frame_count++;

    memset(page_entry, 0, sizeof(struct PageTableEntry));

    flush_single_tlb(virtual_addr);

    return true;
}
_Static_assert(sizeof(struct PageDirectoryEntry) == 4, "PageDirectoryEntry harus 4 byte");
_Static_assert(sizeof(struct PageTableEntry) == 4, "PageTableEntry harus 4 byte");
