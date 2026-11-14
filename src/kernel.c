#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/cpu/pic.h"
#include "header/cpu/idt.h"
#include "header/cpu/isr.h"
#include "header/text/framebuffer.h"
#include "header/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/scheduler.h"


void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    paging_init();

    pic_remap();
    idt_init();
    isr_init();
    pic_unmask_irq(1);

    framebuffer_clear();
    framebuffer_write(0, 0, 'A', 0x0F, 0x00);
    framebuffer_set_cursor(3, 12);

    initialize_filesystem_ext2();

    gdt_install_tss();
    set_tss_register();

    // Shell request
    struct EXT2DriverRequest request = {
        .buf = (uint8_t *) 0,
        .name = "shell",
        .name_len = 5,
        .parent_inode = 1,
        .buffer_size = 0x100000,
    };

    set_tss_kernel_current_stack();

    // Create & Execute process 0
    int32_t retcode = process_create_user_process(request);
    if (retcode != PROCESS_CREATE_SUCCESS) {
        framebuffer_write(1, 0, 'P', 0x0C, 0x00);
        framebuffer_write(1, 1, '0' + retcode, 0x0C, 0x00);
        while(true);
    }

    scheduler_init();
    scheduler_switch_to_next_process();

    _process_list[0].metadata.state = PROCESS_STATE_RUNNING;

    paging_use_page_directory(_process_list[0].context.page_directory_virtual_addr);
    kernel_execute_user_program((void *) 0x0);
}