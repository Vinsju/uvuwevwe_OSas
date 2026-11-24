global _start
extern main

; Kernel menyiapkan stack awal (System V i386):
;   [esp]        = argc
;   [esp + 4]    = argv[0] ... argv[argc-1], lalu NULL

section .text
_start:
    xor  ebp, ebp
    mov  eax, [esp]             ; argc
    lea  ebx, [esp + 4]         ; argv
    and  esp, 0xFFFFFFF0        ; 16-byte align
    sub  esp, 8
    push ebx                    ; argv
    push eax                    ; argc
    call main

    mov ebx, eax
    mov eax, 12
    int 0x30

.hang:
    jmp .hang