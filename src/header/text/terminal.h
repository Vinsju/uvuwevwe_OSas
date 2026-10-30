#ifndef _TERMINAL_H
#define _TERMINAL_H

#include <stdint.h>

#define TERMINAL_WIDTH  80
#define TERMINAL_HEIGHT 25

/**
 * Text output dengan cursor (dipakai syscall putchar / puts).
 * Mendukung '\n' (baris baru), '\r', '\b' (hapus 1 karakter ke kiri), '\t',
 * dan scroll otomatis ketika cursor melewati baris terakhir.
 *
 * Format color: 4 bit bawah = warna huruf, 4 bit atas = warna background.
 */
void terminal_putchar(char c, uint8_t color);

/**
 * Tulis count karakter dari str (tidak bergantung pada null terminator).
 */
void terminal_puts(const char *str, uint32_t count, uint8_t color);

/**
 * Bersihkan layar dan kembalikan cursor ke (0, 0).
 */
void terminal_clear(void);

/**
 * Pindahkan cursor ke (row, col). Nilai di luar layar dipotong ke batas layar.
 */
void terminal_set_cursor(uint8_t row, uint8_t col);

/**
 * Ambil posisi cursor sekarang.
 */
void terminal_get_cursor(uint8_t *row, uint8_t *col);

#endif
