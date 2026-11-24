#include <stdint.h>
#include "header/syscall/syscall.h"

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    while (s[n] != '\0')
    {
        n++;
    }
    return n;
}

static void put(const char *s, uint8_t color)
{
    syscall(SYSCALL_PUTS, (uint32_t)s, str_len(s), color);
}

static void put_uint(uint32_t v, uint8_t color)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    do
    {
        buf[--i] = '0' + (v % 10);
        v /= 10;
    } while (v > 0);
    put(&buf[i], color);
}

int main(int argc, char *argv[])
{
    put("argc = ", 0x0E);
    put_uint((uint32_t)argc, 0x0F);
    put("\n", 0x0F);

    for (int i = 0; i < argc; i++)
    {
        put("argv[", 0x0B);
        put_uint((uint32_t)i, 0x0B);
        put("] = \"", 0x0B);
        put(argv[i], 0x0F);
        put("\"\n", 0x0B);
    }

    put("argv[argc] ", 0x0B);
    put(argv[argc] == 0 ? "= NULL (OK)\n" : "BUKAN NULL (BUG)\n", argv[argc] == 0 ? 0x0A : 0x0C);

    return argc;
}