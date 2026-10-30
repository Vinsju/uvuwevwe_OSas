#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/** 
 * Note: MB often referring to MiB in context of memory management
 * change this to be a small number to test if swap working properly
 * kernel will be tested with a small number of SYSTEM_MEMORY_MB
 */
 #define SYSTEM_MEMORY_MB     128 

// Number of entries in a page directory
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u

// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define PAGE_FRAME_MAX_COUNT (0x7FE0000u / PAGE_FRAME_SIZE)

// kernel's  page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Page Directory Entry Flag, only first 8 bit
 * @param present_bit        Indicate whether this entry is exist or not.
 * @param write_bit          Indicate whether this page is writable
 * ... 
 * @param use_pagesize_4_mb Indicate whether this entry use 4 MB page size (set false) 
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 */
struct PageDirectoryEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t user_access_bit    : 1;
    uint32_t write_through      : 1;
    uint32_t cache_disable      : 1;
    uint32_t accessed           : 1;
    uint32_t dirty              : 1;
    uint32_t use_pagesize_4_mb  : 1;
} __attribute__((packed));

/**
 * Page Directory Entry Flag, only first 8 bit
 * @param present_bit        Indicate whether this entry is exist or not.
 * @param write_bit          Indicate whether this page is writable
 * ... 
 * @param use_pagesize_4_mb Indicate whether this entry use 4 MB page size (set false) 
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 */
struct PageTableEntryFlag {
    uint32_t present_bit     : 1;
    uint32_t write_bit       : 1;
    uint32_t user_access_bit : 1;
    uint32_t write_through   : 1;
    uint32_t cache_disable   : 1;
    uint32_t accessed        : 1;
    uint32_t dirty           : 1;
    uint32_t page_attr_table : 1;
} __attribute__((packed));

/**
 * Page Directory Entry, untuk page table berukuran 4 KiB (CR4.PSE = 0).
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 / Table 4-6 (PDE that references a page table)
 *
 * @param flag            Bit 0-7: flag page directory entry (bit 7 PS harus 0)
 * @param global_page     Bit 8: diabaikan untuk PDE 4 KiB
 * @param ignored         Bit 9-11: bebas dipakai OS
 * @param page_table_addr Bit 12-31: alamat fisik page table >> 12 (nomor frame page table)
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint32_t global_page     : 1;
    uint32_t ignored         : 3;
    uint32_t page_table_addr : 20;
} __attribute__((packed));

#define PTE_AVAILABLE_SWAPPED 0x1u

/**
 * Page Table Entry, untuk page berukuran 4 KiB.
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 / Table 4-6 (PTE that maps a 4-KByte page)
 *
 * @param flag        Bit 0-7: flag page table entry
 * @param global_page Bit 8: G flag
 * @param ignored     Bit 9-11: bebas dipakai OS
 * @param frame_addr  Bit 12-31: alamat fisik frame >> 12 (nomor page frame)
 */
struct PageTableEntry {
    struct PageTableEntryFlag flag;
    uint32_t global_page : 1;
    uint32_t ignored     : 3;
    uint32_t frame_addr  : 20;
} __attribute__((packed));
/**
 * Page Directory, contain array of PageDirectoryEntry.
 * Note: This data structure is volatile (can be modified from outside this code, check "C volatile keyword").
 * MMU operation, TLB hit & miss also affecting this data structure (dirty, accessed bit, etc).
 * 
 * Warning: Address must be aligned in 4 KB (listed on Intel Manual), use __attribute__((aligned(0x1000))),
 * unaligned definition of PageDirectory will cause triple fault
 *
 * @param table Fixed-width array of PageDirectoryEntry with size PAGE_ENTRY_COUNT
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed, aligned(0x1000)));

/**
 *  Page Table, contain array of PageTableEntry
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed, aligned(0x1000)));
/**
 * Containing page manager states.
 * 
 * @param page_frame_map Keeping track empty space. True when the page frame is currently used
 * ...
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
    // TODO: Add if needed ...
} __attribute__((packed));





/**
 * Edit page directory with respective parameter
 * 
 * @param page_dir      Page directory to update
 * @param physical_addr Physical address to map
 * @param virtual_addr  Virtual address to map
 * @param flag          Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageTableEntryFlag flag
);

/**
 * Get the requested page table from respective parameter
 * @param page_dir      Page directory to read
 * @param page_directory_index page table's position/index on page directory 
 * note: sudah diberikan implementasinya pada paging.c dengan pendekatan fractal memory maping alias recursive memory mapping.
 * read: https://wiki.osdev.org/Fractal_Page_Mapping
 */
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);

struct PageDirectory *paging_get_current_page_directory_addr(void);

/**
 * Invalidate page that contain virtual address in parameter
 * 
 * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);





/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 * 
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 * 
 * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             Physical address of allocated frame
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate single user page frame in page directory
 * 
 * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be allocated
 * @return              Will return true if success, false otherwise
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

void paging_init(void);

bool paging_get_free_frame(uint32_t *frame_number);

struct PageTableEntry *paging_get_page_table_entry(struct PageDirectory *page_dir, void *virtual_addr);


#endif