#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_BUFFER_SIZE 256

/* Karakter yang dikirim ke buffer ketika Ctrl+C ditekan (ASCII ETX) */
#define KEYBOARD_CTRL_C 0x03

/* Kode khusus panah atas/bawah di buffer keyboard (dipakai riwayat command shell) */
#define KEYBOARD_KEY_UP   0x11
#define KEYBOARD_KEY_DOWN 0x12

extern volatile uint8_t keyboard_last_scancode;

void keyboard_init(void);
void keyboard_handler(void);

/**
 * Aktifkan mode input untuk user program (syscall 7).
 * Setelah aktif, tombol tidak lagi langsung ditulis ke layar oleh driver,
 * melainkan disimpan ke ring buffer dan dibaca shell lewat get_keyboard_buffer().
 * Sebelum diaktifkan, driver tetap memakai perilaku lama (echo langsung, demo M1).
 */
void keyboard_state_activate(void);
void keyboard_state_deactivate(void);
bool keyboard_is_active(void);

/**
 * Ambil 1 karakter dari ring buffer keyboard (syscall 4), non-blocking.
 * @param buf diisi karakter berikutnya, atau 0 jika buffer kosong.
 *            Enter = '\n', Backspace = '\b', Tab = '\t'.
 */
void get_keyboard_buffer(char *buf);

/**
 * Ambil lalu reset permintaan Ctrl+C. Dipakai SYSCALL_PROCESS_WAIT untuk
 * menghentikan program foreground yang sedang ditunggu shell.
 * @return true jika Ctrl+C ditekan sejak pemanggilan terakhir
 */
bool keyboard_consume_interrupt_request(void);

/**
 * Buang semua karakter yang belum dibaca di ring buffer.
 */
void keyboard_flush_buffer(void);

#endif
