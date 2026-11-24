#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/syscall/syscall.h"
#include "header/filesystem/ext2.h"
#include "header/cpu/keyboard.h"
#include "header/text/terminal.h"
#include "header/cpu/cmos.h"
#include "header/process/process.h"
#include "header/memory/swap.h"
#include "header/cpu/speaker.h"

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

/* Buffer statis: syscall dijalankan tanpa reentrancy, dan terlalu besar untuk stack ISR */
static struct ProcessArgvBlob syscall_argv_blob;

/* Salin argc/argv dari struct ProcessArgs milik user ke kernel dengan validasi penuh. */
static bool copy_user_args(uint32_t args_addr, struct ProcessArgvBlob *blob)
{
    blob->argc = 0;
    blob->data_len = 0;

    if (args_addr == 0 || !user_range_ok(args_addr, sizeof(struct ProcessArgs)))
    {
        return false;
    }
    struct ProcessArgs *user_args = (struct ProcessArgs *)args_addr;
    uint32_t argc = user_args->argc;
    uint32_t argv_addr = (uint32_t)user_args->argv;

    if (argc == 0 || argc > PROCESS_ARGV_MAX_COUNT)
    {
        return false;
    }
    if (argv_addr == 0 || !user_range_ok(argv_addr, argc * sizeof(char *)))
    {
        return false;
    }

    char **argv = (char **)argv_addr;
    for (uint32_t i = 0; i < argc; i++)
    {
        uint32_t str = (uint32_t)argv[i];
        if (str == 0)
        {
            return false;
        }
        blob->offset[i] = blob->data_len;
        while (true)
        {
            if (str >= USER_SPACE_END || blob->data_len >= PROCESS_ARGV_MAX_BYTES)
            {
                return false;
            }
            char c = *(const char *)str++;
            blob->data[blob->data_len++] = c;
            if (c == '\0')
            {
                break;
            }
        }
    }
    blob->argc = argc;
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
    if (check_buffer && request->buffer_size > 0) {
        uint32_t size = request->buffer_size;
        uint32_t file_size;
        
        if (service == SYSCALL_FS_READ && get_file_size(*request, &file_size) == 0 && file_size < size) {
            size = file_size;
        }
        swap_prefault(request->buf, size);
    }
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

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
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
    
    case SYSCALL_GET_TIME:
        if (ebx != 0 && user_range_ok(ebx, sizeof(struct CMOSTime)))
        {
            cmos_read_time((struct CMOSTime *)ebx);
        }
        break;

    case SYSCALL_PUTS_AT:
        if (ecx <= TERMINAL_WIDTH && user_range_ok(ebx, ecx))
        {
            if ((edx & 0xFF) == TERMINAL_STATUS_ROW)
            {
                struct ProcessControlBlock *owner = process_get_current_running_pcb_pointer();
                if (owner != NULL)
                {
                    terminal_set_status_line_owner(owner->metadata.pid);
                }
            }
            terminal_write_at(
                (uint8_t)(edx & 0xFF),
                (uint8_t)((edx >> 8) & 0xFF),
                (const char *)ebx,
                ecx,
                (uint8_t)((edx >> 16) & 0xFF));
        }
        break;

    case SYSCALL_PROCESS_CREATE:
    // ebx = request
    // ecx = *retcode
        // Ctrl+C yang ditekan sebelum program ini dibuat tidak boleh langsung menghentikannya
        keyboard_consume_interrupt_request();
        if (ecx != 0 && user_range_ok(ecx, sizeof(int32_t))) {
            int32_t *retcode = (int32_t *)ecx;
            if (!user_request_ok(ebx, false)) {
                *retcode = -1;
            }
            else if (edx == 0) {
                *retcode = process_create_user_process_args(*(struct EXT2DriverRequest *)ebx, NULL);
            }
            else if (!copy_user_args(edx, &syscall_argv_blob)) {
                *retcode = -1;
            }
            else {
                *retcode = process_create_user_process_args(*(struct EXT2DriverRequest *)ebx, &syscall_argv_blob);
                if (*retcode == PROCESS_CREATE_SUCCESS) {
                    ((struct ProcessArgs *)edx)->pid = process_get_last_created_pid();
                }
            }
        }
        break;
    
        case SYSCALL_PROCESS_KILL:
        {    
            struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
            bool kill_self = current != NULL && current->metadata.pid == ebx;
            bool ok = process_destroy(ebx);
            if (kill_self && ok) {
                return SYSCALL_RESULT_SWITCH_PROCESS;
            }
            if (ecx != 0 && user_range_ok(ecx, sizeof(int8_t))) {
                *(int8_t *)ecx = ok ? 1 : 0;
            }
            break;
        }

        case SYSCALL_PROCESS_INFO:
            if (edx != 0 && user_range_ok(edx, sizeof(uint32_t)) && ecx <= PROCESS_COUNT_MAX && user_range_ok(ebx, ecx * sizeof(struct ProcessInfo))) {
                *(uint32_t *)edx = process_get_info((struct ProcessInfo *)ebx, ecx);
            }
            break;

        case SYSCALL_SBRK:
            if (user_range_ok(ecx, sizeof(uint32_t))) {
                *(uint32_t *)ecx = process_sbrk((int32_t) ebx);
            }
            break;

        case SYSCALL_PROCESS_WAIT:
        {
            /* Blocking: selama pid masih hidup, yield dan ulangi syscall ini nanti.
             * ecx (opsional) = uint8_t * -> diisi 1 jika process dihentikan dengan Ctrl+C, 0 jika selesai sendiri */
            struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
            bool interrupted = false;
            if (current != NULL && current->metadata.pid != ebx && process_is_alive(ebx)) {
                if (!keyboard_consume_interrupt_request()) {
                    return SYSCALL_RESULT_YIELD_RETRY;
                }
                // Ctrl+C: hentikan program foreground, buang ketikan yang tertahan selama program berjalan
                process_destroy(ebx);
                keyboard_flush_buffer();
                interrupted = true;
            }
            if (ecx != 0 && user_range_ok(ecx, sizeof(uint8_t))) {
                *(uint8_t *)ecx = interrupted ? 1 : 0;
            }
            break;
        }
        
        case SYSCALL_EXIT:
        {
            struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();

            if (current != NULL) {
                process_destroy(current->metadata.pid);

            }

            return SYSCALL_RESULT_SWITCH_PROCESS;
        }

        case SYSCALL_SPEAKER:
            if (ebx == 0) speaker_stop();
            else if (ecx == 0) speaker_play(ebx);
            else speaker_beep(ebx, ecx);
            break;


    default:
        break;
    }

    return 0;
}
