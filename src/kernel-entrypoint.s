global loader       ; the entry symbol for ELF
global load_gdt     ; load GDT table
global set_tss_register
global kernel_execute_user_program
global kernel_stack_top
global kernel_saved_esp
global kernel_return_from_user
global ring3_interrupt_stack_top

extern kernel_setup
extern _paging_kernel_page_directory
extern _paging_kernel_page_table


KERNEL_STACK_SIZE equ 2097152        ; size of stack in bytes (2 MiB, spek 2.6)
MAGIC_NUMBER      equ 0x1BADB002     ; define the magic number constant
FLAGS             equ 0x0            ; multiboot flags
CHECKSUM          equ -MAGIC_NUMBER  ; calculate the checksum
                                     ; (magic number + checksum + flags should equal 0)
KERNEL_VIRTUAL_BASE equ 0xC0000000

section .bss
align 4
kernel_stack:
    resb KERNEL_STACK_SIZE
kernel_stack_top:

align 4
kernel_saved_esp:
    resd 1

alignb 16
ring3_interrupt_stack:
    resb 16384
ring3_interrupt_stack_top:


section .multiboot                   ; GNU GRUB Multiboot header
align 4                              ; the code must be 4 byte aligned
    dd MAGIC_NUMBER                  ; write the magic number to the machine code,
    dd FLAGS                         ; the flags,
    dd CHECKSUM                      ; and the checksum

section .setup.text

loader equ (loader_entrypoint - KERNEL_VIRTUAL_BASE)

loader_entrypoint:
    mov edi, _paging_kernel_page_table - KERNEL_VIRTUAL_BASE
    xor eax, eax
    mov ecx, 1024

.fill_kernel_page_table:
    mov edx, eax
    or  edx, 0x3
    mov [edi], edx
    add edi, 4
    add eax, 0x1000
    loop .fill_kernel_page_table

    mov eax, _paging_kernel_page_table - KERNEL_VIRTUAL_BASE
    or  eax, 0x3
    mov [_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE], eax
    mov [_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xC00], eax

    ; Recursive mapping: entry 1023 menunjuk page directory itu sendiri,
    ; sehingga page table dapat diakses di 0xFFC00000 + index * 0x1000 (dipakai paging_get_page_table)
    mov eax, _paging_kernel_page_directory - KERNEL_VIRTUAL_BASE
    or  eax, 0x3
    mov [_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 1023 * 4], eax

    mov eax, _paging_kernel_page_directory - KERNEL_VIRTUAL_BASE
    mov cr3, eax

    mov eax, cr4
    and eax, 0xFFFFFFEF
    mov cr4, eax

    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    lea eax, [loader_virtual]
    jmp eax

loader_virtual:
    mov dword [_paging_kernel_page_directory], 0
    invlpg [0]
    mov esp, kernel_stack + KERNEL_STACK_SIZE
    call kernel_setup

.loop:
    jmp .loop

; More details: https://en.wikibooks.org/wiki/X86_Assembly/Protected_Mode
load_gdt:
    cli
    mov  eax, [esp+4]
    lgdt [eax] ; Load GDT from GDTDescriptor, eax at this line will point GDTR location

    ; Set bit-0 (Protection Enable bit-flag) in Control Register 0 (CR0)
    ; This is optional, as usually GRUB already start with protected mode flag enabled
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    ; Far jump to update cs register
    ; Warning: Invalid GDT will raise exception in following instruction below
    jmp 0x8:flush_cs
flush_cs:
    mov ax, 10h ; Update all segment register
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret

section .text

set_tss_register:
    mov ax, 0x28 | 0 ; GDT TSS Selector, ring 0
    ltr ax
    ret

kernel_execute_user_program:
    ; Simpan EFLAGS kernel
    pushfd

    ; Simpan register kernel
    push ebp
    push ebx
    push esi
    push edi

    ; Simpan posisi stack kernel untuk dipulihkan saat SYSCALL_EXIT
    mov [kernel_saved_esp], esp

    ; Pindahkan data segment ke user data segment (0x23)
    mov eax, 0x20 | 0x3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Siapkan stack frame untuk berpindah ke Ring 3 menggunakan iret
    mov ecx, [esp + 24] ; Argumen virtual_addr sebelum empat push register

    push eax            ; SS user = 0x23

    mov eax, 0xBFFFFFFC
    push eax            ; ESP user = puncak stack process (PROCESS_USER_STACK_TOP)

    pushfd              ; EFLAGS
    or dword [esp], 0x200 ; nyalakan Interrupt Flag supaya keyboard & timer jalan di ring 3

    mov eax, 0x18 | 0x3
    push eax            ; CS user = 0x1B

    mov eax, ecx
    push eax            ; EIP = virtual_addr

    iret

kernel_return_from_user:
    ; Pulihkan segment register kernel
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Pulihkan stack kernel
    mov esp, [kernel_saved_esp]

    ; Pulihkan register kernel
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; Pulihkan EFLAGS kernel
    popfd

    ; Kembali ke kernel_setup()
    ret