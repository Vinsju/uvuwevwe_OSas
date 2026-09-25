#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60

void keyboard_init(void);
void keyboard_handler(void);

#endif