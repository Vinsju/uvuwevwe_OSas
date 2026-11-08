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

    if (!paging_allocate_user_region(&_paging_kernel_page_directory, (void *) 0, 1024)) {
        framebuffer_write(1, 0, 'P', 0x0C, 0x00);
        while (true);
    }
    framebuffer_write(1, 0, '1', 0x0F, 0x00);

    // Load Shell dari root disk ke address 0
    struct EXT2DriverRequest request = {
        .buf = (uint8_t *) 0,
        .name = "shell",
        .name_len = 5,
        .parent_inode = 1,
        .buffer_size = 0x100000,
    };

    if (read(request) != 0) {
        framebuffer_write(1, 0, 'R', 0x0C, 0x00);
        while (true);
    }

    framebuffer_write(1, 0, '2', 0x0F, 0x00);

    // Siapkan kernel stack untuk interrupt dari Ring 3
    set_tss_kernel_current_stack();

    scheduler_init();

    __asm__ volatile ("sti");

    // Jalankan Hello World di Ring 3
    kernel_execute_user_program((uint8_t *) 0);

    // Penanda: kernel berhasil menerima kembali kontrol setelah EXIT
    framebuffer_write(2, 0, 'E', 0x0A, 0x00);

    while (true);
}