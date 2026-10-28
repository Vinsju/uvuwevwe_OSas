#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/memory/paging.h"

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
    for (
        uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
        page_frame_number < PAGE_FRAME_MAX_COUNT;
        page_frame_number++
    ) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;

            dir_entry->flag.present_bit = true;
            dir_entry->flag.write_bit = true;
            dir_entry->flag.user_access_bit = true;

            dir_entry->lower_address =
                page_frame_number & 0x3FF;

            dir_entry->higher_address =
                (page_frame_number >> 10) & 0xFF;

            return (struct PageTable *)(
                RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS
                + page_directory_index * PAGE_FRAME_SIZE
            );
        }
    }

    return NULL;
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

    page_table->table[page_table_index].flag = flag;

    page_table->table[page_table_index].lower_address =
    ((uint32_t) physical_addr >> 12) & 0x3FF;

    page_table->table[page_table_index].higher_address =
    ((uint32_t) physical_addr >> 22) & 0xFF;

    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    asm volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr): "memory");
}

bool paging_allocate_check(uint32_t amount) {
    uint32_t required_page_count =
        (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;

    return page_manager_state.free_page_frame_count >= required_page_count;
}


bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    for (
        uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
        page_frame_number < PAGE_FRAME_MAX_COUNT;
        page_frame_number++
    ) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;

            struct PageTableEntryFlag flag = {
                .present_bit = true,
                .write_bit = true,
                .user_access_bit = true,
            };

            update_page_directory_entry(
                page_dir,
                (void *)(page_frame_number * PAGE_FRAME_SIZE),
                virtual_addr,
                flag
            );

            return true;
        }
    }

    return false;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index =
        ((uint32_t) virtual_addr >> 22) & 0x3FF;

    uint32_t page_table_index =
        ((uint32_t) virtual_addr >> 12) & 0x3FF;

    struct PageTable *page_table =
        paging_get_page_table(page_dir, page_directory_index);

    if (page_table == NULL) {
        return false;
    }

    struct PageTableEntry *page_entry =
        &page_table->table[page_table_index];

    if (!page_entry->flag.present_bit) {
        return false;
    }

    uint32_t page_frame_number =
        page_entry->lower_address |
        (page_entry->higher_address << 10);

    if (
        page_frame_number < KERNEL_RESERVED_PAGE_FRAME_COUNT ||
        page_frame_number >= PAGE_FRAME_MAX_COUNT
    ) {
        return false;
    }

    page_manager_state.page_frame_map[page_frame_number] = false;
    page_manager_state.free_page_frame_count++;

    page_entry->flag.present_bit = false;

    flush_single_tlb(virtual_addr);

    return true;
}