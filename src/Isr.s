global isr_default_handler
global isr_keyboard_handler
global isr_timer_handler
global isr_page_fault_handler

extern keyboard_handler
extern pic_send_eoi
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

; ============================================================================
; Exception 14 (Page Fault)
; ============================================================================
; Beda dengan timer/keyboard: untuk page fault CPU OTOMATIS mendorong 1 angka
; tambahan ke stack, yaitu ERROR CODE. Alamat yang gagal diakses ada di CR2.
;
; Isi stack saat masuk ke sini:
;   [esp]      error code      <- tambahan khusus exception ini
;   [esp + 4]  eip             (alamat instruksi yang gagal)
;   [esp + 8]  cs
;   [esp + 12] eflags
;
; Konvensi pemanggilan C (cdecl): argumen di-push dari KANAN ke KIRI, jadi
; page_fault_handler(fault_address, error_code) -> push error_code dulu, baru address.
isr_page_fault_handler:
    pusha                       ; simpan 8 register umum (8 x 4 = 32 byte)
    mov  eax, cr2               ; eax = alamat yang menyebabkan page fault
    push dword [esp + 32]       ; argumen ke-2: error code (letaknya tepat di atas 32 byte pusha)
    push eax                    ; argumen ke-1: fault address
    call page_fault_handler
    add  esp, 8                 ; buang 2 argumen tadi dari stack
    popa                        ; kembalikan register seperti semula
    add  esp, 4                 ; WAJIB: buang error code. iret mengharapkan eip di [esp],
                                ; kalau error code tidak dibuang, CPU lompat ke alamat ngawur
    iret                        ; kembali dan ULANG instruksi yang tadi gagal