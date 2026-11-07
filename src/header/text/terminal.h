#ifndef _TERMINAL_H
#define _TERMINAL_H

#include <stdint.h>

#define TERMINAL_WIDTH  80
#define TERMINAL_HEIGHT 25

/**
 * Baris terakhir dipakai sebagai status line (misalnya program clock, Book 4.3.3).
 * Teks shell hanya memakai baris 0 - (TERMINAL_TEXT_HEIGHT - 1), sehingga scroll
 * dan clear tidak menggeser / menghapus status line.
 */
#define TERMINAL_STATUS_ROW  (TERMINAL_HEIGHT - 1)
#define TERMINAL_TEXT_HEIGHT (TERMINAL_HEIGHT - 1)

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
 * Bersihkan area teks dan kembalikan cursor ke (0, 0). Status line tidak dihapus.
 */
void terminal_clear(void);

/**
 * Pindahkan cursor ke (row, col). Nilai di luar area teks dipotong ke batas area teks.
 */
void terminal_set_cursor(uint8_t row, uint8_t col);

/**
 * Ambil posisi cursor sekarang.
 */
void terminal_get_cursor(uint8_t *row, uint8_t *col);

/**
 * Tulis count karakter langsung ke posisi (row, col) TANPA memindahkan cursor
 * (dipakai syscall puts_at, misalnya oleh program clock).
 * Karakter kontrol ditulis sebagai spasi, teks yang melewati batas kanan layar dipotong.
 */
void terminal_write_at(uint8_t row, uint8_t col, const char *str, uint32_t count, uint8_t color);

#endif
