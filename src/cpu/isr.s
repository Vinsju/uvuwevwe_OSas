global isr_default_handler
global isr_keyboard_handler
global isr_timer_handler
global isr_syscall_handler

extern keyboard_handler
extern pic_send_eoi
extern syscall_handler

section .text

isr_default_handler:
    pusha
    popa
    iret

isr_timer_handler:
    pusha
    push dword 0
    call pic_send_eoi
    add esp, 4
    popa
    iret

isr_keyboard_handler:
    pusha
    call keyboard_handler
    popa
    iret

; System call (int 0x30), dipanggil user program dari ring 3.
; Argumen diteruskan ke syscall_handler(eax, ebx, ecx, edx) lewat stack (cdecl).
; Semua register dikembalikan seperti semula dengan popa.
isr_syscall_handler:
    pusha
    push edx
    push ecx
    push ebx
    push eax
    call syscall_handler
    add esp, 16
    popa
    iret
