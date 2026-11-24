/**
 * beep: user program uji driver speaker.
 * Dijalankan dari shell: exec beep   (foreground)  atau  exec beep &   (background)
 */
#include <stdint.h>
#include "header/syscall/syscall.h"

/* Jumlah iterasi loop kosong per milidetik. Atur lewat -DLOOPS_PER_MS=... kalau tempo meleset. */
#ifndef LOOPS_PER_MS
#define LOOPS_PER_MS 20000u
#endif

/* Wrapper syscall, sama seperti yang dipakai di user-shell.c */
void syscall(uintptr_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx)
{
    __asm__ volatile("int $0x30"
                     :
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
}

/* frekuensi 0 = jeda, tidak bersuara */
struct Note {
    uint16_t freq;
    uint16_t ms;
};

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * LOOPS_PER_MS; i++)
    {
    }
}

static void play_note(uint32_t freq, uint32_t ms)
{
    if (freq != 0)
    {
        /* Kernel mematikan nadanya sendiri setelah (ms * 9 / 10) ms, sisanya jadi jeda antar nada */
        syscall(SYSCALL_SPEAKER, freq, ms * 9 / 10, 0);
    }
    delay_ms(ms);
}

int main(void)
{
    /* Tangga nada C mayor: C4 D4 E4 F4 G4 A4 B4 C5 */
    static const struct Note melody[] = {
        {262, 300}, {294, 300}, {330, 300}, {349, 300},
        {392, 300}, {440, 300}, {494, 300}, {523, 600},
        {0, 200},
        {523, 300}, {392, 300}, {330, 300}, {262, 600},
    };

    for (uint32_t i = 0; i < sizeof(melody) / sizeof(melody[0]); i++)
    {
        play_note(melody[i].freq, melody[i].ms);
    }

    /* Pastikan speaker mati sebelum keluar */
    syscall(SYSCALL_SPEAKER, 0, 0, 0);
    return 0;
}