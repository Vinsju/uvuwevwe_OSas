#include "../header/cpu/speaker.h"
#include "../header/cpu/interrupt.h"
#include "../header/cpu/portio.h"

static volatile uint32_t beep_ticks_left = 0;

void speaker_play(uint32_t frequency) {
    if (frequency < SPEAKER_FREQ_MIN) frequency = SPEAKER_FREQ_MIN;
    if (frequency > SPEAKER_FREQ_MAX) frequency = SPEAKER_FREQ_MAX;
    uint32_t divisor = PIT_MAX_FREQUENCY / frequency;

    out(PIT_COMMAND_REGISTER_PIO, SPEAKER_PIT_COMMAND_VALUE);
    out(SPEAKER_PIT_CHANNEL_2_PIO, (uint8_t) (divisor & 0xFF));
    out(SPEAKER_PIT_CHANNEL_2_PIO, (uint8_t) (divisor >> 8) & 0xFF);
    
    beep_ticks_left = 0;
    uint8_t ctrl = in(SPEAKER_CONTROL_PIO);
    if ((ctrl & 0x3) != 0x3) out(SPEAKER_CONTROL_PIO, ctrl | 0x3);
}

void speaker_stop(void) {
    beep_ticks_left = 0;
    uint8_t ctrl = in(SPEAKER_CONTROL_PIO);
    out(SPEAKER_CONTROL_PIO, ctrl & 0xFC);
}

void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    speaker_play(frequency);
    uint32_t ticks = duration_ms * PIT_TIMER_FREQUENCY / 1000;
    beep_ticks_left = (ticks == 0) ? 1 : ticks;
}

void speaker_tick(void) {
    if (beep_ticks_left > 0 && --beep_ticks_left == 0) {
        speaker_stop();
    }
}