#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

#define GDT_MAX_ENTRY_COUNT 32
#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x8
#define GDT_KERNEL_DATA_SEGMENT_SELECTOR 0x10

extern struct GDTR _gdt_gdtr;
void load_gdt(struct GDTR *gdtr);

struct SegmentDescriptor {
    // First 32-bit
    uint16_t segment_low;
    uint16_t base_low;

    // Next 16-bit (Bit 32 to 47)
    uint8_t base_mid;
    uint8_t type_bit   : 4;
    uint8_t non_system : 1;
    uint8_t dpl        : 2;
    uint8_t p_bit      : 1;

    // Next 16-bit (Bit 48 to 63)
    uint8_t segment_high : 4;
    uint8_t avl_bit      : 1;
    uint8_t l_bit        : 1;
    uint8_t db_bit       : 1;
    uint8_t g_bit        : 1;
    uint8_t base_high;
} __attribute__((packed));

struct GlobalDescriptorTable {
    struct SegmentDescriptor table[GDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

struct GDTR {
    uint16_t                     size;
    struct GlobalDescriptorTable *address;
} __attribute__((packed));

#endif