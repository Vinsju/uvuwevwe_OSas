#ifndef _ISR_H
#define _ISR_H

#include <stdint.h>

void isr_default_handler(void);
void isr_timer_handler(void);
void isr_keyboard_handler(void);
void isr_init(void);

#endif