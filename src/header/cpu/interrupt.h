#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/cpu/pic.h"

/**
 * Struktur interrupt mengikuti kit Chapter 1 yang sudah dikoreksi (Book 4.1.0).
 * Konstanta port PIC (PIC1_COMMAND, PIC1_DATA, ...) ada di header/cpu/pic.h.
 */

/* -- PIC constants -- */

// PIC interrupt offset (sesuai pic_remap())
#define PIC1_OFFSET          0x20
#define PIC2_OFFSET          0x28

/* -- PICs IRQ list -- */

// PIC Master
#define IRQ_TIMER        0
#define IRQ_KEYBOARD     1
#define IRQ_CASCADE      2
#define IRQ_COM2         3
#define IRQ_COM1         4
#define IRQ_LPT2         5
#define IRQ_FLOPPY_DISK  6
#define IRQ_LPT1_SPUR    7

// PIC Slave
#define IRQ_CMOS         8
#define IRQ_PERIPHERAL_1 9
#define IRQ_PERIPHERAL_2 10
#define IRQ_PERIPHERAL_3 11
#define IRQ_MOUSE        12
#define IRQ_FPU          13
#define IRQ_PRIMARY_ATA  14
#define IRQ_SECOND_ATA   15

/* -- Nomor interrupt yang ditangani kernel -- */
#define INT_PAGE_FAULT   0x0E
#define INT_SYSCALL      0x30

/* Jumlah stub di isr_stub_table (cpu/intsetup.s): 32 exception CPU + 32 IRQ / user defined */
#define ISR_STUB_TABLE_LIMIT 64

/**
 * CPURegister, store CPU registers values.
 * Urutan field mengikuti urutan memory hasil pushad + push segment di intsetup.s.
 * Koreksi Book 4.1.0: pushad menyimpan ebp di alamat lebih rendah daripada esp,
 * sehingga urutan stack adalah { ebp, esp } (kit lama terbalik { esp, ebp }).
 *
 * @param index   CPU index register (di, si)
 * @param stack   CPU stack register (bp, sp)
 * @param general CPU general purpose register (a, b, c, d)
 * @param segment CPU extra segment register (gs, fs, es, ds)
 */
struct CPURegister {
    struct {
        uint32_t edi;
        uint32_t esi;
    } __attribute__((packed)) index;
    struct {
        uint32_t ebp;
        uint32_t esp;
    } __attribute__((packed)) stack;
    struct {
        uint32_t ebx;
        uint32_t edx;
        uint32_t ecx;
        uint32_t eax;
    } __attribute__((packed)) general;
    struct {
        uint32_t gs;
        uint32_t fs;
        uint32_t es;
        uint32_t ds;
    } __attribute__((packed)) segment;
} __attribute__((packed));

/**
 * InterruptStack, data pushed by CPU when interrupt / exception is raised.
 * Refer to Intel x86 Vol 3a: Figure 6-4 Stack usage on transfer to Interrupt.
 *
 * @param error_code Error code that pushed with the exception (0 jika tidak ada)
 * @param eip        Instruction pointer where interrupt is raised
 * @param cs         Code segment selector where interrupt is raised
 * @param eflags     CPU eflags register when interrupt is raised
 */
struct InterruptStack {
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} __attribute__((packed));

/**
 * InterruptFrame, entirety of general CPU states exactly before interrupt.
 * Untuk interrupt dari ring 3 (inter-privilege), intsetup.s sudah mengganti
 * cpu.stack.esp dengan esp milik user (diambil dari stack x86 interrupt).
 * ss user tidak ada di struct ini, tetapi masih ada di stack tepat setelah eflags.
 *
 * @param cpu        CPU state
 * @param int_number Interrupt vector value
 * @param int_stack  Hardware-defined (x86) stack state
 */
struct InterruptFrame {
    struct CPURegister    cpu;
    uint32_t              int_number;
    struct InterruptStack int_stack;
} __attribute__((packed));

_Static_assert(sizeof(struct CPURegister) == 48, "CPURegister harus 48 byte");
_Static_assert(sizeof(struct InterruptFrame) == 68, "InterruptFrame harus 68 byte");

/**
 * Tabel alamat stub interrupt 0x00 - 0x3F, didefinisikan di cpu/intsetup.s.
 * Dipakai idt_init() untuk mengisi IDT.
 */
extern void *isr_stub_table[ISR_STUB_TABLE_LIMIT];

/**
 * Main interrupt handler when any interrupt / exception is raised.
 * DO NOT CALL THIS FUNCTION.
 *
 * Dipanggil oleh call_generic_handler (cpu/intsetup.s) untuk interrupt 0x00 - 0x3F,
 * lalu meneruskan ke handler yang sesuai (page fault, timer, keyboard, syscall).
 *
 * @param frame Information about CPU during interrupt is raised
 */
void main_interrupt_handler(struct InterruptFrame frame);

#endif
