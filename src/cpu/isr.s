global isr_default_handler
global isr_keyboard_handler
global isr_timer_handler

extern keyboard_handler
extern pic_send_eoi

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