# TypedValue to TypeDescriptor Refactoring Plan

## Vision
Work with TypeDescriptors everywhere possible, using LLVM types only when interfacing with LLVM libraries.
Eventually eliminate `map_type_to_llvm_t()` entirely - functions like `process_postfix_expression()` 
should work with TypeDescriptors, not LLVMTypeRef. When looking for pointees, use 
`type_desc->pointee` rather than rummaging around AST nodes.

## Background

### Current TypedValue Structure
```c
typedef struct {
    bool is_lvalue;
    bool is_unsigned;
    unsigned bit_width;
    unsigned bit_offset;
    LLVMValueRef value;
    TypeDescriptor const * type_info;  // The high-level type descriptor (already implemented)
    LLVMTypeRef type;                // The actual LLVM type (e.g., i32, struct.foo)
    LLVMTypeRef pointee_type;          // If it's a pointer, what does it point to?
} TypedValue;
```

The comment in `llvm_typed_value.h` says: "These will eventually be removed as 
all info will be found in the type_info."

### Current TypeDescriptor Structure
```c
typedef struct TypeDescriptor {
    type_descriptor_type_kind_t kind;
    LLVMTypeRef llvm_type;
    TypeQualifier qualifiers;
    TypeSpecifier specifiers;
    TypeDescriptor const * pointee;  // For pointers/arrays
    size_t array_size;                 // 0 for unsized (int[]), >0 for sized (int[10])
    FunctionMetadata function_metadata;
} TypeDescriptor;
```

## Implementation Phases

### Phase 1: Complete resolve_type_descriptor()
- Handle abstract declarators
- Handle function pointer declarators  
- Handle struct/union member declarations

### Phase 2: Add TypeDescriptor Helper Functions
Add to type_descriptors.h/.c:

```c
// Get pointee as TypeDescriptor (preferred)
TypeDescriptor const * type_descriptor_get_pointee(TypeDescriptor const * desc);

// Get pointee as LLVM type (for backward compatibility during transition)
LLVMTypeRef type_descriptor_get_pointee_llvm(TypedValue const * tv);

// Get LLVM type from TypeDescriptor
LLVMTypeRef type_descriptor_get_llvm_type(TypeDescriptor const * desc);

// Get TypeDescriptor from TypedValue
TypeDescriptor const * typed_value_get_descriptor(TypedValue const * tv);

// Get LLVM type from TypedValue (checks type_info first, falls back to .type)
LLVMTypeRef typed_value_get_llvm_type(TypedValue const * tv);

// Get pointee LLVM type from TypedValue (checks type_info first, falls back to .pointee_type)
LLVMTypeRef typed_value_get_pointee_llvm(TypedValue const * tv);
```

### Phase 3: Replace Manual TypedValue Creations
Replace all 12+ instances of manual `(TypedValue){.value = x, .type = y}` with 
`create_typed_value()` using appropriate TypeDescriptors from `get_or_create_*` functions.

Locations to update (approximate):
- line 2174: arr_type → use get_or_create_array_type()
- line 2184: null_type → use get_or_create_pointer_type() for void pointer
- line 2335: farr_type → use get_or_create_array_type()
- line 2342: i32_type → use get_or_create_builtin_type()
- line 2411: var_type → use map_type result's type_info
- line 2467: ptr_val.pointee_type → use typed_value_get_pointee_llvm()
- line 2814: sym_val → similar
- line 3172: param → similar
- line 5119, 5272, 5616: various

### Phase 4: Update TypedValue Creation Sites
Ensure all TypedValue instances are created via `create_typed_value()` which
correctly populates both `type_info` and the LLVM fallback fields.

### Phase 5: Replace pox_intee_type Usage
Replace all 42+ accesses to `.pointee_type` with:
- `typed_value_get_pointee_llvm()` for LLVM type
- `typed_value_get_descriptor()->pointee` for TypeDescriptor

### Phase 6-7: Continue Systematically
- Replace all 20 map_type_to_llvm_t() calls with resolve_type_descriptor()
- Add accessor functions to TypeDescriptor for type operations
- Eventually remove map_type_to_llvm_t() entirely

## Key Helpers to Use

### For Array Types
```c
TypeDescriptor const * get_or_create_array_type(
    TypeDescriptors * registry, 
    TypeDescriptor const * element_type, 
    size_t size
);
```

### For Pointer Types  
```c
TypeDescriptor const * get_or_create_pointer_type(
    TypeDescriptors * registry, 
    TypeDescriptor const * pointee, 
    TypeQualifier qualifiers
);
```

### For Builtin Types
```c
TypeDescriptor const * get_or_create_builtin_type(
    TypeDescriptors * registry, 
    TypeSpecifier specs, 
    TypeQualifier quals
);
```

## Testing Strategy
1. Ensure all 161+ tests pass at each step
2. Add specific tests for new helper functions
3. Test member access scenarios thoroughly

## Progress Tracking
- [ ] Phase 1: Complete resolve_type_descriptor()
- [ ] Phase 2: Add helper functions
- [ ] Phase 3: Replace manual TypedValue creations
- [ ] Phase 4: Update TypedValue creation sites
- [ ] Phase 5: Replace pointee_type usage
- [ ] Phase 6-7: Continue systematically