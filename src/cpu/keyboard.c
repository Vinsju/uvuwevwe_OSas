#include "header/cpu/keyboard.h"
#include "header/cpu/pic.h"
#include "header/text/framebuffer.h"

/* Scancode set 1 -> ASCII tanpa Shift */
static const char scancode_to_ascii[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',

    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',

    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',

    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',

    [0x39] = ' '
};

/* Scancode set 1 -> ASCII dengan Shift */
static const char scancode_to_ascii_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',

    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',

    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',

    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',

    [0x39] = ' '
};

#define SC_LEFT_SHIFT          0x2A
#define SC_RIGHT_SHIFT         0x36
#define SC_LEFT_SHIFT_RELEASE  0xAA
#define SC_RIGHT_SHIFT_RELEASE 0xB6
#define SC_CAPS_LOCK           0x3A
#define SC_EXTENDED_PREFIX     0xE0

volatile char keyboard_last_ascii = 0;
volatile uint8_t keyboard_last_scancode = 0;

/* Mode lama (sebelum shell aktif): echo langsung ke layar */
static uint8_t keyboard_row = 4;
static uint8_t keyboard_col = 0;

/* Mode shell: ring buffer */
static volatile bool keyboard_active = false;
static volatile char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t keyboard_head = 0; // posisi baca
static volatile uint32_t keyboard_tail = 0; // posisi tulis

static bool shift_pressed = false;
static bool caps_lock = false;
static bool extended = false;

static inline uint8_t keyboard_read(void) {
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(KEYBOARD_DATA_PORT)
    );

    return value;
}

static void keyboard_buffer_push(char c) {
    uint32_t next = (keyboard_tail + 1) % KEYBOARD_BUFFER_SIZE;
    if (next == keyboard_head) {
        return; // buffer penuh, karakter dibuang
    }
    keyboard_buffer[keyboard_tail] = c;
    keyboard_tail = next;
}

static char scancode_to_char(uint8_t scancode) {
    char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    if (caps_lock) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
    }
    return c;
}

/* Perilaku lama dari Milestone 1, dipakai selama shell belum aktif */
static void keyboard_legacy_echo(char c) {
    if (c == '\n' || c == '\b' || c == '\t') {
        return;
    }
    framebuffer_write(keyboard_row, keyboard_col, c, 0, 0xF);
    keyboard_col++;

    if (keyboard_col >= 80) {
        keyboard_col = 0;
        keyboard_row++;

        if (keyboard_row >= 25) {
            keyboard_row = 0;
        }
    }

    framebuffer_set_cursor(keyboard_row, keyboard_col);
}

void keyboard_handler(void) {
    uint8_t scancode = keyboard_read();
    keyboard_last_scancode = scancode;

    if (scancode == SC_EXTENDED_PREFIX) {
        extended = true;
        pic_send_eoi(1);
        return;
    }

    if (extended) {
        /* Hanya Enter & '/' keypad yang dipakai, tombol extended lain diabaikan */
        extended = false;
        char ext_char = 0;
        if (scancode == 0x1C) {
            ext_char = '\n';
        } else if (scancode == 0x35) {
            ext_char = '/';
        }
        if (ext_char != 0) {
            keyboard_last_ascii = ext_char;
            if (keyboard_active) {
                keyboard_buffer_push(ext_char);
            } else {
                keyboard_legacy_echo(ext_char);
            }
        }
        pic_send_eoi(1);
        return;
    }

    switch (scancode) {
    case SC_LEFT_SHIFT:
    case SC_RIGHT_SHIFT:
        shift_pressed = true;
        break;
    case SC_LEFT_SHIFT_RELEASE:
    case SC_RIGHT_SHIFT_RELEASE:
        shift_pressed = false;
        break;
    case SC_CAPS_LOCK:
        caps_lock = !caps_lock;
        break;
    default:
        if (scancode < 128) {
            char c = scancode_to_char(scancode);
            keyboard_last_ascii = c;
            if (c != 0) {
                if (keyboard_active) {
                    keyboard_buffer_push(c);
                } else {
                    keyboard_legacy_echo(c);
                }
            }
        }
        break;
    }

    pic_send_eoi(1);
}

void keyboard_init(void) {
}

void keyboard_state_activate(void) {
    keyboard_head = 0;
    keyboard_tail = 0;
    keyboard_active = true;
}

void keyboard_state_deactivate(void) {
    keyboard_active = false;
}

bool keyboard_is_active(void) {
    return keyboard_active;
}

void get_keyboard_buffer(char *buf) {
    if (keyboard_head == keyboard_tail) {
        *buf = 0;
        return;
    }
    *buf = keyboard_buffer[keyboard_head];
    keyboard_head = (keyboard_head + 1) % KEYBOARD_BUFFER_SIZE;
}
