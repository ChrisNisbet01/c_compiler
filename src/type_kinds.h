#pragma once

// --- Type kind for tagged types and typedef entries ---
typedef enum
{
    TYPE_KIND_BUILTIN,         // For typedefs that refer directly to builtin types (e.g., typedef int myint;)
    TYPE_KIND_STRUCT,          // Tagged struct
    TYPE_KIND_UNION,           // Tagged union
    TYPE_KIND_UNTAGGED_STRUCT, // Untagged struct (anonymous)
    TYPE_KIND_UNTAGGED_UNION,  // Untagged union (anonymous)
    TYPE_KIND_ENUM,            // Tagged enum
    TYPE_KIND_UNTAGGED_ENUM    // Untagged enum (anonymous)
} type_kind_t;
