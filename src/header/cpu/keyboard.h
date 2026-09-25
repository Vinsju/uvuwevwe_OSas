#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60

extern volatile uint8_t keyboard_last_scancode;

void keyboard_init(void);
void keyboard_handler(void);

#endif