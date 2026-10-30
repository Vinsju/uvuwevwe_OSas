global isr_default_handler
global isr_keyboard_handler
global isr_timer_handler
global isr_syscall_handler
global isr_page_fault_handler

extern keyboard_handler
extern pic_send_eoi
extern syscall_handler
extern page_fault_handler

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

; Exception 14 (Page Fault): CPU mendorong ERROR CODE ke stack, alamat penyebab ada di CR2.
; Stack saat masuk: [esp] error code, [esp+4] eip, [esp+8] cs, [esp+12] eflags
isr_page_fault_handler:
    pusha                       ; simpan 8 register (32 byte)
    mov  eax, cr2               ; eax = alamat penyebab page fault
    push dword [esp + 32]       ; argumen ke-2: error code
    push eax                    ; argumen ke-1: fault address
    call page_fault_handler
    add  esp, 8                 ; buang 2 argumen
    popa
    add  esp, 4                 ; WAJIB: buang error code sebelum iret
    iret                        ; ulang instruksi yang tadi gagal