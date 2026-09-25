#include "header/cpu/keyboard.h"
#include "header/cpu/pic.h"

static inline uint8_t keyboard_read(void) {
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(KEYBOARD_DATA_PORT)
    );

    return value;
}

void keyboard_handler(void) {
    uint8_t scancode = keyboard_read();

    (void)scancode;

    pic_send_eoi(1);
}

void keyboard_init(void) {
}