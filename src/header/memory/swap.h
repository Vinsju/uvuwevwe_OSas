#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>

#define SWAP_START_BLOCK 8192u // Block pertama swap space
#define SWAP_SLOT_COUNT 1024u // Jumlah slot page yang muat di swap
#define SWAP_BLOCKS_PER_PAGE 8u 

void swap_track_page(void *virtual_addr);
// Catat page user yang baru masuk RAM sebagai calon swap out

bool swap_evict_one(uint32_t *frame_number);
// Algoritma page replacement

void swap_free_slot(uint32_t slot);
// Kosongkan satu slot swap

uint32_t swap_free_slot_count(void);
// Jumlah slot swap yang masih kosong

void swap_prefault(void *buf, uint32_t size);
// Pastikan semua page sedang ada di RAM

void page_fault_handler(uint32_t fault_address, uint32_t error_code);
// Handler C untuk exception CPU Page Fault

void swap_selftest(uint32_t page_count);
// Tes mandiri: alokasi lebih banyak page daripada RAM tersedia

#endif