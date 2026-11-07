; Semua interrupt 0x00 - 0x3F ditangani lewat cpu/intsetup.s (kit Book 4.1.0)
; -> main_interrupt_handler() di cpu/interrupt.c.
; File ini hanya menyisakan handler kosong untuk vektor 0x40 - 0xFF yang tidak dipakai.

global isr_default_handler

section .text

isr_default_handler:
    iret
