#include <stdint.h>
#include <stdbool.h>
#include "header/syscall/syscall.h"
#include "header/cpu/cmos.h"

/*
 * Clock (Book 4.3.3): user program yang menampilkan jam:menit:detik
 * di pojok kanan bawah layar (status line), diperbarui setiap detik.
 * Dijalankan sebagai process terpisah (exec clock) dan bisa dihentikan dengan kill.
 */

#define CLOCK_ROW   24 // status line, tidak ikut scroll shell (lihat header/text/terminal.h)
#define CLOCK_COL   72 // "HH:MM:SS" = 8 karakter, rata kanan pada layar 80 kolom
#define CLOCK_COLOR 0x0B

/* Jeda kecil antar pembacaan CMOS supaya tidak terus-menerus memanggil syscall */
#define CLOCK_POLL_DELAY 20000

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    __asm__ volatile(
        "int $0x30"
        :
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory");
}

static void put_two_digits(char *dst, uint8_t value)
{
    dst[0] = (char)('0' + (value / 10) % 10);
    dst[1] = (char)('0' + value % 10);
}

static void draw_time(const struct CMOSTime *time)
{
    char text[8] = {'0', '0', ':', '0', '0', ':', '0', '0'};
    put_two_digits(&text[0], time->hour);
    put_two_digits(&text[3], time->minute);
    put_two_digits(&text[6], time->second);

    syscall(SYSCALL_PUTS_AT, (uint32_t)text, sizeof(text),
            SYSCALL_PUTS_AT_POS(CLOCK_ROW, CLOCK_COL, CLOCK_COLOR));
}

int main(void)
{
    struct CMOSTime now;
    uint8_t last_second = 0xFF; // nilai mustahil, supaya langsung digambar pertama kali

    while (true)
    {
        syscall(SYSCALL_GET_TIME, (uint32_t)&now, 0, 0);
        if (now.second != last_second)
        {
            last_second = now.second;
            draw_time(&now);
        }

        for (volatile uint32_t i = 0; i < CLOCK_POLL_DELAY; i++)
            ;
    }

    return 0;
}
