#pragma once

#include "struct_members.h"
#include "type_kinds.h"

typedef struct TypeDescriptor TypeDescriptor;

// --- Type info for tagged/untagged types ---
typedef struct type_info
{
    char * tag;       // The tag name (e.g., "MyStruct"), or "" for anonymous structs/unions
    type_kind_t kind; // TYPE_KIND_STRUCT, TYPE_KIND_UNION, or TYPE_KIND_ENUM
    TypeDescriptor const * type_desc;
    struct_field_t * fields;
    size_t field_count;
} type_info_t;

// --- Typedef entry ---
typedef struct scope_typedef_entry
{
    char * name;                      // The typedef's own name
    type_kind_t kind;                 // Which category this refers to
    TypeDescriptor const * type_desc; /* For native types, nested typedefs and enums(e.g. int, char) */
    char const * tag;                 // For tagged kinds - which entry in struct/union/enum list
    int untagged_index;               // For untagged kinds - index into untagged list, -1 otherwise
} scope_typedef_entry_t;
