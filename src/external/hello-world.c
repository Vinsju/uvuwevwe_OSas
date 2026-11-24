#include <stdint.h>
#include "header/syscall/syscall.h"

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    __asm__ volatile(
        "int $0x30"
        :
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory");
}

int main(void)
{
    char message[] = "Hello World!\n";

    syscall(
        SYSCALL_PUTS,
        (uint32_t)message,
        sizeof(message) - 1,
        0x07
    );

    return 0;
}