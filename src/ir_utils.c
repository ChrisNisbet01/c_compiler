#include "ir_utils.h"

#include <stdlib.h>
#include <string.h>

char *
generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix)
{
    char * name = malloc(64);
    sprintf(name, ".anon.%s.%d", prefix, ctx->anon_counter++);
    return name;
}

uint64_t
get_type_alignment(ir_generator_ctx_t * ctx, LLVMTypeRef type)
{
    if (type == NULL || LLVMGetTypeKind(type) == LLVMVoidTypeKind)
    {
        return 1;
    }

    // 2. Query the Preferred (or ABI) alignment directly as a number
    // LLVMABIAlignmentOfType returns the actual unsigned int you want.
    uint64_t alignment = LLVMABIAlignmentOfType(ctx->data_layout, type);

    return alignment;
}

// Helper function to get size in bytes for a type
uint64_t
get_type_size(ir_generator_ctx_t * ctx, TypeDescriptor const * type)
{
    uint64_t size_in_bytes = get_type_size_desc(ctx->data_layout, type);

    return size_in_bytes;
}

TypedValue
cast_typed_value_to_desc(ir_generator_ctx_t * ctx, TypedValue src, TypeDescriptor const * target_desc)
{
    // 1. Ensure we are working with the actual value, not its address
    src = ensure_rvalue(ctx, "cast_rval", src);

    if (src.value == NULL || target_desc == NULL || src.type_info == target_desc)
    {
        return src;
    }

    LLVMTypeRef target_llvm_type = target_desc->llvm_type;
    LLVMValueRef value = src.value;

    type_descriptor_type_kind_t src_kind = src.type_info->kind;
    type_descriptor_type_kind_t target_kind = target_desc->kind;
    bool src_is_int = is_integer_kind(src.type_info);
    bool target_is_int = is_integer_kind(target_desc);
    bool src_is_float = is_floating_kind(src.type_info);
    bool target_is_float = is_floating_kind(target_desc);

    // --- 1. Integer to Integer ---
    if (src_is_int && target_is_int)
    {
        unsigned src_bits = src.type_info->integer_metadata.width;
        unsigned target_bits = target_desc->integer_metadata.width;
        bool is_signed = !src.type_info->specifiers.is_unsigned;

        if (target_bits < src_bits)
        {
            value = LLVMBuildTrunc(ctx->builder, value, target_llvm_type, "trunc_tmp");
        }
        else if (target_bits > src_bits)
        {
            value = is_signed ? LLVMBuildSExt(ctx->builder, value, target_llvm_type, "sext_tmp")
                              : LLVMBuildZExt(ctx->builder, value, target_llvm_type, "zext_tmp");
        }
        return create_typed_value(value, target_desc, false);
    }

    // --- 2. Floating Point to Floating Point ---
    if (src_is_float && target_is_float)
    {
        unsigned src_bits = src.type_info->float_metadata.width;
        unsigned target_bits = target_desc->float_metadata.width;

        if (src_bits > target_bits)
            value = LLVMBuildFPTrunc(ctx->builder, value, target_llvm_type, "fptrunc_tmp");
        else if (src_bits < target_bits)
            value = LLVMBuildFPExt(ctx->builder, value, target_llvm_type, "fpext_tmp");

        return create_typed_value(value, target_desc, false);
    }

    // --- 3. Integer to Floating Point ---
    if (src_is_int && target_is_float)
    {
        bool is_signed = !src.type_info->specifiers.is_unsigned;

        value = is_signed ? LLVMBuildSIToFP(ctx->builder, value, target_llvm_type, "sitofp_tmp")
                          : LLVMBuildUIToFP(ctx->builder, value, target_llvm_type, "uitofp_tmp");
        return create_typed_value(value, target_desc, false);
    }

    // --- 4. Floating Point to Integer ---
    if (src_is_float && target_is_int)
    {
        bool is_signed = !src.type_info->specifiers.is_unsigned;

        value = is_signed ? LLVMBuildFPToSI(ctx->builder, value, target_llvm_type, "fptosi_tmp")
                          : LLVMBuildFPToUI(ctx->builder, value, target_llvm_type, "fptoui_tmp");
        return create_typed_value(value, target_desc, false);
    }

    // --- 5. Pointer Conversions (Pointer-to-Pointer, Int-to-Ptr, Ptr-to-Int) ---
    // Note: In modern LLVM, BitCast is often unnecessary for 'ptr', but
    // keep it for explicit type tracking in your TypedValue.
    if (src_kind == NCC_TYPE_KIND_POINTER && target_kind == NCC_TYPE_KIND_POINTER)
    {
        // No instruction needed for opaque pointers, just update the metadata
        return create_typed_value(value, target_desc, false);
    }

    if (src_kind == NCC_TYPE_KIND_POINTER && target_is_int)
    {
        // When converting pointer to bool (i1), compare to null instead of ptrtoint
        if (LLVMGetIntTypeWidth(target_llvm_type) == 1)
        {
            LLVMValueRef null_ptr = LLVMConstNull(LLVMTypeOf(value));
            value = LLVMBuildICmp(ctx->builder, LLVMIntNE, value, null_ptr, "ptr_is_nonnull");
            return create_typed_value(value, target_desc, false);
        }
        value = LLVMBuildPtrToInt(ctx->builder, value, target_llvm_type, "ptrtoint_tmp");
        return create_typed_value(value, target_desc, false);
    }

    if (src_is_int && target_kind == NCC_TYPE_KIND_POINTER)
    {
        value = LLVMBuildIntToPtr(ctx->builder, value, target_llvm_type, "inttoptr_tmp");
        return create_typed_value(value, target_desc, false);
    }

    // --- 6. Array to Pointer Decay (Implicit) ---
    if (src_kind == NCC_TYPE_KIND_ARRAY && target_kind == NCC_TYPE_KIND_POINTER)
    {
        // GEP to element 0
        LLVMValueRef indices[2]
            = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};
        value = LLVMBuildInBoundsGEP2(ctx->builder, src.type_info->llvm_type, value, indices, 2, "decay_ptr");
        return create_typed_value(value, target_desc, false);
    }

    return src;
}

// Helper wrapper for LLVMBuildStore with proper alignment
void
aligned_store(
    ir_generator_ctx_t * ctx, LLVMBuilderRef builder, LLVMValueRef value, LLVMTypeRef value_type, LLVMValueRef ptr
)
{
    if (value == NULL || value_type == NULL || ptr == NULL)
    {
        debug_error(
            "aligned_store: NULL value/type/ptr passed (%p:%p:%p)", (void *)value, (void *)value_type, (void *)ptr
        );
        return;
    }
    LLVMValueRef store = LLVMBuildStore(builder, value, ptr);
    unsigned alignment = get_type_alignment(ctx, value_type);
    LLVMSetAlignment(store, alignment);
}

// Helper wrapper for LLVMBuildLoad2 with proper alignment
LLVMValueRef
aligned_load(ir_generator_ctx_t * ctx, LLVMBuilderRef builder, LLVMTypeRef ty, LLVMValueRef ptr, char const * name)
{
    if (ty == NULL)
    {
        debug_error("aligned_load: NULL type passed");
        return NULL;
    }
    if (ptr == NULL)
    {
        debug_error("aligned_load: NULL ptr passed");
        return NULL;
    }

    LLVMValueRef load = LLVMBuildLoad2(builder, ty, ptr, name);
    unsigned alignment = get_type_alignment(ctx, ty);
    LLVMSetAlignment(load, alignment);
    return load;
}
