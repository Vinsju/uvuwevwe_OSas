#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/disk.h"
#include "header/text/framebuffer.h"

#define KERNEL_VIRTUAL_BASE 0xC0000000u // alamat di atas ini semua miliki kernel
#define PAGE_MASK (~(PAGE_FRAME_SIZE - 1)) // buang 12 bit bawah

/* PENCATATAN SLOT SWAP */
static bool swap_slot_used[SWAP_SLOT_COUNT]; // True ketika slot ke-i sedang berisi sebuah page
static uint32_t swap_used_count; // Berapa slot yang sedang terisi

/* ============================================================================
 *  ANTREAN FIFO (First In First Out) UNTUK ALGORITMA PENGGANTIAN
 * ============================================================================
 *  Setiap kali halaman user masuk RAM, alamat virtualnya dimasukkan ke ujung
 *  antrean. Saat RAM penuh, halaman di DEPAN antrean (yang paling lama di RAM)
 *  yang dikorbankan ke swap.
 *
 *  Antrean dibuat sebagai "ring buffer" (array melingkar):
 *    fifo_head  = indeks elemen paling depan (paling tua)
 *    fifo_count = jumlah elemen di antrean
 *    elemen ke-k ada di fifo_page[(fifo_head + k) % FIFO_CAPACITY]
 *
 *  Antrean bisa berisi entri "basi" (stale), misalnya halaman yang sudah
 *  di-free atau sudah di swap. Entri basi tidak dihapus di tempat, tapi
 *  dilewati saat diambil (lihat swap_evict_one). Ini lebih sederhana daripada
 *  mencari dan menghapus entri di tengah antrean.
 */

 #define FIFO_CAP PAGE_FRAME_MAX_COUNT
 static uint32_t fifo_page[FIFO_CAP];
 static uint32_t fifo_head; // elemen paling depan
 static uint32_t fifo_count; //jumlah elemen di antrean

 // Masukkan alamat ke belakang antrean
 static void fifo_push(uint32_t virtual_address) {
    if (fifo_count == FIFO_CAP) {
        fifo_head = (fifo_head + 1) & FIFO_CAP;
        fifo_count--;
    }
    fifo_page[(fifo_head + fifo_count) % FIFO_CAP] = virtual_address;
    fifo_count++;
 }

// Ambil alamat dari belakang antrean
static bool fifo_pop(uint32_t *virtual_address) {
    if (fifo_count == 0) return false;
    *virtual_address = fifo_page[fifo_head];
    fifo_head = (fifo_head + 1) % FIFO_CAP;
    fifo_count--;
    return true;
}

// Cari slot kosong pertama
static bool swap_alloc_slot(uint32_t *slot) {
    for (uint32_t i=0; i<SWAP_SLOT_COUNT; i++) {
        if (!swap_slot_used[i]) {
            swap_slot_used[i] = true;
            swap_used_count++;
            *slot = i;
            return true;
        }
    }
    return false;
}

// Mem-free kan sejumlah slot yang sedang used
void swap_free_slot(uint32_t slot) {
    if (slot < SWAP_SLOT_COUNT && swap_slot_used[slot]) {
        swap_slot_used[slot] = false;
        swap_used_count--;
    }
}

// Menghitung jumlah free slot
uint32_t swap_free_slot_count() {
    return SWAP_SLOT_COUNT - swap_used_count;
}

// Menghitung alamat block dari sebuah slot
static uint32_t slot_to_block(uint32_t slot) {
    return SWAP_START_BLOCK + slot * SWAP_BLOCKS_PER_PAGE;
}

void swap_track_page(void *virtual_address) {
    fifo_push((uint32_t) virtual_address & PAGE_MASK);
}

/* SWAP OUT: RAM -> DISK 
 * Ambil halaman tertua dari anteran FIFO
 * Cari slot kosong di swap
 * Tulis isi halaman ke slot itu di disk
 * Ubah PTE: present = 0, tandai Swapped, simpan nomor slot
 * Flush TLB untuk alamat itu
 * Frame fisik sekarang bebas & dikembalikan
*/
bool swap_evict_one(uint32_t *frame_number) {
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    uint32_t virtual_address;

    while (fifo_pop(&virtual_address)) {
        struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, (void *) virtual_address);

        if (entry == NULL || !entry->flag.present_bit) continue; // Entri basi

        uint32_t slot;
        if (!swap_alloc_slot(&slot)) { // Swap penuh
            fifo_push(virtual_address);
            return false;
        }

        write_blocks((void *) virtual_address, slot_to_block(slot), SWAP_BLOCKS_PER_PAGE);

        // Simpan nomor framse sebelum PTE dikosongkan
        uint32_t frame = entry->frame_addr;
        memset(entry, 0, sizeof(*entry));
        entry->ignored = PTE_AVAILABLE_SWAPPED;
        entry->frame_addr = slot;

        flush_single_tlb((void *) virtual_address);

        *frame_number = frame;
        return true;
    }
    return false;
}


