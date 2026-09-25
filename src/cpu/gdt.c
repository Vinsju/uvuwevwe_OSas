#include "header/cpu/gdt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        // Entry 0: Null Descriptor
        {
            .segment_low = 0, .base_low = 0, .base_mid = 0,
            .type_bit = 0, .non_system = 0, .dpl = 0, .p_bit = 0,
            .segment_high = 0, .avl_bit = 0, .l_bit = 0, .db_bit = 0, .g_bit = 0,
            .base_high = 0
        },
        // Entry 1: Kernel Code Segment (Type: 0b1010 / 0xA)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA, .non_system = 1, .dpl = 0, .p_bit = 1,
            .segment_high = 0xF, .avl_bit = 0, .l_bit = 0, .db_bit = 1, .g_bit = 1,
            .base_high = 0
        },
        // Entry 2: Kernel Data Segment (Type: 0b0010 / 0x2)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2, .non_system = 1, .dpl = 0, .p_bit = 1,
            .segment_high = 0xF, .avl_bit = 0, .l_bit = 0, .db_bit = 1, .g_bit = 1,
            .base_high = 0
        }
    }
};

struct GDTR _gdt_gdtr = {
    .size = sizeof(struct GlobalDescriptorTable) - 1,
    .address = &global_descriptor_table
};