# Implementation Plan: TypedValue/TypeDescriptor Helpers

## Goal
Add helper functions to bridge between TypeDescriptors and LLVM types for TypedValue instances.
Start using these helpers in manual TypedValue creation sites.

## Step 1: Add Helper Functions to type_descriptors.h/.c

### Functions to Add
```c
// Get pointee as TypeDescriptor (preferred long-term)
TypeDescriptor const * type_descriptor_get_pointee(TypeDescriptor const * desc);

// Get pointee as LLVM type (for backward compatibility during transition)
// This function checks type_info first, falls back to pointee_type field
LLVMTypeRef typed_value_get_pointee_llvm(TypedValue const * tv);

// Get LLVM type from TypedValue (checks type_info first, falls back to .type field)
LLVMTypeRef typed_value_get_llvm_type(TypedValue const * tv);

// Get TypeDescriptor from TypedValue
TypeDescriptor const * typed_value_get_descriptor(TypedValue const * tv);
```

Note: The TypedValue-specific helpers need to be in llvm_typed_value.c since they access TypedValue struct.

## Step 2: Update Manual TypedValue Creations

### Locations to Update

#### Line 2174: __FILE__ macro
Current:
```c
TypedValue val = (TypedValue){.value = ptr, .type = arr_type};
```
Need: Create array TypeDescriptor first

#### Line 2184: NULL macro  
Current:
```c
TypedValue null_val = (TypedValue){.value = null_const, .type = null_type};
```
Need: Create pointer-to-void TypeDescriptor

#### Line 2335-2343: __LINE__, __func__ macros
Current:
```c
TypedValue fval = (TypedValue){.value = fptr, .type = farr_type};
TypedValue lval = (TypedValue){.value = line_const, .type = ctx->ref_type.i32_type};
```
Need: Create appropriate TypeDescriptors

#### Line 2411: Global variable
Current:
```c
TypedValue val = (TypedValue){
    .value = global_var,
    .type = var_type,
    .is_lvalue = true,
};
```
Need: Get TypeDescriptor from the type_info of map_type_to_llvm_t result

#### Line 2467: Assignment target (from original issue)
Current:
```c
lhs_res = (TypedValue){
    .value = target_addr,
    .type = ptr_val.pointee_type,
    .is_lvalue = true
};
```
Need: Use typed_value_get_pointee_llvm() instead

#### Line 2814: Symbol value
Similar to line 2411

#### Line 3172: Parameter value
Similar - need to create param TypeDescriptor

#### Line 5119: Base value for member access
Current uses LLVM type directly

#### Line 5272: Compound literal result
Current uses LLVM type directly

#### Line 5616: Function value
Current uses LLVM type from LLVMGlobalGetValueType

## Implementation Order

1. Add type_descriptor_get_pointee() to type_descriptors.c
2. Add typed_value_get_* helpers to llvm_typed_value.c  
3. Update each TypedValue creation site one by one
4. Run tests after each change
5. Commit when all changes in this plan are done

## Testing
- Run full test suite after each TypedValue site update
- Ensure all 161 tests pass