/* SWAP IN: DISK -> RAM
*/
static bool swap_in(struct PageTableEntry *entry, uint32_t virtual_addr) {
    uint32_t slot = entry->frame_addr;

    uint32_t frame;
    if (!paging_get_free_frame(&frame)) {
        return false;
    }

    memset(entry, 0, sizeof(*entry));
    entry->flag.present_bit = 1;
    entry->flag.write_bit = 1;
    entry->flag.user_access_bit = 1;
    entry->frame_addr = frame;
    flush_single_tlb((void *) virtual_addr);

    read_blocks((void *) virtual_addr, slot_to_block(slot), SWAP_BLOCKS_PER_PAGE);

    swap_free_slot(slot);
    swap_track_page((void *) virtual_addr);
    return true;
}

void swap_prefault(void *buf, uint32_t size) {
    if (size == 0) return;
    uint32_t start = (uint32_t) buf & PAGE_MASK;
    uint32_t end = (uint32_t) buf + size - 1;

    for (uint32_t page = start; page <= end && page >= start; page += PAGE_FRAME_SIZE) {
        volatile uint8_t touch = *(volatile uint8_t *) page;
        (void) touch;
    }
}

static void print_text(uint8_t row, uint8_t col, const char *text, uint8_t color) {
    for (uint8_t i=0; text[i] != '\0' && col+i < 80; i++) {
        framebuffer_write(row, col+i, text[i], color, 0x0);
    }
}

static void print_hex(uint8_t row, uint8_t col, uint32_t value, uint8_t color) {
    const char *digits = "0123456789ABCDEF";
    char text[11] = "0x00000000";
    for (int i=0; i<8; i++) {
        text[9-i] = digits[(value >> (4*i)) & 0xF];
    }
    print_text(row, col, text, color);
}

void page_fault_handler(uint32_t fault_address, uint32_t error_code) {
    if (!(error_code & 0x1) && fault_address < KERNEL_VIRTUAL_BASE) {
        struct PageTableEntry *entry = paging_get_page_table_entry(paging_get_current_page_directory_addr(), (void *) fault_address);
        if (entry != NULL && !entry->flag.present_bit && (entry->ignored & PTE_AVAILABLE_SWAPPED) && swap_in(entry, fault_address & PAGE_MASK)) {
            return;
        }
    }

    print_text(24, 0, "PAGE FAULT addr=", 0xC);
    print_hex(24, 16, fault_address, 0xC);
    print_text(24, 27, "err=", 0xC);
    print_hex(24, 32, error_code, 0xC);
    __asm__ volatile("cli");
    while(true) __asm__ volatile("hlt");
}

#define SELFTEST_BASE 0x10000000u

static uint32_t selftest_pattern(uint32_t page, uint32_t word) {
    return (page * 0x9E3779B1u) ^ (word * 0x85EBCA6Bu) ^ 0xA5A5A5A5u;
}

void swap_selftest(uint32_t page_count) {
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    uint32_t free_frames_before = page_manager_state.free_page_frame_count;
    uint32_t allocated = 0, bad_pages = 0;

    print_text(23, 0, "SWAP TEST: frames bebas=", 0xF);
    print_hex(23, 24, free_frames_before, 0xF);
    print_text(23, 35, " halaman=", 0xF);
    print_hex(23, 44, page_count, 0xF);

    for (uint32_t p=0; p<page_count; p++) {
        uint32_t *page = (uint32_t *)(SELFTEST_BASE + p * PAGE_FRAME_SIZE);
        
        if (!paging_allocate_user_page_frame(page_dir, page)) break;
        allocated++;
        for (uint32_t w=0; w<PAGE_FRAME_SIZE/4; w++) {
            page[w] = selftest_pattern(p,w);
        }
    }

    for (uint32_t p=0; p<allocated; p++) {
        uint32_t *page = (uint32_t *)(SELFTEST_BASE + p * PAGE_FRAME_SIZE);

        for (uint32_t w=0; w<PAGE_FRAME_SIZE/4; w++) {
            if (page[w] != selftest_pattern(p,w)) {
                bad_pages++;
                break;
            }
        }
    }

    uint32_t swapped = swap_used_count;
    for (uint32_t p=0; p<allocated; p++) {
        paging_free_user_page_frame(page_dir, (void *)(SELFTEST_BASE + p*PAGE_FRAME_SIZE));
    }

    bool ok = allocated == page_count && bad_pages == 0 && swap_used_count == 0;
    print_text(24, 0, ok ? "SWAP TEST PASS " : "SWAP TEST FAIL ", ok ? 0xA : 0xC);
    print_text(24, 15, "alloc=", 0xF);
    print_hex(24, 21, allocated, 0xF);
    print_text(24, 32, " rusak=", 0xF);
    print_hex(24, 39, bad_pages, 0xF);
    print_text(24, 50, " di swap=", 0xF);
    print_hex(24, 59, swapped, 0xF);
}