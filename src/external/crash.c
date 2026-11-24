#ifndef CRASH_CASE
#define CRASH_CASE 1
#endif

int main(void) {
#if CRASH_CASE == 1
    __asm__ volatile("ud2");                              // invalid opcode (#6)
#elif CRASH_CASE == 2
    volatile int z = 0; volatile int x = 1 / z; (void)x;  // divide error (#0)
#elif CRASH_CASE == 3
    *(volatile int *)0x50000000 = 1;                      // page fault, alamat tak valid
#elif CRASH_CASE == 4
    *(volatile int *)0xC0100000 = 1;                      // tulis ke alamat kernel
#elif CRASH_CASE == 6
    for (;;) { }                                          // CASE 6
#endif
    return 0;                                             // CASE 5: keluar normal
}