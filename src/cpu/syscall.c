#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/syscall/syscall.h"
#include "header/filesystem/ext2.h"
#include "header/cpu/keyboard.h"
#include "header/text/terminal.h"

/* Semua alamat milik user berada di bawah higher half kernel */
#define USER_SPACE_END 0xC0000000u

/**
 * Input dari user wajib dianggap tidak aman (Book 3.4.1, Security).
 * Pastikan seluruh rentang [addr, addr + size) berada di user space dan tidak overflow.
 */
static bool user_range_ok(uint32_t addr, uint32_t size)
{
    if (addr == 0 && size != 0)
    {
        return false;
    }
    if (addr >= USER_SPACE_END)
    {
        return false;
    }
    return size <= USER_SPACE_END - addr;
}

/* Validasi request FS: struct, nama, dan buffer harus milik user */
static bool user_request_ok(uint32_t request_addr, bool check_buffer)
{
    if (!user_range_ok(request_addr, sizeof(struct EXT2DriverRequest)) || request_addr == 0)
    {
        return false;
    }
    struct EXT2DriverRequest *request = (struct EXT2DriverRequest *)request_addr;
    if (request->name == NULL || !user_range_ok((uint32_t)request->name, request->name_len))
    {
        return false;
    }
    if (check_buffer && request->buffer_size > 0 &&
        !user_range_ok((uint32_t)request->buf, request->buffer_size))
    {
        return false;
    }
    return true;
}

static void syscall_fs(uint32_t service, uint32_t request_addr, uint32_t retcode_addr)
{
    if (retcode_addr == 0 || !user_range_ok(retcode_addr, sizeof(int8_t)))
    {
        return;
    }
    int8_t *retcode = (int8_t *)retcode_addr;

    /* delete tidak memakai buf & buffer_size */
    bool check_buffer = service != SYSCALL_FS_DELETE;
    if (!user_request_ok(request_addr, check_buffer))
    {
        *retcode = -1;
        return;
    }

    struct EXT2DriverRequest *request = (struct EXT2DriverRequest *)request_addr;
    switch (service)
    {
    case SYSCALL_FS_READ:
        *retcode = read(*request);
        break;
    case SYSCALL_FS_READ_DIRECTORY:
        *retcode = read_directory(request);
        break;
    case SYSCALL_FS_WRITE:
        *retcode = write(request);
        break;
    case SYSCALL_FS_DELETE:
        *retcode = delete (*request);
        break;
    default:
        *retcode = -1;
        break;
    }
}

void syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    switch (eax)
    {
    case SYSCALL_FS_READ:
    case SYSCALL_FS_READ_DIRECTORY:
    case SYSCALL_FS_WRITE:
    case SYSCALL_FS_DELETE:
        syscall_fs(eax, ebx, ecx);
        break;

    case SYSCALL_KEYBOARD_GETCHAR:
        if (ebx != 0 && user_range_ok(ebx, sizeof(char)))
        {
            get_keyboard_buffer((char *)ebx);
        }
        break;

    case SYSCALL_PUTCHAR:
        terminal_putchar((char)ebx, (uint8_t)ecx);
        break;

    case SYSCALL_PUTS:
        if (user_range_ok(ebx, ecx))
        {
            terminal_puts((const char *)ebx, ecx, (uint8_t)edx);
        }
        break;

    case SYSCALL_KEYBOARD_ACTIVATE:
        keyboard_state_activate();
        break;

    case SYSCALL_CLEAR_SCREEN:
        terminal_clear();
        break;

    case SYSCALL_SET_CURSOR:
        terminal_set_cursor((uint8_t)ebx, (uint8_t)ecx);
        break;

    case SYSCALL_FS_FILE_SIZE:
        if (ecx != 0 && user_range_ok(ecx, sizeof(int8_t)))
        {
            int8_t *retcode = (int8_t *)ecx;
            if (edx == 0 || !user_range_ok(edx, sizeof(uint32_t)) || !user_request_ok(ebx, false))
            {
                *retcode = -1;
            }
            else
            {
                *retcode = get_file_size(*(struct EXT2DriverRequest *)ebx, (uint32_t *)edx);
            }
        }
        break;

    case SYSCALL_GET_CURSOR:
        if (ebx != 0 && ecx != 0 && user_range_ok(ebx, 1) && user_range_ok(ecx, 1))
        {
            terminal_get_cursor((uint8_t *)ebx, (uint8_t *)ecx);
        }
        break;

    default:
        break;
    }
}
