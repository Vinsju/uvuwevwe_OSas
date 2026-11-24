#ifndef _SPEAKER_H
#define _SPEAKER_H

#include <stdint.h>

#define SPEAKER_PIT_CHANNEL_2_PIO 0x42
#define SPEAKER_CONTROL_PIO 0x61
#define SPEAKER_FREQ_MIN 20
#define SPEAKER_FREQ_MAX 20000

#define SPEAKER_PIT_COMMAND_VALUE ((0b10 << 6) | PIT_COMMAND_VALUE_ACC_LOHIBYTE | PIT_COMMAND_VALUE_OPR_SQUARE_WAVE | PIT_COMMAND_VALUE_BINARY_MODE)

void speaker_play(uint32_t frequency);
/* nyala terus sampai speaker_stop */

void speaker_stop(void);

void speaker_beep(uint32_t frequency, uint32_t duration_ms);
/* nyala sebentar tanpa blocking */

void speaker_tick(void); 
/* dipanggil tiap tick timer (IRQ0) */



#endif