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
        struct PageTableEntry *entry = paging_get_page_table(page_dir, (void *) virtual_address);

        if (entry == NULL || !entry->flag.present_bit) continue; // Entri basi

        uint32_t slot;
        if (!swap_alloc_slot(&slot)) { // Swap penuh
            fifo_push(virtual_address);
            return false;
        }

        write_blocks((void *) virtual_address, slot_to_block(slot), SWAP_BLOCKS_PER_PAGE);

        // Simpan nomor framse sebelum PTE dikosongkan
        
    }
}
