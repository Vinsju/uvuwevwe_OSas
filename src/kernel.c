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


void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    paging_init();

    pic_remap();
    pic_unmask_irq(1);
    idt_init();
    isr_init();

    framebuffer_clear();
    framebuffer_write(0, 0, 'A', 0x0F, 0x00);
    framebuffer_set_cursor(3, 12);

    initialize_filesystem_ext2();

    gdt_install_tss();
    set_tss_register();

    
    if (!paging_allocate_user_region(&_paging_kernel_page_directory, (void *) 0, 1024)) {
        while (true);   
    }

    // Load shell dari root disk ke address 0
    struct EXT2DriverRequest request = {
        .buf          = (uint8_t *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,          
        .buffer_size  = 0x100000,
    };
    if (read(request) != 0) {
        while (true);   
    }

    __asm__ volatile ("sti");

    set_tss_kernel_current_stack();
    kernel_execute_user_program((uint8_t *) 0);

    while (true);
}