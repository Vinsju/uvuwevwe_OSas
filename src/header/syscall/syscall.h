#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>

/**
 * Nomor layanan system call (register eax), dipakai bersama oleh kernel dan user program.
 * Dipanggil lewat interrupt 0x30 (inter-privilege interrupt, DPL 3).
 *
 *  eax | layanan                | ebx                          | ecx                   | edx
 * -----+------------------------+------------------------------+-----------------------+------------
 *   0  | FS read()              | struct EXT2DriverRequest *   | int8_t *return code   | -
 *   1  | FS read_directory()    | struct EXT2DriverRequest *   | int8_t *return code   | -
 *   2  | FS write()             | struct EXT2DriverRequest *   | int8_t *return code   | -
 *   3  | FS delete()            | struct EXT2DriverRequest *   | int8_t *return code   | -
 *   4  | keyboard getchar()     | char * (0 jika buffer kosong)| -                     | -
 *   5  | text putchar()         | char                         | warna                 | -
 *   6  | text puts()            | char * buffer                | jumlah karakter       | warna
 *   7  | aktifkan input keyboard| -                            | -                     | -
 *   8  | clear screen           | -                            | -                     | -
 *   9  | set cursor             | row                          | col                   | -
 *  10  | get cursor             | uint8_t *row                 | uint8_t *col          | -
 *  11  | FS ukuran file         | struct EXT2DriverRequest *   | int8_t *return code   | uint32_t *size
 *  12  | exit                   | return code main()           | -                     | -
 *  13  | waktu CMOS (RTC)       | struct CMOSTime *            | -                     | -
 *  14  | puts di posisi tertentu| char * buffer                | jumlah karakter       | row | col << 8 | warna << 16
 *  15 | buat process (exec)    | struct EXT2DriverRequest *   | int32_t *return code  | -
 *  16 | kill process           | pid                          | int8_t *hasil (1 ok)  | -
 *  17 | info process (ps)      | struct ProcessInfo * array   | kapasitas array       | uint32_t *jumlah
 *
 * Warna: 4 bit bawah = huruf, 4 bit atas = background.
 * Return code FS mengikuti spesifikasi CRUD Chapter 2, ditambah -1 jika argumen syscall invalid.
 */
#define SYSCALL_INTERRUPT_NUMBER 0x30

#define SYSCALL_FS_READ             0
#define SYSCALL_FS_READ_DIRECTORY   1
#define SYSCALL_FS_WRITE            2
#define SYSCALL_FS_DELETE           3
#define SYSCALL_KEYBOARD_GETCHAR    4
#define SYSCALL_PUTCHAR             5
#define SYSCALL_PUTS                6
#define SYSCALL_KEYBOARD_ACTIVATE   7
#define SYSCALL_CLEAR_SCREEN        8
#define SYSCALL_SET_CURSOR          9
#define SYSCALL_GET_CURSOR          10
#define SYSCALL_FS_FILE_SIZE        11
#define SYSCALL_EXIT                12
#define SYSCALL_GET_TIME            13
#define SYSCALL_PUTS_AT             14
#define SYSCALL_PROCESS_CREATE      15
#define SYSCALL_PROCESS_KILL         16
#define SYSCALL_PROCESS_INFO        17

#define PROCESS_INFO_NAME_LENGTH 32
struct ProcessInfo {
    uint32_t pid;
    char     name[PROCESS_INFO_NAME_LENGTH];
    uint8_t  state;
};

#define SYSCALL_RESULT_NORMAL 0 // kembali ke process pemanggil
#define SYSCALL_RESULT_RETURN_TO_KERNEL 1 // kembali ke kernel_setup()
#define SYSCALL_RESULT_SWITCH_PROCESS 2 // pindah ke process lain

/* Helper untuk register edx SYSCALL_PUTS_AT: posisi & warna dikemas dalam 1 register */
#define SYSCALL_PUTS_AT_POS(row, col, color) \
    ((uint32_t)(row) | ((uint32_t)(col) << 8) | ((uint32_t)(color) << 16))

/**
 * Handler kernel untuk interrupt 0x30. Dipanggil dari main_interrupt_handler (cpu/interrupt.c)
 * dengan nilai register eax, ebx, ecx, edx milik user program.
 */
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

#endif
