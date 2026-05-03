#pragma once

#include <stdint.h>

typedef struct
{
    unsigned bit_offset;
    unsigned bit_width;     // bit_width == 0 indicates this is not a bitfield or an unnamed bitfield
    unsigned storage_index; // -1 for regular fields, >=0 for bitfields = index of storage field
    uint64_t offset;        // offset from the start of the struct, allowing for padding.
} struct_bitfield_data_t;
