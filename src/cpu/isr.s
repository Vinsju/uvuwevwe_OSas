global isr_default_handler
global isr_keyboard_handler

extern keyboard_handler

section .text

isr_default_handler:
    pusha
    popa
    iret

isr_keyboard_handler:
    pusha
    call keyboard_handler
    popa
    iret