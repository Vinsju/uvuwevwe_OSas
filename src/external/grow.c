#include <stdint.h>
#include <stdbool.h>
#include "header/syscall/syscall.h"
#include "header/cpu/cmos.h"

// grow : program yang membesarkan dirinya sendiri saat berjalan
// Alur: sbrl() per halaman -> tulis pola ke setiap halaman -> verifikasi -> tahan -> sbrk(-total)

#define PAGE_SIZE 4096u

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static uint32_t sbrk(int32_t increment) {
    uint32_t old_break = SYSCALL_SBRK_FAIL;
    syscall(SYSCALL_SBRK, (uint32_t)increment, (uint32_t)&old_break, 0);
    return old_break;
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static void put(const char *s, uint8_t color) {
    syscall(SYSCALL_PUTS, (uint32_t)s, str_len(s), color);
}

static void put_uint(uint32_t v, uint8_t color) {
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + v%10);
        v /= 10;
    } while (v>0);
    put(&buf[i], color);
}

static void put_hex(uint32_t v, uint8_t color) {
    char buf[11] = "0x";
    for (int i=0; i<8; i++) {
        uint32_t d = (v >> (28 - 4 * i)) & 0xF;
        buf[2+i] = (char)(d<10 ? '0' + d : 'A' + d - 10);
    }
    buf[10] = '\0';
    put(buf, color);
}

static uint32_t parse_uint(const char *s, uint32_t fallback) {
    uint32_t v=0;
    if (s == 0 || *s == '\0') return fallback;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return fallback;
        v = v*10 + (uint32_t)(*s - '0');
    }
    return v;
}

static uint32_t pattern(uint32_t page, uint32_t word) {
    return (page * 0x9E3779B1u) ^ (word * 0x85EBCA6Bu) ^ 0x5A5A5A5Au;
}

static void wait_seconds(uint32_t seconds) {
    struct CMOSTime now;
    syscall(SYSCALL_GET_TIME, (uint32_t)&now, 0,0);
    uint8_t last = now.second;
    while (seconds > 0) {
        for (volatile uint32_t i=0; i<20000; i++);
        syscall(SYSCALL_GET_TIME, (uint32_t)&now, 0, 0);
        if (now.second != last) {
            last = now.second;
            seconds--;
        }
    }
}

int main(int argc, char *argv[]) {
    uint32_t kib = parse_uint(argc > 1 ? argv[1] : 0, 1024);
    uint32_t hold = parse_uint(argc > 2 ? argv[2] : 0, 0);
    uint32_t target_pages = (kib * 1024 + PAGE_SIZE - 1) / PAGE_SIZE;

    uint32_t heap_start = sbrk(0);
    put("grow: heap mulai di ", 0x0B);
    put_hex(heap_start, 0x0F);
    put(", target ", 0x0B);
    put_uint(target_pages, 0x0F);
    put(" halaman\n", 0x0B);

    // grow page
    uint32_t pages = 0;
    while (pages < target_pages) {
        uint32_t page_addr = sbrk((int32_t) PAGE_SIZE);
        if (page_addr == SYSCALL_SBRK_FAIL) {
            put("grow: sbrk gagal (memori + swap habis) di halaman ", 0x0C);
            put_uint(pages, 0x0C);
            put("\n", 0x0C);
            break;
        }
        uint32_t *page = (uint32_t *)page_addr;
        for (uint32_t w=0; w<PAGE_SIZE/4; w++) {
            page[w] = pattern(pages,w);
        }
        pages++;
    }

    // Verif halaman
    uint32_t bad=0;
    for (uint32_t p=0; p<pages; p++) {
        uint32_t *page = (uint32_t *)(heap_start + p * PAGE_SIZE);
        for (uint32_t w=0; w<PAGE_SIZE/4; w++) {
            if (page[w] != pattern(p,w)) {
                bad++;
                break;
            }
        }
    }

    put("grow: heap ", 0x0B);
    put_hex(heap_start, 0x0F);
    put(" - ", 0x0B);
    put_hex(sbrk(0), 0x0F);
    put(" = ", 0x0B);
    put_uint(pages, 0x0F);
    put(" halaman (", 0x0B);
    put_uint(pages * 4, 0x0F);
    put(" KB), rusak: ", 0x0B);
    put_uint(bad, bad == 0 ? 0x0A : 0x0C);
    put(bad == 0 ? "  [OK]\n" : "  [GAGAL]\n", bad == 0 ? 0x0A : 0x0C);

    //Tahan memori
    if (hold > 0) wait_seconds(hold);

    // Kecilkan lagi
    sbrk(-(int32_t)(pages * PAGE_SIZE));
    put("grow: heap dilepas, ukuran heap sekarang ", 0x0B);
    put_uint((sbrk(0) - heap_start) / 1024, 0x0F);
    put(" KB\n", 0x0B);
    return bad == 0 ? 0 : 1;
}