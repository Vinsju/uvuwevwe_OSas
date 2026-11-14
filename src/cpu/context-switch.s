global process_context_switch

; void process_context_switch(struct Context ctx);   (noreturn)
;
; ctx dipass by value (cdecl), jadi mulai di [esp+4] saat masuk fungsi.
; Offset field struct Context:
;   edi  0   esi  4   ebp  8   esp 12
;   ebx 16   edx 20   ecx 24   eax 28
;   gs  32   fs  36   es  40   ds  44
;   eip 48   eflags 52   page_directory 56 (diurus di C, bukan di sini)

section .text

process_context_switch:
    ; 1. Simpan base address ctx. ecx dipilih sebagai base,
    ;    jadi ecx harus jadi register TERAKHIR yang di-load.
    lea  ecx, [esp + 4]

    ; 2. Siapkan stack frame iret (urutan push: ss, esp, eflags, cs, eip)
    push dword 0x20 | 0x3       ; ss  = user data segment
    push dword [ecx + 12]       ; esp user
    push dword [ecx + 52]       ; eflags
    push dword 0x18 | 0x3       ; cs  = user code segment
    push dword [ecx + 48]       ; eip

    ; 3. Load semua register dari ctx
    ;    Segment register tidak bisa diisi dari memory langsung, pakai eax
    mov  eax, [ecx + 32]
    mov  gs, ax
    mov  eax, [ecx + 36]
    mov  fs, ax
    mov  eax, [ecx + 40]
    mov  es, ax
    mov  eax, [ecx + 44]
    mov  ds, ax

    mov  edi, [ecx + 0]
    mov  esi, [ecx + 4]
    mov  ebp, [ecx + 8]
    mov  ebx, [ecx + 16]
    mov  edx, [ecx + 20]

    ; 4. Dua register terakhir: eax dulu, ecx paling akhir (dia base-nya)
    mov  eax, [ecx + 28]
    mov  ecx, [ecx + 24]

    ; 5. Lompat ke process
    iret