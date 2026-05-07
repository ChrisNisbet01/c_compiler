#include "type_utils.h"

#include "ast_node_name.h"
#include "debug.h"
#include "type_descriptors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char const *
extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node)
{
    if (type_spec_node == NULL)
    {
        return NULL;
    }

    if (type_spec_node->type == AST_NODE_STRUCT_TYPE_REF || type_spec_node->type == AST_NODE_UNION_TYPE_REF
        || type_spec_node->type == AST_NODE_ENUM_TYPE_REF)
    {
        c_grammar_node_t const * ident = type_spec_node->type_ref.identifier;
        return ident ? ident->text : NULL;
    }

    if (type_spec_node->type != AST_NODE_TYPE_SPECIFIER || type_spec_node->list.count == 0)
    {
        return NULL;
    }

    c_grammar_node_t const * spec_child = type_spec_node->list.children[0];
    c_grammar_node_t const * id_node = NULL;

    if (spec_child->type == AST_NODE_ENUM_DEFINITION)
    {
        id_node = spec_child->enum_definition.identifier;
    }
    else if (spec_child->type == AST_NODE_STRUCT_DEFINITION || spec_child->type == AST_NODE_UNION_DEFINITION)
    {
        id_node = spec_child->struct_definition.identifier;
    }
    else if (
        spec_child->type == AST_NODE_ENUM_TYPE_REF || spec_child->type == AST_NODE_STRUCT_TYPE_REF
        || spec_child->type == AST_NODE_UNION_TYPE_REF
    )
    {
        id_node = spec_child->type_ref.identifier;
    }

    if (id_node == NULL)
    {
        return NULL;
    }

    return id_node->text;
}

char const *
extract_typedef_name(c_grammar_node_t const * type_spec_node)
{
    if (type_spec_node == NULL || type_spec_node->type != AST_NODE_TYPEDEF_SPECIFIER
        || type_spec_node->identifier.identifier == NULL)
    {
        debug_info("%s: no name", __func__);
        return NULL;
    }
    debug_info("%s got name: %s", __func__, type_spec_node->identifier.identifier->text);
    return type_spec_node->identifier.identifier->text;
}

char *
generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix)
{
    char * name = malloc(64);
    sprintf(name, ".anon.%s.%d", prefix, ctx->anon_counter++);
    return name;
}

c_grammar_node_t const *
search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node)
{
    if (declarator_node == NULL)
    {
        return NULL;
    }
    c_grammar_node_t const * suffix_list = declarator_node->declarator.declarator_suffix_list;
    if (suffix_list->list.count == 0)
    {
        return NULL;
    }
    c_grammar_node_t const * suffix = suffix_list->list.children[0];
    if (suffix->type != AST_NODE_DECLARATOR_SUFFIX || suffix->list.count == 0)
    {
        return NULL;
    }
    c_grammar_node_t const * parameters_list = suffix->list.children[0];
    if (parameters_list->type != AST_NODE_PARAMETER_LIST)
    {
        return NULL;
    }

    return parameters_list;
}

c_grammar_node_t const *
extract_parameter_list(c_grammar_node_t const * suffix)
{
    if (suffix == NULL)
    {
        return NULL;
    }
    if (suffix->type == AST_NODE_PARAMETER_LIST)
    {
        return suffix;
    }
    if (suffix->list.count > 0)
    {
        c_grammar_node_t const * child = suffix->list.children[0];
        if (child->type == AST_NODE_PARAMETER_LIST)
        {
            return child;
        }
    }
    return NULL;
}

bool
is_function_suffix(c_grammar_node_t const * suffix)
{
    c_grammar_node_t const * param_list = extract_parameter_list(suffix);
    return param_list != NULL;
}

char const *
search_for_identifier(c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NULL;
    }

    if (node->type == AST_NODE_DECLARATOR)
    {
        char const * var_name = NULL;
        c_grammar_node_t const * direct_decl_node = node->declarator.direct_declarator;

        // For regular variables: DirectDeclarator -> Identifier
        // For function pointers: DirectDeclarator -> FunctionPointerDeclarator -> {Pointer, Identifier,
        // DeclaratorSuffix*}
        if (direct_decl_node && direct_decl_node->list.count > 0)
        {
            c_grammar_node_t * first_child = direct_decl_node->list.children[0];
            if (first_child->type == AST_NODE_IDENTIFIER)
            {
                var_name = first_child->text;
            }
            else if (first_child->type == AST_NODE_DECLARATOR)
            {
                // Nested declarator (e.g., for function pointers like *name)
                // Find the DirectDeclarator inside and get the Identifier
                c_grammar_node_t const * nested_direct = first_child->declarator.direct_declarator;
                if (nested_direct != NULL)
                {
                    char const * id = search_for_identifier(nested_direct);
                    if (id != NULL)
                    {
                        var_name = id;
                    }
                }
            }
            else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
            {
                // FunctionPointerDeclarator: contains Pointer, Identifier, DeclaratorSuffix*
                var_name = first_child->function_pointer_declarator.identifier->text;
            }
        }

        return var_name;
    }

    for (size_t i = 0; i < node->list.count; i++)
    {
        c_grammar_node_t * child = node->list.children[i];

        if (child->type == AST_NODE_IDENTIFIER && child->text != NULL)
        {
            return child->text;
        }
    }

    return NULL;
}

unsigned
get_fp_width(LLVMTypeRef type)
{
    LLVMTypeKind kind = LLVMGetTypeKind(type);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (kind)
    {
    case LLVMHalfTypeKind:
        return 16;
    case LLVMFloatTypeKind:
        return 32;
    case LLVMDoubleTypeKind:
        return 64;
    case LLVMX86_FP80TypeKind:
        return 80;
    case LLVMFP128TypeKind:
        return 128;
    default:
        return 0; // Not a floating-point type
    }

#pragma GCC diagnostic pop
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

    debug_info("Type kind %u has alignment: %u", LLVMGetTypeKind(type), alignment);

    return alignment;
}

// Helper function to get size in bytes for a type
uint64_t
get_type_size(ir_generator_ctx_t * ctx, TypeDescriptor const * type)
{
    uint64_t size_in_bytes = get_type_size_desc(ctx->data_layout, type);
    debug_info("type size: %llu", size_in_bytes);
    return size_in_bytes;
}

bool
function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2)
{
    return type1 == type2;
}

TypedValue
cast_typed_value_to_desc(ir_generator_ctx_t * ctx, TypedValue src, TypeDescriptor const * target_desc)
{
    debug_info("%s: in", __func__);

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
    debug_info("%s", __func__);
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
    debug_info(
        "builder: %p ty: %p, ptr: %p, name: %s (%p)", (void *)builder, (void *)ty, (void *)ptr, name, (void *)name
    );
    LLVMValueRef load = LLVMBuildLoad2(builder, ty, ptr, name);
    unsigned alignment = get_type_alignment(ctx, ty);
    LLVMSetAlignment(load, alignment);
    return load;
}
