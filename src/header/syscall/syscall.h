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

/**
 * Handler kernel untuk interrupt 0x30. Dipanggil dari isr_syscall_handler (cpu/isr.s)
 * dengan nilai register eax, ebx, ecx, edx milik user program.
 */
void syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

#endif
