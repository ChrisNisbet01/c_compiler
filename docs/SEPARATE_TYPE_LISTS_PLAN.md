# Separate Type Lists Plan

## Overview

This plan implements proper scoping for struct/union/enum types and typedefs by using separate lists for each category.

## Problem

Currently, all types are stored in a single list and looked up by name only. This causes issues when:
- `struct Point` declared in one function, another `struct Point` in another function
- Typedefs and direct struct declarations conflict
- Type lookup doesn't distinguish between `struct Foo`, `enum Bar`, and `typedef name`

## Solution

Use separate lists for different kinds of type declarations, stored in each scope.

## Data Structures

### type_kind_t enum
```c
typedef enum {
    TYPE_KIND_PRIMITIVE,      // int, char, etc.
    TYPE_KIND_STRUCT,         // tagged struct
    TYPE_KIND_UNION,          // tagged union
    TYPE_KIND_ENUM,           // tagged enum
    TYPE_KIND_UNTAGGED_STRUCT,    // untagged struct
    TYPE_KIND_UNTAGGED_UNION,     // untagged union
    TYPE_KIND_UNTAGGED_ENUM      // untagged enum
} type_kind_t;
```

### scope_local_types_t (tagged structs/unions only)
```c
typedef struct scope_local_types
{
    struct_info_t * structs;  // struct and union declarations with tags
    size_t count;
    size_t capacity;
} scope_local_types_t;
```

### scope_enums_t (tagged enums only)
```c
typedef struct scope_enums
{
    struct_info_t * enums;  // enum declarations with tags
    size_t count;
    size_t capacity;
} scope_enums_t;
```

### scope_untagged_structs_t (anonymous structs)
```c
typedef struct scope_untagged_structs
{
    LLVMTypeRef * types;  // Anonymous struct types
    size_t count;
    size_t capacity;
} scope_untagged_structs_t;
```

### scope_typedefs_t (typedef names)
```c
typedef struct scope_typedef_entry
{
    char * name;              // The typedef's own name (e.g., "Point", "my_int")
    type_kind_t kind;         // Tells us which list to reference
    LLVMTypeRef type;        // Only for PRIMITIVE kinds
    char * tag;              // For tagged kinds - which entry in that list
    int untagged_index;      // For untagged kinds - index into untagged list
} scope_typedef_entry_t;

typedef struct scope_typedefs
{
    scope_typedef_entry_t * entries;
    size_t count;
    size_t capacity;
} scope_typedefs_t;
```

### Updated scope_t
```c
typedef struct scope
{
    symbol_t * symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    
    scope_local_types_t local_types;       // Tagged struct/union tags
    scope_enums_t enums;                  // Tagged enum tags
    scope_untagged_structs_t untagged_structs;  // Anonymous structs
    scope_typedefs_t typedefs;             // Typedef names
    
    struct scope * parent;
} scope_t;
```

## Registration Rules

| Declaration | kind | type | tag | untagged_index |
|------------|------|------|-----|----------------|
| `struct Point { ... };` | STRUCT | - | "Point" | - |
| `union Data { ... };` | UNTAGGED_STRUCT* | - | "Data" | - |
| `enum Color { ... };` | ENUM | - | "Color" | - |
| `struct Empty;` (forward) | STRUCT | - | "Empty" | - |
| `typedef struct { } Point;` | UNTAGGED_STRUCT | NULL | NULL | N |
| `typedef struct Foo Bar;` (forward) | STRUCT | NULL | "Foo" | - |
| `typedef struct Foo { } Bar;` | STRUCT | NULL | "Foo" | - |
| `typedef enum { RED } Color;` | UNTAGGED_ENUM | NULL | NULL | N |
| `typedef int my_int;` | PRIMITIVE | int type | NULL | - |

*Note: unions would use similar UNTAGGED_UNION kind

## Lookup Logic

| Syntax | How to Resolve |
|--------|----------------|
| `struct Point` | kind=STRUCT, look up "Point" in struct list |
| `union Data` | kind=UNTAGGED_UNION, look up by tag "Data" in untagged union list |
| `enum Color` | kind=ENUM, look up "Color" in enum list |
| `Point` (identifier, no keyword) | kind=PRIMITIVE/UNTAGGED_STRUCT/etc., resolve accordingly |

## Forward Declaration Handling

For `struct Empty;` (forward) followed by `struct Empty { ... };` (definition):
1. Create entry in struct list with tag name, zero members
2. When full definition encountered:
   - Find existing entry by tag name
   - If entry exists and has no members → update with new members
   - If entry already has members → error or ignore (redefinition)

## Implementation Steps

1. Add `type_kind_t` enum to `llvm_ir_generator.h`
2. Add `scope_untagged_structs_t` typedef to `llvm_ir_generator.h`
3. Update `scope_typedef_entry_t` to include kind, tag, untagged_index fields
4. Update `scope_typedefs_t` to use entries array
5. Update `scope_t` to include untagged_structs
6. Update `scope_create` to initialize all new lists
7. Update `scope_free` to free all new lists
8. Add helper functions for untagged structs
9. Update `register_struct_definition` to:
   - Handle forward declarations (create entry with zero members)
   - Update existing entries when full definition is provided
10. Update typedef handling to detect kind and store appropriately
11. Update lookup functions to use kind for correct list
12. Add tests for all typedef forms

## Coding Guidelines

Follow C_CODING_STYLE.md:
- East const: `char const * name`
- No implicit booleans: `if (ptr != NULL)`
- Always use braces
- Separate lookup from use
- sizeof with variable: `sizeof(*ptr)`
- Block comments `/* ... */`
- Include order: header, project, external, system

Follow .clang-format:
- 4 spaces, no tabs
- 120 char limit
- Allman braces
- Pointer alignment: middle
