# Implementation Plan: Separate Type Lists for Structs and Typedefs

## Overview
This plan implements proper scoping and lookup for struct/union types and typedefs by using separate lists.

## Current State
- 7 of 8 new tests pass
- 1 test fails: `test_mixed_struct_typedef.c` - mixing `struct Point` and `TaggedPoint` typedef usage

## Root Cause
Currently, typedefs and tagged structs use the same lookup mechanism. When using `TaggedPoint` (typedef), it should look in typedef list, not struct list.

## Implementation Steps

### Phase 1: Data Structures

1. **Add type_kind_t enum** to `llvm_ir_generator.h`
   - TYPE_KIND_STRUCT (tagged struct)
   - TYPE_KIND_UNTAGGED_STRUCT (untagged struct)
   - TYPE_KIND_UNTAGGED_UNION (untagged union)

2. **Add scope_untagged_structs_t** to `llvm_ir_generator.h`
   - Array of LLVMTypeRef for anonymous structs/unions

3. **Update scope_typedef_entry_t** to include:
   - kind: which list to reference
   - tag: for tagged types
   - untagged_index: for untagged types

### Phase 2: Registration Logic

4. **Update register_struct_definition()**
   - Handle forward declarations (create entry with no members)
   - Handle full definitions (update existing if forward decl exists)

5. **Update typedef registration in AST_NODE_TYPEDEF_DECLARATION handler**
   - Detect which case based on AST:
     - `typedef struct { } Name;` → UNTAGGED_STRUCT
     - `typedef struct Tag Name;` (forward) → STRUCT with tag
     - `typedef struct Tag { } Name;` → STRUCT with tag
     - `typedef union { } Name;` → UNTAGGED_UNION

### Phase 3: Lookup Logic

6. **Update type extraction** in `extract_compound_type_name()`
   - Detect presence of `struct`/`union` keyword
   - Return kind information

7. **Update all find_struct_type() call sites**
   - If keyword present → search struct list
   - If no keyword → search typedef list

### Phase 4: Testing

8. **Run existing test suite** (91 tests)
9. **Run new tests** (8 tests in not_yet/)

## Files to Modify
- src/llvm_ir_generator.h - data structures
- src/llvm_ir_generator.c - registration and lookup logic

## Success Criteria
- All 91 existing tests pass
- All 8 new tests pass:
  - test_tagged_struct.c
  - test_untagged_struct_typedef.c
  - test_tagged_typedef_same_name.c
  - test_tagged_typedef_different_name.c
  - test_struct_same_name_different_scope.c
  - test_mixed_struct_typedef.c (currently failing)
  - test_typedef_tagged_forward.c
  - test_untagged_union_typedef.c
