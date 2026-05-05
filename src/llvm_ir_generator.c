#include "llvm_ir_generator.h"

#include "ast_node_name.h"
#include "ast_print.h"
#include "c_grammar_ast.h"
#include "debug.h"
#include "declaration_handler.h"
#include "enum_evaluation.h"
#include "generator_lists.h"
#include "type_utils.h"

// Helper function to get natural alignment for a type
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TypedValue NullTypedValue;

// Forward declarations for functions used before definition

static void process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);

static TypedValue _process_expression_impl(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, int line);
#define process_expression(c, n) _process_expression_impl((c), (n), __LINE__);

char const * extract_typedef_name(c_grammar_node_t const * type_spec_node);
char const * extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node);
char * generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix);
bool is_function_suffix(c_grammar_node_t const * suffix);
c_grammar_node_t const * extract_parameter_list(c_grammar_node_t const * suffix);
c_grammar_node_t const * search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node);

static type_info_t const * register_tagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * type_child,
    char const * tag,
    type_kind_t kind,
    TypeQualifier quals
);

static type_info_t const * register_untagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, type_kind_t kind, int * new_type_id
);

static char const * search_ast_for_type_tag(c_grammar_node_t const * definition_node);

static type_info_t const *
register_tagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node, char const * tag);

static type_info_t const *
register_untagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node, int * new_enum_id);

static TypedValue get_variable_pointer(ir_generator_ctx_t * ctx, c_grammar_node_t const * identifier_node);

// --- Function declaration tracking ---

static struct function_decl_entry *
find_function_declaration(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL || name == NULL)
    {
        return NULL;
    }
    debug_info("%s: %s", __func__, name);

    for (size_t i = 0; i < ctx->function_declarations.count; ++i)
    {
        struct function_decl_entry * entry = &ctx->function_declarations.entries[i];
        if (entry->name != NULL && strcmp(entry->name, name) == 0)
        {
            debug_info("found");
            LLVMValueRef func = LLVMGetNamedFunction(ctx->module, name);
            if (entry->func.value == NULL)
            {
                entry->func = create_typed_value(func, entry->func.type_info, false);
            }

            return entry;
        }
    }

    debug_info("not found");
    return NULL;
}

static bool
add_function_declaration(ir_generator_ctx_t * ctx, char const * name, TypedValue func, bool has_definition)
{
    if (ctx == NULL || name == NULL || func.type_info == NULL)
    {
        debug_error("%s: Invalid arguments", __func__);

        return false;
    }

    debug_info("%s: name='%s' has_definition=%d", __func__, name, has_definition);
    dump_typed_value("func", func);

    // Check if function already exists
    struct function_decl_entry * existing = find_function_declaration(ctx, name);

    if (existing != NULL)
    {
        // Function already declared - check for signature mismatch
        if (!function_signatures_match(
                existing->func.type_info->pointee->llvm_type, func.type_info->pointee->llvm_type
            ))
        {
            debug_warning("function signature mismatch");
            return true; // Conflict detected
        }

        // Check for redefinition
        if (existing->has_definition && has_definition)
        {
            debug_warning("function redefinition");
            return true; // Redefinition detected
        }

        // Update definition status
        if (has_definition && !existing->has_definition)
        {
            existing->has_definition = true;
        }

        return false; // No conflict
    }

    // Add new function declaration
    if (ctx->function_declarations.count >= ctx->function_declarations.capacity)
    {
        size_t new_cap = ctx->function_declarations.capacity == 0 ? 4 : ctx->function_declarations.capacity * 2;
        struct function_decl_entry * new_entries
            = realloc(ctx->function_declarations.entries, new_cap * sizeof(*new_entries));
        if (new_entries == NULL)
        {
            return false;
        }
        ctx->function_declarations.entries = new_entries;
        ctx->function_declarations.capacity = new_cap;
    }

    ctx->function_declarations.entries[ctx->function_declarations.count].name = strdup(name);
    ctx->function_declarations.entries[ctx->function_declarations.count].func = func;
    ctx->function_declarations.entries[ctx->function_declarations.count].has_definition = has_definition;
    ctx->function_declarations.count++;

    debug_info("added function: %s, count now: %zu", name, ctx->function_declarations.count);

    return false;
}

// Helper wrapper for LLVMBuildStore with proper alignment
static void
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
static LLVMValueRef
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

static TypedValue
ensure_rvalue(ir_generator_ctx_t * ctx, char const * label, TypedValue val)
{
    debug_info("%s: is %s, lvalue: %d", __func__, label, val.is_lvalue);
    dump_typed_value(label, val);
    if (val.value == NULL)
    {
        debug_warning("no value!");
        return val;
    }
    if (!val.is_lvalue)
    {
        debug_info("already rvalue");
        return val; // Already data
    }

    if (val.type_info->kind == NCC_TYPE_KIND_ARRAY)
    {
        // We don't load the array. We GEP to its first element to get a pointer.
        LLVMValueRef indices[2]
            = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};

        LLVMValueRef decayed_ptr
            = LLVMBuildInBoundsGEP2(ctx->builder, val.type_info->llvm_type, val.value, indices, 2, "decay_ptr");

        // Return the address as an R-value pointer
        TypeDescriptor const * ptr_to_elem
            = get_or_create_pointer_type(ctx->type_descriptors, val.type_info->pointee, (TypeQualifier){0});
        return create_typed_value(decayed_ptr, ptr_to_elem, false);
    }

    // Convert Lvalue to Rvalue by performing the load
    LLVMValueRef container = aligned_load(ctx, ctx->builder, val.type_info->llvm_type, val.value, label);

    // 2. If it's a bitfield, extract the bits
    if (val.bitfield.bit_width > 0)
    {
        LLVMValueRef offset = LLVMConstInt(ctx->ref_type.i32_type, val.bitfield.bit_offset, false);
        LLVMValueRef mask = LLVMConstInt(ctx->ref_type.i32_type, (1ULL << val.bitfield.bit_width) - 1, false);
        LLVMValueRef shifted = LLVMBuildLShr(ctx->builder, container, offset, "bf_extract");
        container = LLVMBuildAnd(ctx->builder, shifted, mask, "bf_val");
    }

    val.value = container;
    val.is_lvalue = false;

    return val;
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

static TypedValue
process_array_subscript(ir_generator_ctx_t * ctx, c_grammar_node_t const * subscript_node, TypedValue base)
{
    if (ctx == NULL || base.value == NULL || base.type_info == NULL || subscript_node == NULL)
    {
        return NullTypedValue;
    }

    // 1. Process the index expression
    TypedValue index_res = NullTypedValue;
    if (subscript_node->list.count >= 1)
    {
        c_grammar_node_t * index_node = subscript_node->list.children[0];
        index_res = process_expression(ctx, index_node);
        index_res = ensure_rvalue(ctx, "subscript_index", index_res);
    }

    if (index_res.value == NULL)
    {
        return NullTypedValue;
    }

    TypeDescriptor const * base_desc = base.type_info;
    TypeDescriptor const * elem_desc = base_desc->pointee;
    /* TODO: should support 5[a] as well as a[5]. Just a matter of swapping bases and desc. */

    // Safety check: pointer or array must have a pointee
    if (elem_desc == NULL)
    {
        ir_gen_error(&ctx->errors, subscript_node, "Subscripted value is not an array or pointer");
        return NullTypedValue;
    }

    LLVMValueRef elem_ptr = NULL;

    // 2. Build GEP based on base type kind
    if (base_desc->kind == NCC_TYPE_KIND_POINTER)
    {
        // POINTER PATH: e.g., char *p; p[i]
        // Pointer is already an RVALUE (the address).
        // We use a 1-index GEP: ptr_val + index
        TypedValue ptr_rval = ensure_rvalue(ctx, "subscript_ptr_base", base);

        elem_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, elem_desc->llvm_type, ptr_rval.value, &index_res.value, 1, "arrayidx"
        );
    }
    else if (base_desc->kind == NCC_TYPE_KIND_ARRAY)
    {
        // ARRAY PATH: e.g., char a[10]; a[i]
        // Array base is usually an LVALUE (the address of the block).
        // We use a 2-index GEP: base_ptr + 0 offset + index
        LLVMValueRef indices[2];
        indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
        indices[1] = index_res.value;

        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, base_desc->llvm_type, base.value, indices, 2, "arrayidx");
    }
    else
    {
        ir_gen_error(&ctx->errors, subscript_node, "Type cannot be subscripted");
        return NullTypedValue;
    }

    // 3. Return the element as an LVALUE (so we can assign to it: a[i] = x)
    return create_typed_value(elem_ptr, elem_desc, true);
}

static void
process_initializer_list(
    ir_generator_ctx_t * ctx,
    LLVMValueRef base_ptr,
    TypeDescriptor const * desc,
    c_grammar_node_t const * initializer_node,
    int * outer_index
)
{
    if (initializer_node == NULL || base_ptr == NULL || desc == NULL)
        return;

    // 1. Zero-initialize for safety (especially for structs with padding or omitted fields)
    if (desc->kind == NCC_TYPE_KIND_STRUCT)
    {
        LLVMValueRef size = LLVMSizeOf(desc->llvm_type);
        LLVMValueRef zero = LLVMConstNull(ctx->ref_type.i8_type);
        uint32_t align = get_type_alignment_desc(desc);
        LLVMBuildMemSet(ctx->builder, base_ptr, zero, size, align);
    }

    int local_index = 0;

    for (size_t i = 0; i < initializer_node->list.count; i++)
    {
        c_grammar_node_t const * list_entry = initializer_node->list.children[i];
        c_grammar_node_t const * value_node = list_entry->initializer_list_entry.initializer;
        c_grammar_node_t const * designation = list_entry->initializer_list_entry.designation;

        // Unwrap value from INITIALIZER wrapper
        if (value_node->list.count > 0) /* Should always be true. */
        {
            value_node = value_node->list.children[0];
        }

        // 2. Handle Designated Initializers (e.g., .x = 10 or .inner.y = 20)
        if (designation != NULL)
        {
            LLVMValueRef current_ptr = base_ptr;
            TypeDescriptor const * current_desc = desc;

            for (size_t d = 0; d < designation->list.count; d++)
            {
                c_grammar_node_t const * field_ident = designation->list.children[d];
                if (field_ident->type == AST_NODE_IDENTIFIER && field_ident->text != NULL)
                {
                    int field_idx = type_descriptor_find_struct_field_index_from_desc(current_desc, field_ident->text);
                    if (field_idx < 0)
                    {
                        break;
                    }
                    struct_field_t const * field = type_descriptor_get_struct_field_type(current_desc, field_idx);
                    TypeDescriptor const * field_desc = field->type_desc;

                    LLVMValueRef indices[2]
                        = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                           LLVMConstInt(ctx->ref_type.i32_type, field->bitfield.storage_index, false)};

                    LLVMValueRef next_ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, current_desc->llvm_type, current_ptr, indices, 2, "designator_ptr"
                    );

                    current_desc = field_desc;
                    current_ptr = next_ptr;
                }
            }

            if (value_node->type == AST_NODE_INITIALIZER_LIST) /* Nested intializers. */
            {
                process_initializer_list(ctx, current_ptr, current_desc, value_node, NULL);
            }
            else
            {
                TypedValue tval = process_expression(ctx, value_node);
                TypedValue cast_val = cast_typed_value_to_desc(ctx, tval, current_desc);
                aligned_store(ctx, ctx->builder, cast_val.value, current_desc->llvm_type, current_ptr);
            }

            local_index++; // Update positional tracker
            if (outer_index)
                (*outer_index)++;
            continue;
        }

        // 3. Handle Positional Initializers (Standard list)
        TypeDescriptor const * target_desc = NULL;
        LLVMValueRef target_ptr = NULL;

        if (desc->kind == NCC_TYPE_KIND_ARRAY)
        {
            target_desc = desc->pointee;
            LLVMValueRef indices[2]
                = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                   LLVMConstInt(ctx->ref_type.i32_type, local_index, false)};
            target_ptr = LLVMBuildInBoundsGEP2(ctx->builder, desc->llvm_type, base_ptr, indices, 2, "array_init_ptr");
        }
        else if (desc->kind == NCC_TYPE_KIND_STRUCT)
        {
            if (local_index < (int)desc->struct_metadata.members.num_members)
            {
                target_desc = desc->struct_metadata.members.members[local_index].type_desc;
                unsigned storage_idx = desc->struct_metadata.members.members[local_index].bitfield.storage_index;

                LLVMValueRef indices[2]
                    = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                       LLVMConstInt(ctx->ref_type.i32_type, storage_idx, false)};
                target_ptr
                    = LLVMBuildInBoundsGEP2(ctx->builder, desc->llvm_type, base_ptr, indices, 2, "struct_init_ptr");
            }
        }

        if (target_ptr && target_desc)
        {
            if (value_node->type == AST_NODE_INITIALIZER_LIST)
            {
                process_initializer_list(ctx, target_ptr, target_desc, value_node, NULL);
            }
            else
            {
                TypedValue tval = process_expression(ctx, value_node);
                TypedValue cast_val = cast_typed_value_to_desc(ctx, tval, target_desc);
                aligned_store(ctx, ctx->builder, cast_val.value, target_desc->llvm_type, target_ptr);
            }
        }

        local_index++;
        if (outer_index)
            (*outer_index)++;
    }
}

static void
process_initializer_list_type_desc(
    ir_generator_ctx_t * ctx,
    LLVMValueRef base_ptr,
    TypeDescriptor const * type_desc,
    c_grammar_node_t const * initializer_node,
    int * outer_index
)
{
    if (initializer_node == NULL || base_ptr == NULL || type_desc == NULL)
    {
        return;
    }

    LLVMTypeRef element_type = type_desc->llvm_type;
    type_descriptor_type_kind_t kind = type_desc->kind;

    // For structs, zero-initialize (C standard requirement for omitted members)
    if (kind == NCC_TYPE_KIND_STRUCT)
    {
        LLVMValueRef size = LLVMSizeOf(element_type);
        LLVMValueRef zero = LLVMConstNull(ctx->ref_type.i8_type);
        // Use TypeDescriptor's knowledge of alignment if available, or fallback
        unsigned alignment = get_type_alignment_desc(type_desc);
        LLVMBuildMemSet(ctx->builder, base_ptr, zero, size, alignment);
    }

    int local_index = 0;

    for (size_t i = 0; i < initializer_node->list.count; ++i)
    {
        c_grammar_node_t const * list_entry = initializer_node->list.children[i];
        c_grammar_node_t const * value_node = list_entry->initializer_list_entry.initializer;
        c_grammar_node_t const * designation = list_entry->initializer_list_entry.designation;

        // Unwrap value from INITIALIZER wrapper
        if (value_node->list.count > 0) /* Should always be true. */
        {
            value_node = value_node->list.children[0];
        }

        if (designation != NULL)
        {
            LLVMValueRef current_ptr = base_ptr;
            TypeDescriptor const * current_desc = type_desc;

            int final_field_idx = -1;
            TypeDescriptor const * final_type_desc = NULL;

            // Resolve Designation Path (e.g., .pos.x)
            for (size_t d = 0; d < designation->list.count; d++)
            {
                c_grammar_node_t const * field_ident = designation->list.children[d];

                if (field_ident->type == AST_NODE_IDENTIFIER)
                {
                    char const * field_name = field_ident->text;

                    // Use your new TypeDescriptor metadata to find field
                    int field_idx = type_descriptor_find_struct_field_index_from_desc(current_desc, field_name);

                    if (field_idx < 0)
                    {
                        debug_error("failed to find field '%s' index", field_name);
                        break;
                    }

                    struct_field_t const * field = type_descriptor_get_struct_field_type(current_desc, field_idx);
                    TypeDescriptor const * field_desc = field->type_desc;

                    if (field == NULL)
                    {
                        debug_error("failed to find field '%s' type descriptor", field_name);
                        break;
                    }

                    if (d + 1 < designation->list.count)
                    {
                        // Navigate deeper into nested struct
                        LLVMValueRef indices[2]
                            = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                               LLVMConstInt(ctx->ref_type.i32_type, field->bitfield.storage_index, false)};
                        current_ptr = LLVMBuildInBoundsGEP2(
                            ctx->builder, current_desc->llvm_type, current_ptr, indices, 2, "nested_ptr"
                        );
                        current_desc = field_desc;
                    }
                    else
                    {
                        final_field_idx = field_idx;
                        final_type_desc = field_desc;
                    }
                }
            }

            if (final_type_desc != NULL)
            {
                if (value_node->type == AST_NODE_INITIALIZER_LIST)
                {
                    // Recursively process nested list at the specific field pointer
                    LLVMValueRef indices[2]
                        = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                           LLVMConstInt(ctx->ref_type.i32_type, final_field_idx, false)};
                    LLVMValueRef field_ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, current_desc->llvm_type, current_ptr, indices, 2, "init_field_ptr"
                    );
                    process_initializer_list_type_desc(ctx, field_ptr, final_type_desc, value_node, NULL);
                }
                else
                {
                    // Store simple value with a cast
                    TypedValue tvalue = process_expression(ctx, value_node);
                    LLVMValueRef indices[2]
                        = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                           LLVMConstInt(ctx->ref_type.i32_type, final_field_idx, false)};
                    LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, current_desc->llvm_type, current_ptr, indices, 2, "init_ptr"
                    );

                    // Modern cast uses the descriptor
                    TypedValue cast_value = cast_typed_value_to_desc(ctx, tvalue, final_type_desc);
                    aligned_store(ctx, ctx->builder, cast_value.value, cast_value.type_info->llvm_type, elem_ptr);
                }
            }
            else
            {
                debug_warning("%s: no final type!", __func__);
            }
            local_index++;
            if (outer_index)
                (*outer_index)++;
            continue;
        }

        // --- Non-designated Logic ---

        TypeDescriptor const * target_desc = NULL;
        if (kind == NCC_TYPE_KIND_ARRAY)
        {
            target_desc = type_desc->pointee; // Array element type
        }
        else if (kind == NCC_TYPE_KIND_STRUCT)
        {
            target_desc = type_descriptor_get_struct_field_type(type_desc, local_index)->type_desc;
        }

        if (target_desc == NULL)
        {
            break;
        }

        if (value_node->type == AST_NODE_INITIALIZER_LIST)
        {
            LLVMValueRef indices[2]
                = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                   LLVMConstInt(ctx->ref_type.i32_type, local_index, false)};
            LLVMValueRef elem_ptr
                = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "nested_ptr");
            process_initializer_list_type_desc(ctx, elem_ptr, target_desc, value_node, NULL);
        }
        else
        {
            TypedValue tvalue = process_expression(ctx, value_node);
            LLVMValueRef indices[2]
                = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                   LLVMConstInt(ctx->ref_type.i32_type, local_index, false)};
            LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");

            TypedValue cast_value = cast_typed_value_to_desc(ctx, tvalue, target_desc);
            aligned_store(ctx, ctx->builder, cast_value.value, target_desc->llvm_type, elem_ptr);
        }

        local_index++;
        if (outer_index)
            (*outer_index)++;
    }
}

static char const *
decode_string(char const * const src)
{
    if (src == NULL)
    {
        return NULL;
    }

    size_t const len = strlen(src);
    char * const decoded = malloc(len + 1);
    if (decoded == NULL)
    {
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;
    while (i < len)
    {
        if (src[i] == '\\' && i + 1 < len)
        {
            switch (src[i + 1])
            {
            case 'n':
                decoded[j++] = '\n';
                break;
            case 't':
                decoded[j++] = '\t';
                break;
            case 'r':
                decoded[j++] = '\r';
                break;
            case '0':
                decoded[j++] = '\0';
                break;
            case '\\':
                decoded[j++] = '\\';
                break;
            case '\"':
                decoded[j++] = '\"';
                break;
            case '\'':
                decoded[j++] = '\'';
                break;
            default:
                decoded[j++] = src[i + 1];
                break;
            }
            i += 2;
        }
        else
        {
            decoded[j++] = src[i++];
        }
    }
    decoded[j] = '\0';
    return decoded;
}

// --- IR Generator Context Initialization and Disposal ---

static c_grammar_node_t const *
find_typedef_name_node(c_grammar_node_t const * typedef_decl)
{
    if (!typedef_decl || typedef_decl->type != AST_NODE_TYPEDEF_DECLARATOR)
    {
        return NULL;
    }

    c_grammar_node_t const * direct_decl = typedef_decl->typedef_declarator.direct_declarator;

    if (direct_decl->type == AST_NODE_IDENTIFIER)
    {
        return direct_decl;
    }

    if (direct_decl->type == AST_NODE_TYPEDEF_DECLARATOR)
    {
        return find_typedef_name_node(direct_decl);
    }

    if (direct_decl->type == AST_NODE_TYPEDEF_DIRECT_DECLARATOR)
    {
        // Check identifier (TypedefDefiningIdentifier)
        if (direct_decl->typedef_direct_declarator.identifier != NULL)
        {
            return direct_decl->typedef_direct_declarator.identifier;
        }
        // Check nested typedef declarator (wrapped in parens)
        if (direct_decl->typedef_direct_declarator.nested_typedef_declarator != NULL)
        {
            return find_typedef_name_node(direct_decl->typedef_direct_declarator.nested_typedef_declarator);
        }
    }

    return NULL;
}

static char const *
search_ast_for_type_tag(c_grammar_node_t const * definition_node)
{
    if (definition_node == NULL)
    {
        return NULL;
    }

    if (definition_node->type == AST_NODE_ENUM_DEFINITION)
    {
        if (definition_node->enum_definition.identifier == NULL)
        {
            return NULL;
        }
        return definition_node->enum_definition.identifier->text;
    }

    if (definition_node->type == AST_NODE_STRUCT_DEFINITION || definition_node->type == AST_NODE_UNION_DEFINITION)
    {
        if (definition_node->struct_definition.identifier == NULL)
        {
            return NULL;
        }
        return definition_node->struct_definition.identifier->text;
    }

    return NULL;
}

type_info_t const *
register_opaque_struct_or_union_definition(ir_generator_ctx_t * ctx, char const * tag, bool is_union)
{
    if (ctx == NULL || tag == NULL)
    {
        return NULL;
    }

    type_info_t opaque = {0};
    opaque.tag = strdup(tag);
    opaque.kind = is_union ? TYPE_KIND_UNION : TYPE_KIND_STRUCT;
    struct_or_union_members_st * members = NULL;
    bool is_complete = false;

    opaque.type_desc = register_struct_type(
        ctx->type_descriptors,
        LLVMStructCreateNamed(ctx->context, tag),
        (TypeQualifier){0},
        is_union,
        is_complete,
        members
    );
    type_info_t const * registered = generator_add_tagged_type(ctx, opaque);
    if (registered == NULL)
    {
        free((void *)opaque.tag);
        return NULL;
    }

    return registered;
}

static type_info_t const *
register_tagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * type_child,
    char const * tag,
    type_kind_t kind,
    TypeQualifier quals
)
{
    if (ctx == NULL || tag == NULL)
    {
        return NULL;
    }

    type_info_t * existing = generator_lookup_tagged_entry_by_tag_and_kind(ctx, tag, kind);
    if (existing != NULL)
    {
        if (!existing->type_desc->struct_metadata.is_complete)
        {
            struct_or_union_members_st members = extract_struct_or_union_members_type_descriptor(ctx, type_child);

            if (members.num_members > 0)
            {
                existing->field_count = members.num_members;
                existing->fields = malloc(members.num_members * sizeof(*members.members));
                memcpy(existing->fields, members.members, members.num_members * sizeof(*members.members));
                for (size_t i = 0; i < members.num_members; i++)
                {
                    if (existing->fields[i].name != NULL)
                    {
                        existing->fields[i].name = strdup(existing->fields[i].name);
                    }
                }
            }

            struct_field_t * last_field = &members.members[members.num_members - 1];
            unsigned num_storage_units = last_field->bitfield.storage_index + 1;
            LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
            if (field_types == NULL)
            {
                return existing;
            }
            int current_storage_unit = -1;
            for (size_t i = 0; i < members.num_members; i++)
            {
                struct_field_t * field = &members.members[i];
                if (field->bitfield.storage_index != (unsigned)current_storage_unit)
                {
                    current_storage_unit = (int)field->bitfield.storage_index;
                    field_types[current_storage_unit] = field->type_desc->llvm_type;
                }
            }
            LLVMStructSetBody(existing->type_desc->llvm_type, field_types, num_storage_units, false);
            free(field_types);

            type_descriptor_complete_struct(existing->type_desc, &members);

            for (size_t i = 0; i < members.num_members; i++)
            {
                free(members.members[i].name);
            }
            free(members.members);

            return existing;
        }
        return NULL;
    }

    struct_or_union_members_st members = extract_struct_or_union_members_type_descriptor(ctx, type_child);

    type_info_t opaque = {0};
    opaque.tag = strdup(tag);
    opaque.kind = kind;

    if (members.num_members > 0)
    {
        opaque.field_count = members.num_members;
        opaque.fields = malloc(members.num_members * sizeof(*members.members));
        memcpy(opaque.fields, members.members, members.num_members * sizeof(*members.members));
        for (size_t i = 0; i < members.num_members; i++)
        {
            if (opaque.fields[i].name != NULL)
            {
                opaque.fields[i].name = strdup(opaque.fields[i].name);
            }
        }
    }

    for (size_t i = 0; i < members.num_members; i++)
    {
        fprintf(
            stderr,
            "%s: member %zu: %s offset: %u, width: %u, storage: %u\n",
            __func__,
            i,
            members.members[i].name,
            members.members[i].bitfield.bit_offset,
            members.members[i].bitfield.bit_width,
            members.members[i].bitfield.storage_index
        );
    }

    bool is_complete = true;
    opaque.type_desc = register_struct_type(
        ctx->type_descriptors,
        LLVMStructCreateNamed(ctx->context, tag),
        quals,
        kind == TYPE_KIND_UNION,
        is_complete,
        &members
    );

    type_info_t const * registered = generator_add_tagged_type(ctx, opaque);
    if (registered == NULL)
    {
        free((void *)opaque.tag);
        for (size_t i = 0; i < opaque.field_count; i++)
        {
            free(opaque.fields[i].name);
        }
        return NULL;
    }

    if (members.num_members == 0)
    {
        return registered;
    }

    struct_field_t * last_field = &members.members[members.num_members - 1];
    unsigned num_storage_units = last_field->bitfield.storage_index + 1;
    LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
    if (field_types == NULL)
    {
        return registered;
    }
    int current_storage_unit = -1;
    for (size_t i = 0; i < members.num_members; i++)
    {
        struct_field_t * field = &members.members[i];
        if (field->bitfield.storage_index != (unsigned)current_storage_unit)
        {
            current_storage_unit = (int)field->bitfield.storage_index;
            field_types[current_storage_unit] = field->type_desc->llvm_type;
        }
    }
    LLVMStructSetBody(registered->type_desc->llvm_type, field_types, num_storage_units, false);
    free(field_types);

    for (size_t i = 0; i < members.num_members; i++)
    {
        free(members.members[i].name);
    }
    free(members.members);

    return registered;
}

static type_info_t const *
add_untagged_struct_or_union_type(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, type_kind_t kind, int * new_type_id
)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    struct_or_union_members_st members = extract_struct_or_union_members_type_descriptor(ctx, type_child);

    type_info_t new_struct = {0};
    new_struct.tag = generate_anon_name(ctx, (kind == TYPE_KIND_UNTAGGED_STRUCT) ? "struct" : "union");
    new_struct.kind = kind;

    if (members.num_members > 0)
    {
        new_struct.field_count = members.num_members;
        new_struct.fields = malloc(members.num_members * sizeof(*members.members));
        memcpy(new_struct.fields, members.members, members.num_members * sizeof(*members.members));
        for (size_t i = 0; i < members.num_members; i++)
        {
            if (new_struct.fields[i].name != NULL)
            {
                new_struct.fields[i].name = strdup(new_struct.fields[i].name);
            }
        }
    }

    for (size_t i = 0; i < members.num_members; i++)
    {
        fprintf(
            stderr,
            "%s: member %zu: %s bit offset: %u, bit width: %u, bit storage: %u\n",
            __func__,
            i,
            members.members[i].name,
            members.members[i].bitfield.bit_offset,
            members.members[i].bitfield.bit_width,
            members.members[i].bitfield.storage_index
        );
    }

    bool is_complete = true;
    new_struct.type_desc = register_struct_type(
        ctx->type_descriptors,
        LLVMStructCreateNamed(ctx->context, new_struct.tag),
        (TypeQualifier){0},
        kind == TYPE_KIND_UNTAGGED_UNION,
        is_complete,
        &members
    );

    type_info_t const * registered = generator_add_untagged_type(ctx, new_struct, new_type_id);
    if (registered == NULL)
    {
        free((void *)new_struct.tag);
        for (size_t i = 0; i < new_struct.field_count; i++)
        {
            free(new_struct.fields[i].name);
        }
        return NULL;
    }

    if (members.num_members == 0)
    {
        return registered;
    }

    struct_field_t * last_field = &new_struct.fields[new_struct.field_count - 1];
    unsigned num_storage_units = last_field->bitfield.storage_index + 1;
    LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
    int current_storage_unit = -1;
    for (size_t i = 0; i < new_struct.field_count; i++)
    {
        struct_field_t * field = &members.members[i];
        if (field->bitfield.storage_index != (unsigned)current_storage_unit)
        {
            current_storage_unit = field->bitfield.storage_index;
            field_types[current_storage_unit] = field->type_desc->llvm_type;
        }
    }

    LLVMStructSetBody(new_struct.type_desc->llvm_type, field_types, (unsigned)num_storage_units, false);
    free(field_types);

    for (size_t i = 0; i < members.num_members; i++)
    {
        debug_info("freeing: %zu %p '%s'", i, members.members[i].name, members.members[i].name);
        free(members.members[i].name);
    }
    free(members.members);

    return registered;
}

static type_info_t const *
register_untagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, type_kind_t kind, int * new_type_id
)
{
    debug_info("%s", __func__);

    return add_untagged_struct_or_union_type(ctx, type_child, kind, new_type_id);
}

type_info_t const *
register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    if (type_child == NULL
        || (type_child->type != AST_NODE_STRUCT_DEFINITION && type_child->type != AST_NODE_UNION_DEFINITION))
    {
        return NULL;
    }

    char const * struct_tag = NULL;

    if (type_child->struct_definition.identifier != NULL)
    {
        struct_tag = type_child->struct_definition.identifier->text;
    }
    if (struct_tag == NULL)
    {
        type_kind_t kind
            = (type_child->type == AST_NODE_STRUCT_DEFINITION) ? TYPE_KIND_UNTAGGED_STRUCT : TYPE_KIND_UNTAGGED_UNION;

        return register_untagged_struct_or_union_definition(ctx, type_child, kind, NULL);
    }

    type_kind_t kind = (type_child->type == AST_NODE_STRUCT_DEFINITION) ? TYPE_KIND_STRUCT : TYPE_KIND_UNION;

    return register_tagged_struct_or_union_definition(ctx, type_child, struct_tag, kind, (TypeQualifier){0});
}

static type_info_t const *
register_untagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node, int * new_enum_id)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    if (!register_enum_constants(ctx, enum_node))
    {
        return NULL;
    }

    type_info_t enum_info = {0};

    enum_info.kind = TYPE_KIND_UNTAGGED_ENUM;
    // Enums are typically represented as int, but this can be improved to use the smallest type that fits the values
    enum_info.type_desc = type_descriptor_get_int32_type(ctx->type_descriptors, false);
    enum_info.fields = NULL;
    enum_info.field_count = 0;

    return generator_add_untagged_type(ctx, enum_info, new_enum_id);
}

static type_info_t const *
register_tagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node, char const * tag)
{
    if (ctx == NULL || tag == NULL)
    {
        return NULL;
    }

    if (!register_enum_constants(ctx, enum_node))
    {
        return NULL;
    }

    type_info_t enum_info = {0};

    enum_info.kind = TYPE_KIND_ENUM;
    enum_info.type_desc = type_descriptor_get_int32_type(ctx->type_descriptors, false);
    enum_info.tag = strdup(tag);

    return generator_add_tagged_type(ctx, enum_info);
}

type_info_t const *
register_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node)
{
    char const * tag = search_ast_for_type_tag(enum_node);
    if (tag != NULL)
    {
        return register_tagged_enum_definition(ctx, enum_node, tag);
    }
    return register_untagged_enum_definition(ctx, enum_node, NULL);
}

/**
 * @brief Initializes the IR generator context.
 * Creates LLVM context, module, and builder.
 */
ir_generator_ctx_t *
ir_generator_init(
    char const * module_name,
    ir_generation_flags flags,
    epc_parser_ctx_t * parse_ctx,
    source_location_tracker_t * loc_tracker
)
{
    ir_generator_ctx_t * ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        debug_error("Failed to allocate memory for context.");
        return NULL;
    }
    ctx->generation_flags = flags;

    // Initialize LLVM

    ctx->context = LLVMContextCreate();
    if (!ctx->context)
    {
        debug_error("Failed to create LLVM context.");
        ir_generator_dispose(ctx);
        return NULL;
    }

    // Initialize error collection (any error will be fatal since max_errors=1)
    ir_gen_error_collection_init(&ctx->errors, 1, parse_ctx, module_name, loc_tracker);

    ctx->module = LLVMModuleCreateWithName(module_name);
    ctx->data_layout = LLVMGetModuleDataLayout(ctx->module);

    ctx->ref_type.i1_type = LLVMInt1TypeInContext(ctx->context);
    ctx->ref_type.i8_type = LLVMInt8TypeInContext(ctx->context);
    ctx->ref_type.i16_type = LLVMInt16TypeInContext(ctx->context);
    ctx->ref_type.i32_type = LLVMInt32TypeInContext(ctx->context);
    ctx->ref_type.i64_type = LLVMInt64TypeInContext(ctx->context);
    ctx->ref_type.ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    ctx->ref_type.f32_type = LLVMFloatTypeInContext(ctx->context);
    ctx->ref_type.f64_type = LLVMDoubleTypeInContext(ctx->context);
    ctx->ref_type.long_double_type = LLVMX86FP80TypeInContext(ctx->context);
    ctx->ref_type.void_type = LLVMVoidTypeInContext(ctx->context);

    ctx->type_descriptors = type_descriptors_create_registry(ctx->context, ctx->data_layout);
    if (ctx->type_descriptors == NULL)
    {
        ir_generator_dispose(ctx);
        return NULL;
    }

    if (!ctx->module)
    {
        debug_error("Failed to create LLVM module.");
        ir_generator_dispose(ctx);
        return NULL;
    }

    ctx->builder = LLVMCreateBuilder();
    if (!ctx->builder)
    {
        debug_error("Failed to create LLVM builder.");
        ir_generator_dispose(ctx);
        return NULL;
    }

    // Initialize with global scope
    ctx->current_scope = generator_scope_create(ctx); // NULL parent = global scope
    if (!ctx->current_scope)
    {
        debug_error("Failed to create global scope.");
        ir_generator_dispose(ctx);
        return NULL;
    }

    // Add built-in macro __FILE__ as a string constant in the global scope
    {
        char const * file_name = module_name ? module_name : "";
        size_t len = strlen(file_name);
        LLVMTypeRef arr_type = LLVMArrayType(ctx->ref_type.i8_type, (unsigned)(len + 1));
        LLVMValueRef global = LLVMAddGlobal(ctx->module, arr_type, "__FILE__");
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(global, true);
        LLVMSetInitializer(global, LLVMConstStringInContext(ctx->context, file_name, (unsigned)len, false));

        // Store pointer to the first element as the macro value
        LLVMValueRef indices[2]
            = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};
        LLVMValueRef ptr = LLVMConstInBoundsGEP2(arr_type, global, indices, 2);

        TypeDescriptor const * char_desc = type_descriptor_get_int8_type(ctx->type_descriptors, true);
        TypeDescriptor const * arr_desc = get_or_create_array_type(ctx->type_descriptors, char_desc, len + 1);
        TypedValue val = create_typed_value(ptr, arr_desc, false);
        generator_add_symbol(ctx, "__FILE__", val);
    }

    /* Create some builtins. */
    {
        TypeDescriptor const * char_desc = type_descriptor_get_int8_type(ctx->type_descriptors, false);
        TypeDescriptor const * char_ptr_desc
            = get_or_create_pointer_type(ctx->type_descriptors, char_desc, (TypeQualifier){0});
        scope_typedef_entry_t typedef_entry = {
            .kind = TYPE_KIND_BUILTIN,
            .name = strdup("__builtin_va_list"),
            .type_desc = char_ptr_desc,
        };

        generator_add_typedef_entry(ctx, typedef_entry);
    }

    if (ctx->generation_flags.generate_default_variables)
    {
        /* Create a replacement for NULL, which won't be available if not preprocessing. */
        LLVMTypeRef null_type = ctx->ref_type.ptr_type;
        LLVMValueRef global_null = LLVMAddGlobal(ctx->module, null_type, "NULL_REPLACEMENT");
        LLVMValueRef null_llvm_val = LLVMConstPointerNull(null_type);
        LLVMSetInitializer(global_null, null_llvm_val);
        LLVMSetGlobalConstant(global_null, true);
        LLVMSetLinkage(global_null, LLVMInternalLinkage);

        TypeDescriptor const * void_desc = type_descriptor_get_void_type(ctx->type_descriptors);
        TypeDescriptor const * ptr_desc
            = get_or_create_pointer_type(ctx->type_descriptors, void_desc, (TypeQualifier){0});
        TypedValue null_val = create_typed_value(global_null, ptr_desc, false);
        generator_add_symbol(ctx, "NULL", null_val);

        // hack in a function definition for printf(), auto-declare as variadic returning i32
        // with no required arguments to support different call patterns
        char const * printf_fn_name = "printf";
        TypeDescriptor const * ret_type = type_descriptor_get_int32_type(ctx->type_descriptors, false);
        LLVMTypeRef ret_type_llvm = ret_type->llvm_type;
        LLVMTypeRef func_type = LLVMFunctionType(ret_type_llvm, NULL, 0, true);
        LLVMValueRef func = LLVMAddFunction(ctx->module, printf_fn_name, func_type);
        TypeDescriptor const * func_desc = get_or_create_function_type(ctx->type_descriptors, ret_type, NULL, 0, true);
        TypedValue print_val = create_typed_value(func, func_desc, false);
        add_function_declaration(ctx, printf_fn_name, print_val, false);
    }

    return ctx;
}

/**
 * @brief Frees the symbol table memory (all scopes in the chain).
 */
static void
pop_all_scopes(ir_generator_ctx_t * ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    // Free all scopes in the chain
    while (ctx->current_scope)
    {
        generator_scope_pop(ctx);
    }
}

/**
 * @brief Disposes of the IR generator context and associated LLVM resources.
 */
void
ir_generator_dispose(ir_generator_ctx_t * ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    pop_all_scopes(ctx); // Free symbol table first (includes local types)

    // Free error collection
    ir_gen_error_collection_free(&ctx->errors);

    // Free function declarations
    for (size_t i = 0; i < ctx->function_declarations.count; ++i)
    {
        free(ctx->function_declarations.entries[i].name);
    }
    free(ctx->function_declarations.entries);

    if (ctx->builder)
        LLVMDisposeBuilder(ctx->builder);
    // LLVMDisposeModule takes ownership of the module.
    if (ctx->module)
        LLVMDisposeModule(ctx->module);
    if (ctx->context)
        LLVMContextDispose(ctx->context);

    type_descriptors_destroy_registry(ctx->type_descriptors);

    free(ctx);
}

// --- Main LLVM IR Generation Function ---

/**
 * @brief Generates LLVM IR from the provided AST root.
 * @param ctx The IR generator context.
 * @param ast_root The root node of the AST.
 * @return The LLVM module containing the generated IR, or NULL on failure.
 */
LLVMModuleRef
generate_llvm_ir(ir_generator_ctx_t * ctx, c_grammar_node_t const * ast_root)
{
    if (!ctx || !ast_root)
    {
        debug_error("Invalid context or AST root provided.");
        return NULL;
    }

    process_ast_node(ctx, ast_root);

    // Check if errors occurred during IR generation
    if (ir_gen_has_errors(&ctx->errors))
    {
        return NULL;
    }

    return ctx->module;
}

// --- AST Node Processing Logic ---

static void
add_function_scope_builtin_macros(ir_generator_ctx_t * ctx, char const * func_name)
{
    // __FUNC__ and __func__ as string constants of the function name
    char const * func_name_macro = func_name;
    size_t flen = strlen(func_name_macro);
    TypeDescriptor const * char_desc = type_descriptor_get_int8_type(ctx->type_descriptors, true);
    TypeDescriptor const * farr_desc = get_or_create_array_type(ctx->type_descriptors, char_desc, flen + 1);
    LLVMValueRef fglobal = LLVMAddGlobal(ctx->module, farr_desc->llvm_type, "__FUNC__");
    LLVMSetLinkage(fglobal, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(fglobal, true);
    LLVMSetInitializer(fglobal, LLVMConstStringInContext(ctx->context, func_name_macro, (unsigned)flen, false));

    TypeDescriptor const * i32_desc = type_descriptor_get_uint32_type(ctx->type_descriptors, true);
    LLVMValueRef findices[2]
        = {LLVMConstInt(i32_desc->llvm_type, 0, false), LLVMConstInt(i32_desc->llvm_type, 0, false)};
    LLVMValueRef fptr = LLVMConstInBoundsGEP2(farr_desc->llvm_type, fglobal, findices, 2);

    TypedValue fval = create_typed_value(fptr, farr_desc, false);
    generator_add_symbol(ctx, "__FUNC__", fval);
    // __func__ alias to same value
    generator_add_symbol(ctx, "__func__", fval);

    // __LINE__ as integer constant 0 (i32)
    LLVMValueRef line_const = LLVMConstInt(i32_desc->llvm_type, 0, false);
    TypedValue lval = create_typed_value(line_const, i32_desc, false);
    generator_add_symbol(ctx, "__LINE__", lval);
}

static LLVMValueRef
evaluate_constant_initializer(ir_generator_ctx_t * ctx, TypeDescriptor const * desc, c_grammar_node_t const * node)
{
    // Handle String Literals -> Constant Array
    if (node->type == AST_NODE_STRING_LITERAL)
    {
        char const * str = decode_string(node->text);
        LLVMValueRef c = LLVMConstStringInContext(ctx->context, str, strlen(str), false);
        free((char *)str);
        return c;
    }

    // Handle Initializer Lists: {1, 2, 3}
    if (node->type == AST_NODE_INITIALIZER_LIST)
    {
        if (desc->kind == NCC_TYPE_KIND_ARRAY)
        {
            size_t count = node->list.count;
            LLVMValueRef * elems = malloc(sizeof(LLVMValueRef) * desc->array_metadata.size);

            for (uint32_t i = 0; i < desc->array_metadata.size; i++)
            {
                if (i < count)
                {
                    c_grammar_node_t const * list_entry = node->list.children[i];
                    // c_grammar_node_t const * designation = list_entry->initializer_list_entry.designation;
                    c_grammar_node_t const * initializer = list_entry->initializer_list_entry.initializer;

                    /* TODO: Support the designation. */
                    // Recurse for nested elements
                    elems[i] = evaluate_constant_initializer(ctx, desc->pointee, initializer);
                }
                else
                {
                    // Pad with zeros
                    elems[i] = LLVMConstNull(desc->pointee->llvm_type);
                }
            }
            LLVMValueRef res = LLVMConstArray(desc->pointee->llvm_type, elems, desc->array_metadata.size);
            free(elems);
            return res;
        }

        if (desc->kind == NCC_TYPE_KIND_STRUCT)
        {
            size_t field_count = desc->struct_metadata.members.num_members;
            LLVMValueRef * fields = malloc(sizeof(LLVMValueRef) * field_count);

            for (size_t i = 0; i < field_count; i++)
            {
                if (i < node->list.count)
                {
                    c_grammar_node_t const * list_entry = node->list.children[i];
                    // c_grammar_node_t const * designation = list_entry->initializer_list_entry.designation;
                    c_grammar_node_t const * initializer = list_entry->initializer_list_entry.initializer;
                    /* TODO: Support the designation. */

                    fields[i] = evaluate_constant_initializer(
                        ctx, desc->struct_metadata.members.members[i].type_desc, initializer
                    );
                }
                else
                {
                    fields[i] = LLVMConstNull(desc->struct_metadata.members.members[i].type_desc->llvm_type);
                }
            }
            // Note: Use LLVMConstNamedStruct if your descriptor holds the actual named type
            LLVMValueRef res = LLVMConstStructInContext(ctx->context, fields, field_count, false);
            free(fields);
            return res;
        }
    }

    // Simple expressions (must be constant foldable)
    // You might need a specialized process_constant_expression here
    TypedValue val = process_expression(ctx, node);
    if (val.value == NULL)
    {
        return val.value;
    }

    if (LLVMIsConstant(val.value))
    {
        return val.value;
    }

    return LLVMConstNull(desc->llvm_type);
}

/**
 * @brief Create a global variable (static or non-static).
 *
 * Handles creation of file-scope global variables including:
 * - Unsized arrays with string literal initializers
 * - Zero-initialization for globals without explicit initializers
 * - Explicit initializer handling (array, struct, expression)
 *
 * @param ctx IR generator context
 * @param var_type LLVM type for the variable
 * @param function_signature LLVM type for the function signature if this is a function pointer (else NULL)
 * @param var_name Name of the variable
 * @param is_static Whether this is a static global
 * @param is_const Whether this is a const global
 * @param initializer_expr_node Initializer AST node (may be NULL)
 * @param decl_specifiers Declaration specifiers (for pointee type calculation)
 * @return Created global variable (always succeeds, never returns NULL)
 */
static LLVMValueRef
create_global_variable(
    ir_generator_ctx_t * ctx,
    TypeDescriptor const * type_desc,
    char const * var_name,
    bool is_const,
    c_grammar_node_t const * initializer_expr_node
)
{
    debug_info("Creating global for variable '%s'", var_name);

    LLVMValueRef global_var = NULL;
    TypeDescriptor const * final_desc = type_desc;

    // 1. Handle unsized array with string literal: char str[] = "hello";
    if (type_desc->kind == NCC_TYPE_KIND_ARRAY && type_desc->array_metadata.size == 0 && initializer_expr_node
        && initializer_expr_node->type == AST_NODE_STRING_LITERAL)
    {
        char const * decoded = decode_string(initializer_expr_node->text);
        size_t str_len = strlen(decoded ? decoded : initializer_expr_node->text) + 1;

        // Update the descriptor to have the actual size
        final_desc = get_or_create_array_type(ctx->type_descriptors, type_desc->pointee, str_len);
        free((char *)decoded);
    }

    // 2. Create the Global Identity
    global_var = LLVMAddGlobal(ctx->module, final_desc->llvm_type, var_name);
    LLVMSetLinkage(global_var, LLVMInternalLinkage); // Default to internal for safety
    if (is_const)
        LLVMSetGlobalConstant(global_var, true);

    // 3. Evaluate the Initializer (Must be Constant)
    if (initializer_expr_node)
    {
        LLVMValueRef const_init = evaluate_constant_initializer(ctx, final_desc, initializer_expr_node);
        if (const_init)
        {
            LLVMSetInitializer(global_var, const_init);
        }
        else
        {
            // Fallback to null if constant evaluation fails
            LLVMSetInitializer(global_var, LLVMConstNull(final_desc->llvm_type));
        }
    }
    else
    {
        // C standard: globals without initializers are zero-initialized
        LLVMSetInitializer(global_var, LLVMConstNull(final_desc->llvm_type));
    }

    // 4. Add to Symbol Table
    TypedValue val = create_typed_value(global_var, final_desc, true);
    generator_add_symbol(ctx, var_name, val);

    return global_var;
}
static void
process_declarator(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * init_decl_initializer,
    c_grammar_node_t const * decl_specifiers,
    c_grammar_node_t const * declarator_node
)
{
    debug_info("%s", __func__);

    // 1. Resolve the TypeDescriptor immediately.
    // This handles pointers, arrays, functions, and typedefs in one go.
    TypeDescriptor const * type_desc = resolve_type_descriptor(ctx, decl_specifiers, declarator_node);
    if (type_desc == NULL)
    {
        debug_warning("%s: Failed to resolve type descriptor", __func__);
        return;
    }
    debug_info(
        "process_declarator got type with qualifiers: const=%d, volatile=%d",
        type_desc->qualifiers.is_const,
        type_desc->qualifiers.is_volatile
    );

    // 2. Extract Identifier (Variable Name)
    char const * var_name = search_for_identifier(declarator_node);
    if (var_name == NULL)
    {
        debug_warning("%s: Failed to find identifier", __func__);
        return;
    }

    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    bool is_global = (current_block == NULL);
    bool is_static = false;

    if (decl_specifiers != NULL && decl_specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        is_static = decl_specifiers->decl_specifiers.storage.has_static;
    }

    // 3. Global / Static Storage
    if (type_desc->kind == NCC_TYPE_KIND_FUNCTION)
    {
        // Check if the function is already in the LLVM module
        LLVMValueRef func = LLVMGetNamedFunction(ctx->module, var_name);
        dump_type_descriptor(var_name, type_desc, DEBUG_LEVEL_ERROR);
        if (!func)
        {
            debug_info("Adding function declaration: %s", var_name);
            func = LLVMAddFunction(ctx->module, var_name, type_desc->llvm_type);

            // Standard C function declarations have "External" linkage by default
            LLVMSetLinkage(func, LLVMExternalLinkage);
        }

        // Add it to your symbol table so the compiler can resolve calls to it.
        // Functions are usually treated as R-values (the address of the function).
        TypedValue func_val = create_typed_value(func, type_desc, false);
        generator_add_symbol(ctx, var_name, func_val);

        return;
    }
    if (is_static || is_global)
    {
        c_grammar_node_t const * init_expr = (init_decl_initializer && init_decl_initializer->list.count > 0)
                                                 ? init_decl_initializer->list.children[0]
                                                 : NULL;

        create_global_variable(ctx, type_desc, var_name, false, init_expr);

        return;
    }

    // 4. Local Storage (Stack)
    debug_info("Allocating local variable '%s'", var_name);
    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, type_desc->llvm_type, var_name);

    // 5. Add to Symbol Table
    // We no longer need struct_name or pointee_type hacks; the descriptor has it all.
    TypedValue sym_val = create_typed_value(alloca_inst, type_desc, true);
    generator_add_symbol(ctx, var_name, sym_val);

    // 6. Process Initializer
    if (init_decl_initializer != NULL && init_decl_initializer->list.count > 0)
    {
        c_grammar_node_t const * init_node = init_decl_initializer->list.children[0];

        if (init_node->type == AST_NODE_INITIALIZER_LIST)
        {
            // Use your updated initializer list logic that takes a TypeDescriptor
            process_initializer_list_type_desc(ctx, alloca_inst, type_desc, init_node, NULL);
        }
        else
        {
            // Scalar initialization
            TypedValue init_res = process_expression(ctx, init_node);
            if (init_res.value == NULL)
            {
                return;
            }

            TypedValue cast_val = cast_typed_value_to_desc(ctx, init_res, type_desc);
            aligned_store(ctx, ctx->builder, cast_val.value, type_desc->llvm_type, alloca_inst);
        }
    }
}

static void
process_function_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Create function scope for parameters and body
    generator_scope_push(ctx);
    // --- Handle Function Definition ---
    c_grammar_node_t const * decl_specifiers_node = node->function_definition.declaration_specifiers;
    c_grammar_node_t const * declarator_node = node->function_definition.declarator;
    c_grammar_node_t const * compound_stmt_node = node->function_definition.body;

    if (decl_specifiers_node == NULL || declarator_node == NULL || compound_stmt_node == NULL)
    {
        debug_error("Function definition is missing declaration specifiers, declarator, or body.");
        generator_scope_pop(ctx);
        return;
    }

    TypeDescriptor const * type_desc = resolve_type_descriptor(ctx, decl_specifiers_node, declarator_node);
    if (type_desc == NULL)
    {
        debug_info("%s: failed to get type desc for function definition", __func__);
        return;
    }

    TypeDescriptor const * previous_function_return_type = ctx->current_function_return_type;
    ctx->current_function_return_type = type_desc->function_metadata.return_type;

    // --- Extract Function Name ---
    char const * func_name = NULL;
    c_grammar_node_t const * direct_decl = declarator_node->declarator.direct_declarator;

    if (direct_decl && direct_decl->list.count > 0 && direct_decl->list.children[0]->type == AST_NODE_IDENTIFIER)
    {
        func_name = direct_decl->list.children[0]->text;
    }
    if (func_name == NULL)
    {
        func_name = "unknown_function";
    }
    add_function_scope_builtin_macros(ctx, func_name);

    // Check for function redeclaration or signature mismatch
    LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, func_name);
    if (existing != NULL)
    {
        struct function_decl_entry * decl = find_function_declaration(ctx, func_name);

        /* Only check signature mismatch for tracked declarations (not forward decls from our static handling) */
        if (decl != NULL)
        {
            LLVMTypeRef existing_type = LLVMGlobalGetValueType(existing);
            if (!function_signatures_match(existing_type, type_desc->llvm_type))
            {
                ctx->current_function_return_type = previous_function_return_type;
                ir_gen_error(&ctx->errors, node, "Function '%s' redeclared with different signature.", func_name);
                generator_scope_pop(ctx);
                return;
            }

            if (decl->has_definition)
            {
                ctx->current_function_return_type = previous_function_return_type;
                ir_gen_error(&ctx->errors, node, "Function '%s' already has a body.", func_name);
                generator_scope_pop(ctx);
                return;
            }

            decl->has_definition = true;
        }
        else
        {
            /* Forward declaration not tracked — register it now */
            TypedValue decl = create_typed_value(existing, type_desc, false);
            add_function_declaration(ctx, func_name, decl, true);
        }
    }
    else
    {
        /* No existing declaration — add it now */
        TypedValue decl = create_typed_value(existing, type_desc, false);
        add_function_declaration(ctx, func_name, decl, true);
    }

    /* Reuse existing declaration if already added (e.g. from a forward declaration),
     * but only if the type matches. If it was an auto-declared stub with wrong type,
     * delete it and recreate with the correct type. */
    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, func_name);
    if (func != NULL)
    {
        LLVMTypeRef existing_type = LLVMGlobalGetValueType(func);
        if (!function_signatures_match(existing_type, type_desc->llvm_type))
        {
            /* Replace the stub: redirect all uses to a new function then delete the old one */
            LLVMValueRef new_func = LLVMAddFunction(ctx->module, "", type_desc->llvm_type);
            LLVMReplaceAllUsesWith(func, new_func);
            LLVMDeleteFunction(func);
            LLVMSetValueName(new_func, func_name);
            func = new_func;
        }
        /* Verify param count matches before proceeding */
        if (LLVMCountParams(func) != (unsigned)type_desc->function_metadata.param_count)
        {
            debug_error(
                "Function '%s': param count mismatch after setup (%u vs %zu), skipping.",
                func_name,
                LLVMCountParams(func),
                type_desc->function_metadata.param_count
            );
            generator_scope_pop(ctx);
            ctx->current_function_return_type = previous_function_return_type;

            return;
        }
    }
    else
    {
        debug_info(
            "Adding new function '%s' to module type signature: %d", func_name, LLVMGetTypeKind(type_desc->llvm_type)
        );
        func = LLVMAddFunction(ctx->module, func_name, type_desc->llvm_type);
        debug_info("Added function");
    }

    // Create a basic block for the function's entry point.
    LLVMBasicBlockRef entry_block = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry_block);

    /* Add inlinehint attribute if function has inline specifier */
    if (decl_specifiers_node->type == AST_NODE_NAMED_DECL_SPECIFIERS
        && decl_specifiers_node->decl_specifiers.function_specifier != NULL)
    {
        LLVMAttributeRef attr
            = LLVMCreateEnumAttribute(ctx->context, LLVMGetEnumAttributeKindForName("inlinehint", 10), 0);
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex, attr);
    }

    // --- Handle function parameters: allocate space and store arguments ---
    debug_info("Processing %u function parameters for '%s'", type_desc->function_metadata.param_count, func_name);

    c_grammar_node_t const * param_list = search_parameters_list_in_declarator(declarator_node);
    if (param_list == NULL)
    {
        debug_error("failed to find_parameter list");
        generator_scope_pop(ctx);
        return;
    }
    parameter_definitions_t params = extract_function_parameters(ctx, param_list);

    for (size_t i = 0; i < params.count; ++i)
    {
        char const * p_name = params.names[i];
        TypeDescriptor const * p_type_desc = params.types[i];
        debug_info("%s: Processing parameter %zu name: %s type desc %p", __func__, i, p_name, (void *)p_type_desc);
        LLVMTypeRef p_type = p_type_desc->llvm_type;
        LLVMValueRef param_val = LLVMGetParam(func, (unsigned)i);
        LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, p_type, p_name != NULL ? p_name : "fn_param");

        aligned_store(ctx, ctx->builder, param_val, p_type, alloca_inst);

        if (p_name != NULL)
        {

            TypedValue p_val = create_typed_value(alloca_inst, p_type_desc, true);

            // If your descriptor is a struct or points to one,
            // the symbol table can now just check p_type->kind
            generator_add_symbol(ctx, p_name, p_val);
        }
        debug_info("Processed parameter %zu", i);
    }

    parameter_definitions_cleanup(&params);

    debug_info("processed parameters");
    // Process the compound statement (function body).
    process_ast_node(ctx, compound_stmt_node);

    ctx->current_function_return_type = previous_function_return_type;

    if (ctx->errors.fatal)
    {
        generator_scope_pop(ctx);
        return;
    }

    // --- Add a default return if the function doesn't end with one ---
    // Only add a default return if the block is valid and unterminated
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    if (current_block && !LLVMGetBasicBlockTerminator(current_block))
    {
        if (LLVMGetTypeKind(type_desc->function_metadata.return_type->llvm_type) == LLVMVoidTypeKind)
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            TypeDescriptor const * i32_desc = type_descriptor_get_int32_type(ctx->type_descriptors, true);
            LLVMBuildRet(ctx->builder, LLVMConstInt(i32_desc->llvm_type, 0, false));
        }
    }

    // Pop function scope
    generator_scope_pop(ctx);

    /* Clear the builder insert point so subsequent declarations don't
     * mistakenly think we're inside a function body. */
    LLVMClearInsertionPosition(ctx->builder);
}

static void
process_return_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * expr_node = node->return_statement.expression;

    // We need the expected return type descriptor.
    // Assuming you store this in your context when starting a function:
    TypeDescriptor const * expected_ret_desc = ctx->current_function_return_type;

    if (expected_ret_desc == NULL)
    {
        debug_error("Internal Error: No return type descriptor found for current function context.");
        return;
    }

    if (expr_node != NULL)
    {
        // 1. Process the expression
        TypedValue return_value = process_expression(ctx, expr_node);
        if (return_value.value == NULL)
        {
            return;
        }
        dump_typed_value("process_return", return_value);
        // 2. Ensure it's an RValue (we can't return a memory address/LValue directly)
        return_value = ensure_rvalue(ctx, "return_rval", return_value);

        if (return_value.value == NULL)
        {
            debug_error("Failed to process return expression.");
            return;
        }

        // 3. Use your new descriptor-based cast
        // This handles converting int -> float, or promoting widths as defined by C rules.
        TypedValue cast_val = cast_typed_value_to_desc(ctx, return_value, expected_ret_desc);

        LLVMBuildRet(ctx->builder, cast_val.value);
    }
    else
    {
        // Handle 'return;' (no expression)
        if (expected_ret_desc->function_metadata.is_void_return || expected_ret_desc->specifiers.is_void)
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            // C Rule: returning nothing in a non-void function is usually a warning,
            // but for code generation, we provide a zeroed value of the expected type.
            debug_warning("Return without value in non-void function.");

            LLVMValueRef zero_const = LLVMConstNull(expected_ret_desc->llvm_type);
            LLVMBuildRet(ctx->builder, zero_const);
        }
    }
}

static void
process_declaration(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    /* [ OptionalKwExtension DeclarationSpecifiers OptionalInitDeclaratorList ] */
    // --- Handle Variable Declarations ---

    // Register any struct/enum definitions in the declaration specifiers (in current scope)
    c_grammar_node_t const * decl_specifiers = node->declaration.declaration_specifiers;
    debug_info("decl specifiers node: %s", get_node_type_name_from_node(decl_specifiers));
    c_grammar_node_t const * specifiers_list = decl_specifiers->decl_specifiers.type_specifiers;
    debug_info("list: %s count %u", get_node_type_name_from_node(specifiers_list), specifiers_list->list.count);

    // TypeDescriptor const * resolved_type = resolve_type_from_ast(ctx, node);
    // debug_info("resolved type: %p", (void *)resolved_type);

    c_grammar_node_t const * type_spec_node = NULL;

    if (specifiers_list->list.count > 0)
    {
        type_spec_node = specifiers_list->list.children[0];
    }
    if (type_spec_node != NULL)
    {
        for (size_t j = 0; j < type_spec_node->list.count; ++j)
        {
            c_grammar_node_t * type_child = type_spec_node->list.children[j];

            if (type_child != NULL)
            {
                if ((type_child->type == AST_NODE_STRUCT_DEFINITION) || (type_child->type == AST_NODE_UNION_DEFINITION))
                {
                    register_struct_definition(ctx, type_child);
                }
                else if (type_child->type == AST_NODE_ENUM_DEFINITION)
                {
                    register_enum_definition(ctx, type_child);
                }
            }
        }
    }

    c_grammar_node_t const * init_decl_nodes = node->declaration.init_declarator_list;
    debug_info("init decl nodes: %s", get_node_type_name_from_node(init_decl_nodes));
    // Process InitDeclarators to create variables and initialize them.
    if (init_decl_nodes != NULL)
    {
        for (size_t i = 0; i < init_decl_nodes->list.count; ++i)
        {
            c_grammar_node_t const * init_decl_node = init_decl_nodes->list.children[i];

            c_grammar_node_t const * init_decl_initializer = init_decl_node->init_declarator.initializer;
            c_grammar_node_t const * declarator_node = init_decl_node->init_declarator.declarator;

            process_declarator(ctx, init_decl_initializer, decl_specifiers, declarator_node);
        }
    }
}

/**
 * @brief Recursively processes AST nodes to generate LLVM IR.
 * This function dispatches to specific handlers based on the node type.
 */
static void
_process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return;
    }

    switch (node->type)
    {
    case AST_NODE_TRANSLATION_UNIT:
    {
        if (node->translation_unit.external_declarations == NULL)
        {
            debug_error("Translation unit is missing external declarations.");
            return;
        }
        generator_scope_push(ctx);
        process_ast_node(ctx, node->translation_unit.external_declarations);
        generator_scope_pop(ctx);
        break;
    }
    case AST_NODE_EXTERNAL_DECLARATIONS:
    {
        // Process each external declaration (could be variable or function)
        if (node->list.children)
        {
            for (size_t i = 0; i < node->list.count; ++i)
            {
                process_ast_node(ctx, node->list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_EXTERNAL_DECLARATION:
    {
        if (node->external_declaration.preprocessor_directive != NULL)
        {
            process_ast_node(ctx, node->external_declaration.preprocessor_directive);
        }
        else if (node->external_declaration.top_level_declaration != NULL)
        {
            process_ast_node(ctx, node->external_declaration.top_level_declaration);
        }
        break;
    }
    case AST_NODE_TOP_LEVEL_DECLARATION:
    {
        process_ast_node(ctx, node->top_level_declaration.declaration);
        break;
    }
    case AST_NODE_PREPROCESSOR_LINE_MARKER:
    {
        ast_node_preprocessor_line_marker_t const * marker = &node->line_marker;

        /* 2. Update the tracker with the mapping */
        source_location_tracker_add_entry(
            ctx->errors.loc_tracker, node->source_data.offset, marker->line_number, marker->filename
        );
        debug_info("flags count: %zu", marker->flags_count);
        /* 3. Handle include stack based on flags */
        for (size_t i = 0; i < marker->flags_count; i++)
        {
            debug_info("marker %zu flag: %zu", i, marker->flags[i]);

            if (marker->flags[i] == 1) /* Enter include */
            {
                source_location_tracker_push_include(ctx->errors.loc_tracker, marker->filename, marker->line_number);
            }
            else if (marker->flags[i] == 2) /* Exit include */
            {
                source_location_tracker_pop_include(ctx->errors.loc_tracker);
            }
        }
        break;
    }
    case AST_NODE_PREPROCESSOR_DIRECTIVE:
    {
        // For now, we can ignore preprocessor directives in IR generation
        break;
    }
    case AST_NODE_FUNCTION_DEFINITION:
    {
        process_function_definition(ctx, node);
        break;
    }
    case AST_NODE_COMPOUND_STATEMENT:
    {
        // Create new scope for this block
        generator_scope_push(ctx);

        for (size_t i = 0; i < node->list.count; ++i)
        {
            process_ast_node(ctx, node->list.children[i]);
        }

        // Pop block scope when exiting
        generator_scope_pop(ctx);
        break;
    }
    case AST_NODE_EXPRESSION_STATEMENT:
    {
        c_grammar_node_t const * expr_node = node->expression_statement.expression;

        process_expression(ctx, expr_node);
        break;
    }
    case AST_NODE_DECLARATION:
    {
        process_declaration(ctx, node);
        break;
    }
    case AST_NODE_TYPEDEF_DECLARATION:
    {
        /* Handle TypedefDeclaration node: [KwExtension, DeclarationSpecifiers, InitDeclaratorList] */
        c_grammar_node_t const * decl_specs = node->declaration.declaration_specifiers;
        c_grammar_node_t const * struct_def_node = NULL;
        c_grammar_node_t const * enum_def_node = NULL;
        c_grammar_node_t const * specifiers_list = decl_specs->decl_specifiers.type_specifiers;
        c_grammar_node_t const * typedef_specifier_node = decl_specs->decl_specifiers.typedef_specifier;
        char const * typedef_name = search_for_identifier(typedef_specifier_node);
        scope_typedef_entry_t const * existing_typedef_info = NULL;

        if (typedef_name != NULL)
        {
            debug_info("Processing typedef '%s'", typedef_name);
            /* We should have a typedef of this name already registered. */
            existing_typedef_info = generator_lookup_typedef_entry_by_name(ctx, typedef_name);
            if (existing_typedef_info != NULL)
            {
                debug_info("Found typedef descriptor for '%s'", typedef_name);
            }
            else
            {
                debug_info("No typedef descriptor found for '%s'", typedef_name);
            }
        }
        else
        {
            debug_info("No typedef name found");
        }

        /* Look for struct/union/enum definition inside DeclarationSpecifiers once */
        for (size_t i = 0; i < specifiers_list->list.count; ++i)
        {
            c_grammar_node_t * spec_child = specifiers_list->list.children[i];

            {
                for (size_t j = 0; j < spec_child->list.count; ++j)
                {
                    c_grammar_node_t const * type_child = spec_child->list.children[j];
                    if (type_child
                        && (type_child->type == AST_NODE_STRUCT_DEFINITION
                            || type_child->type == AST_NODE_UNION_DEFINITION))
                    {
                        struct_def_node = type_child;
                        break;
                    }
                    else if (type_child && type_child->type == AST_NODE_ENUM_DEFINITION)
                    {
                        enum_def_node = type_child;
                        break;
                    }
                }
            }
            if (struct_def_node || enum_def_node)
            {
                break;
            }
        }

        /* Iterate over all TypedefInitDeclarators */
        c_grammar_node_t const * init_declarator_list = node->declaration.init_declarator_list;

        for (size_t i = 0; i < init_declarator_list->list.count; ++i)
        {
            c_grammar_node_t const * typedef_init_decl = init_declarator_list->list.children[i];

            /* TypedefInitDeclarator -> TypedefDeclarator -> Identifier (via find_typedef_name_node) */
            c_grammar_node_t const * typedef_decl = typedef_init_decl->init_declarator.declarator;

            TypeDescriptor const * typedef_type_desc = resolve_type_descriptor(ctx, decl_specs, typedef_decl);
            if (typedef_type_desc != NULL)
            {
                debug_info("%s: Got type desc for typedef", __func__);
            }
            else
            {
                debug_info("%s: Failed to get type desc for typedef", __func__);
            }

            c_grammar_node_t const * name_node = find_typedef_name_node(typedef_decl);

            if (name_node != NULL && name_node->type == AST_NODE_IDENTIFIER && name_node->text != NULL)
            {
                char const * typedef_name = name_node->text;
                debug_info("Typedef: name='%s'", typedef_name);

                bool handled = false;
                if (typedef_type_desc != NULL)
                {
                    type_kind_t kind = generator_lookup_kind_by_type_descriptor(ctx, typedef_type_desc);
                    debug_info("%s existing info: %p", __func__, (void *)existing_typedef_info);
                    scope_typedef_entry_t typedef_entry = {
                        .kind = kind,
                        .name = strdup(typedef_name),
                        .type_desc = typedef_type_desc,
                    };
                    generator_add_typedef_entry(ctx, typedef_entry);
                    handled = true;
                }

                /* Check if there's a forward declaration or tagged reference (e.g. typedef struct Foo Foo) */
                for (size_t j = 0; j < specifiers_list->list.count && !handled; ++j)
                {
                    c_grammar_node_t * spec_child = specifiers_list->list.children[j];
                    {
                        for (size_t k = 0; k < spec_child->list.count; ++k)
                        {
                            c_grammar_node_t const * type_child = spec_child->list.children[k];
                            type_kind_t kind = TYPE_KIND_BUILTIN;
                            if (type_child->type == AST_NODE_STRUCT_TYPE_REF)
                            {
                                kind = TYPE_KIND_STRUCT;
                            }
                            else if (type_child->type == AST_NODE_UNION_TYPE_REF)
                            {
                                kind = TYPE_KIND_UNION;
                            }
                            else if (type_child->type == AST_NODE_ENUM_TYPE_REF)
                            {
                                kind = TYPE_KIND_ENUM;
                            }

                            if (kind != TYPE_KIND_BUILTIN)
                            {
                                c_grammar_node_t const * tag_name_node = type_child->type_ref.identifier;

                                if (tag_name_node != NULL && tag_name_node->type == AST_NODE_IDENTIFIER)
                                {
                                    generator_add_typedef_forward_decl(ctx, typedef_name, tag_name_node->text, kind);
                                    handled = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (handled)
                {
                    continue;
                }

                if (struct_def_node)
                {
                    /* We have a full struct/union definition */
                    char const * struct_tag = search_ast_for_type_tag(struct_def_node);
                    type_kind_t kind;
                    scope_typedef_entry_t typedef_entry = {0};
                    typedef_entry.name = strdup(typedef_name);

                    type_info_t const * type_info;

                    if (struct_tag != NULL)
                    {
                        kind = struct_def_node->type == AST_NODE_STRUCT_DEFINITION ? TYPE_KIND_STRUCT : TYPE_KIND_UNION;
                        type_info = register_tagged_struct_or_union_definition(
                            ctx, struct_def_node, struct_tag, kind, (TypeQualifier){0}
                        );
                        typedef_entry.tag = strdup(struct_tag);
                    }
                    else
                    {
                        kind = struct_def_node->type == AST_NODE_STRUCT_DEFINITION ? TYPE_KIND_UNTAGGED_STRUCT
                                                                                   : TYPE_KIND_UNTAGGED_UNION;
                        type_info = register_untagged_struct_or_union_definition(
                            ctx, struct_def_node, kind, &typedef_entry.untagged_index
                        );
                    }
                    typedef_entry.type_desc = type_info->type_desc;
                    typedef_entry.kind = kind;
                    generator_add_typedef_entry(ctx, typedef_entry);
                }
                else if (enum_def_node)
                {
                    /* Register the enum values as constants */
                    char const * enum_tag = search_ast_for_type_tag(enum_def_node);
                    scope_typedef_entry_t typedef_entry = {0};
                    typedef_entry.name = strdup(typedef_name);

                    type_info_t const * type_info;

                    if (enum_tag != NULL)
                    {
                        typedef_entry.kind = TYPE_KIND_ENUM;
                        typedef_entry.tag = strdup(enum_tag);
                        type_info = register_tagged_enum_definition(ctx, enum_def_node, enum_tag);
                    }
                    else
                    {
                        typedef_entry.kind = TYPE_KIND_UNTAGGED_ENUM;
                        type_info
                            = register_untagged_enum_definition(ctx, enum_def_node, &typedef_entry.untagged_index);
                    }
                    typedef_entry.type_desc = type_info->type_desc;
                    generator_add_typedef_entry(ctx, typedef_entry);
                }
                else
                {
                    /* Simple type typedef: e.g. typedef int my_int; */
                    c_grammar_node_t const * specifier_list = decl_specs->decl_specifiers.type_specifiers;
                    c_grammar_node_t const * qualifier_list = decl_specs->decl_specifiers.type_qualifiers;
                    TypeSpecifier const decl_type_spec = build_type_specifiers(specifier_list);
                    TypeQualifier const decl_type_qual = build_type_qualifiers(qualifier_list);
                    TypeDescriptor const * type_desc
                        = get_or_create_builtin_type(ctx->type_descriptors, decl_type_spec, decl_type_qual);
                    if (type_desc != NULL)
                    {
                        scope_typedef_entry_t typedef_entry = {0};
                        typedef_entry.name = strdup(typedef_name);
                        typedef_entry.type_desc = type_desc;
                        typedef_entry.kind = TYPE_KIND_BUILTIN;
                        generator_add_typedef_entry(ctx, typedef_entry);
                    }
                    else
                    {
                        debug_info("Failed to resolve type for typedef '%s'", typedef_name);
                        print_ast(decl_specs);
                    }
                }
            }
        }
        break;
    }
    case AST_NODE_FOR_STATEMENT:
    {
        c_grammar_node_t const * init_node = node->for_statement.init;
        c_grammar_node_t const * cond_node = node->for_statement.condition;
        c_grammar_node_t const * post_node = node->for_statement.post;
        c_grammar_node_t const * body_node = node->for_statement.body;

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_cond");
        LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_body");
        LLVMBasicBlockRef post_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_post");
        LLVMBasicBlockRef after_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "for_after");

        // Save and set break/continue targets for this loop
        LLVMBasicBlockRef old_break_target = ctx->break_target;
        LLVMBasicBlockRef old_continue_target = ctx->continue_target;
        ctx->break_target = after_block;
        ctx->continue_target = post_block;

        // 1. Process Init
        debug_info("process for init");
        process_ast_node(ctx, init_node);
        if (ctx->errors.fatal)
        {
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        LLVMBuildBr(ctx->builder, cond_block);

        // 2. Emit Cond block
        debug_info("process for cond");
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        TypedValue cond_res = process_expression(ctx, cond_node);
        if (ctx->errors.fatal)
        {
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        if (cond_res.value != NULL)
        {
            // Convert condition to bool (i1) if it's not already.
            cond_res = cast_typed_value_to_desc(
                ctx,
                cond_res,
                get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0})
            );
            LLVMBuildCondBr(ctx->builder, cond_res.value, body_block, after_block);
        }
        else
        {
            // Empty condition is always true
            LLVMBuildBr(ctx->builder, body_block);
        }

        // 3. Emit Body block
        debug_info("process for body");
        LLVMPositionBuilderAtEnd(ctx->builder, body_block);
        process_ast_node(ctx, body_node);
        if (ctx->errors.fatal)
        {
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        // If body doesn't have terminator, jump to post
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, post_block);
        }

        // 4. Emit Post block
        debug_info("process for post");
        LLVMPositionBuilderAtEnd(ctx->builder, post_block);
        process_expression(ctx, post_node);
        if (ctx->errors.fatal)
        {
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }
        LLVMBuildBr(ctx->builder, cond_block);

        // Restore old break/continue targets
        ctx->break_target = old_break_target;
        ctx->continue_target = old_continue_target;

        // 5. Continue from after block
        LLVMPositionBuilderAtEnd(ctx->builder, after_block);
        break;
    }
    case AST_NODE_WHILE_STATEMENT:
    {
        c_grammar_node_t const * condition_node = node->while_statement.condition;
        c_grammar_node_t const * body_node = node->while_statement.body;

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "while_cond");
        LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "while_body");
        LLVMBasicBlockRef after_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "while_after");

        // Save and set break/continue targets for this loop
        LLVMBasicBlockRef old_break_target = ctx->break_target;
        LLVMBasicBlockRef old_continue_target = ctx->continue_target;
        ctx->break_target = after_block;
        ctx->continue_target = cond_block;

        // Jump to condition block
        LLVMBuildBr(ctx->builder, cond_block);

        // --- Emit condition block ---
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        TypedValue condition_res = process_expression(ctx, condition_node);
        if (condition_res.value == NULL)
        {
            debug_error("Failed to process condition for WhileStatement.");
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        // Convert condition to bool (i1) if it's not already.
        condition_res = cast_typed_value_to_desc(
            ctx,
            condition_res,
            get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0})
        );

        LLVMBuildCondBr(ctx->builder, condition_res.value, body_block, after_block);

        // --- Emit body block ---
        LLVMPositionBuilderAtEnd(ctx->builder, body_block);
        process_ast_node(ctx, body_node);
        if (ctx->errors.fatal)
        {
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        // If the body block doesn't already have a terminator, jump back to condition
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, cond_block);
        }

        // Restore old break/continue targets
        ctx->break_target = old_break_target;
        ctx->continue_target = old_continue_target;

        // --- Continue from after block ---
        LLVMPositionBuilderAtEnd(ctx->builder, after_block);
        break;
    }
    case AST_NODE_DO_WHILE_STATEMENT:
    {
        c_grammar_node_t const * body_node = node->do_while_statement.body;
        c_grammar_node_t const * condition_node = node->do_while_statement.condition;

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "do_body");
        LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "do_cond");
        LLVMBasicBlockRef after_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "do_after");

        // Save and set break/continue targets for this loop
        LLVMBasicBlockRef old_break_target = ctx->break_target;
        LLVMBasicBlockRef old_continue_target = ctx->continue_target;
        ctx->break_target = after_block;
        ctx->continue_target = cond_block;

        // Jump to body block
        LLVMBuildBr(ctx->builder, body_block);

        // --- Emit body block ---
        LLVMPositionBuilderAtEnd(ctx->builder, body_block);
        process_ast_node(ctx, body_node);
        if (ctx->errors.fatal)
        {
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        // Jump to condition
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, cond_block);
        }

        // --- Emit condition block ---
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        TypedValue condition_res = process_expression(ctx, condition_node);
        if (condition_res.value == NULL)
        {
            debug_error("Failed to process condition for DoWhileStatement.");
            ctx->break_target = old_break_target;
            ctx->continue_target = old_continue_target;
            return;
        }

        // Convert condition to bool (i1) if it's not already.
        condition_res = cast_typed_value_to_desc(
            ctx,
            condition_res,
            get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0})
        );

        // Restore old break/continue targets
        ctx->break_target = old_break_target;
        ctx->continue_target = old_continue_target;

        LLVMBuildCondBr(ctx->builder, condition_res.value, body_block, after_block);
        // --- Continue from after block ---
        LLVMPositionBuilderAtEnd(ctx->builder, after_block);
        break;
    }
    case AST_NODE_CASE_LABEL:
    {
        // CaseLabel: [case_expr, (optional) statement]
        // If there's a statement, process it
        if (node->list.count == 2)
        {
            c_grammar_node_t * stmt = node->list.children[1];
            process_ast_node(ctx, stmt);
        }
        break;
    }
    case AST_NODE_BREAK_STATEMENT:
    {
        // Break statement: jump to the enclosing switch/loop's after block
        if (ctx->break_target)
        {
            LLVMBuildBr(ctx->builder, ctx->break_target);
        }
        else
        {
            debug_error("break statement not within a loop or switch.");
        }
        break;
    }
    case AST_NODE_CONTINUE_STATEMENT:
    {
        // Continue statement: jump to the enclosing loop's continue (post) block
        if (ctx->continue_target)
        {
            LLVMBuildBr(ctx->builder, ctx->continue_target);
        }
        else
        {
            debug_error("continue statement not within a loop.");
        }
        break;
    }
    case AST_NODE_SWITCH_STATEMENT:
    {
        // SwitchStatement: [SwitchExpression, CompoundStatement with SwitchCase/DefaultStatement]
        // SwitchCase: [case_label1, case_label2, ..., statement1, statement2, ...]
        //   - Case labels have type AST_NODE_CASE_LABEL
        //   - Statements are other statement types
        // DefaultStatement: [statement*]
        c_grammar_node_t const * switch_expr = node->switch_statement.expression;
        c_grammar_node_t const * body_stmt = node->switch_statement.body;

        TypedValue switch_val = process_expression(ctx, switch_expr);
        if (switch_val.value == NULL)
        {
            debug_error("Failed to process switch expression.");
            return;
        }
        switch_val = ensure_rvalue(ctx, "switch_rval", switch_val);

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        // Create after switch block
        LLVMBasicBlockRef after_switch = LLVMAppendBasicBlockInContext(ctx->context, current_func, "switch_after");

        // Save the old break target and set the new one
        LLVMBasicBlockRef old_break_target = ctx->break_target;
        ctx->break_target = after_switch;

        // Collect all SwitchCase and DefaultStatement items in order
        typedef struct
        {
            bool is_default;
            c_grammar_node_t const * node;
            LLVMBasicBlockRef body_block;
        } switch_item_t;

        size_t num_items = 0;
        size_t items_capacity = 16;
        switch_item_t * items = malloc(items_capacity * sizeof(*items));

        size_t default_idx = SIZE_MAX;

        if (body_stmt && body_stmt->type == AST_NODE_COMPOUND_STATEMENT)
        {
            for (size_t i = 0; i < body_stmt->list.count; ++i)
            {
                c_grammar_node_t * child = body_stmt->list.children[i];

                if (child->type == AST_NODE_SWITCH_CASE)
                {
                    if (num_items >= items_capacity)
                    {
                        items_capacity *= 2;
                        items = realloc(items, items_capacity * sizeof(*items));
                    }
                    items[num_items].is_default = false;
                    items[num_items].node = child;
                    items[num_items].body_block = NULL;
                    num_items++;
                }
                else if (child->type == AST_NODE_DEFAULT_STATEMENT)
                {
                    if (num_items >= items_capacity)
                    {
                        items_capacity *= 2;
                        items = realloc(items, items_capacity * sizeof(*items));
                    }
                    items[num_items].is_default = true;
                    items[num_items].node = child;
                    items[num_items].body_block = NULL;
                    default_idx = num_items;
                    num_items++;
                }
            }
        }

        // Create body blocks for all items that have statements
        for (size_t i = 0; i < num_items; i++)
        {
            c_grammar_node_t const * item_node = items[i].node;
            bool has_statements = item_node->switch_case.statements->list.count > 0;

            if (has_statements)
            {
                char block_name[64];
                if (items[i].is_default)
                {
                    snprintf(block_name, sizeof(block_name), "switch_default");
                }
                else
                {
                    snprintf(block_name, sizeof(block_name), "case_body_%zu", i);
                }
                items[i].body_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, block_name);
            }
        }

        // Create switch entry block
        LLVMBasicBlockRef switch_entry = LLVMAppendBasicBlockInContext(ctx->context, current_func, "switch_entry");
        LLVMBuildBr(ctx->builder, switch_entry);
        LLVMPositionBuilderAtEnd(ctx->builder, switch_entry);

        // Determine default target
        LLVMBasicBlockRef default_target
            = (default_idx != SIZE_MAX && items[default_idx].body_block) ? items[default_idx].body_block : after_switch;

        // Count case values for switch instruction
        size_t num_case_values = 0;
        for (size_t i = 0; i < num_items; i++)
        {
            if (!items[i].is_default)
            {
                // Count case labels in this SwitchCase
                num_case_values = items[i].node->switch_case.labels->list.count;
            }
        }

        LLVMValueRef switch_inst
            = LLVMBuildSwitch(ctx->builder, switch_val.value, default_target, (unsigned)num_case_values);

        // Add all cases to switch
        for (size_t i = 0; i < num_items; i++)
        {
            if (items[i].is_default)
            {
                continue;
            }

            // Find fallthrough target (next item with a body block)
            LLVMBasicBlockRef fallthrough_target = after_switch;
            for (size_t j = i + 1; j < num_items; j++)
            {
                if (items[j].body_block)
                {
                    fallthrough_target = items[j].body_block;
                    break;
                }
            }

            // Add each case value from this SwitchCase
            c_grammar_node_t const * switch_case_node = items[i].node;
            for (size_t j = 0; j < switch_case_node->switch_case.labels->list.count; j++)
            {
                c_grammar_node_t const * child = switch_case_node->switch_case.labels->list.children[j];
                // CaseLabel contains the case expression
                if (child->list.count >= 1)
                {
                    TypedValue case_val = process_expression(ctx, child->list.children[0]);
                    if (case_val.value == NULL)
                    {
                        return;
                    }
                    case_val = ensure_rvalue(ctx, "switch_case_rval", case_val);
                    LLVMAddCase(
                        switch_inst, case_val.value, items[i].body_block ? items[i].body_block : fallthrough_target
                    );
                }
            }
        }

        // Process bodies in forward order
        for (size_t i = 0; i < num_items; i++)
        {
            if (!items[i].body_block)
            {
                continue; // No body to process (empty case that falls through)
            }

            LLVMPositionBuilderAtEnd(ctx->builder, items[i].body_block);

            // Find fallthrough target
            LLVMBasicBlockRef fallthrough_target = after_switch;
            for (size_t j = i + 1; j < num_items; j++)
            {
                if (items[j].body_block)
                {
                    fallthrough_target = items[j].body_block;
                    break;
                }
            }

            // Process all statement children (skip CaseLabel children in SwitchCase)
            c_grammar_node_t const * item_node = items[i].node;
            for (size_t j = 0; j < item_node->switch_case.statements->list.count; j++)
            {
                c_grammar_node_t const * child = item_node->switch_case.statements->list.children[j];
                process_ast_node(ctx, child);
                if (ctx->errors.fatal)
                {
                    return;
                }

                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                {
                    break;
                }
            }

            // Add fallthrough if no terminator
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
            {
                LLVMBuildBr(ctx->builder, fallthrough_target);
            }
        }

        // Restore break target
        ctx->break_target = old_break_target;

        // Continue from after switch
        LLVMPositionBuilderAtEnd(ctx->builder, after_switch);

        free(items);
        break;
    }
    case AST_NODE_IF_STATEMENT:
    {
        debug_info("XXX");
        // AST structure for IfStatement: [ConditionExpression, ThenStatement, (Optional) ElseStatement]
        c_grammar_node_t const * condition_node = node->if_statement.condition;
        c_grammar_node_t const * then_node = node->if_statement.then_statement;
        c_grammar_node_t const * else_node = node->if_statement.else_statement;

        TypedValue condition_val = process_expression(ctx, condition_node);
        if (condition_val.value == NULL)
        {
            debug_error("Failed to process condition for IfStatement.");
            return;
        }
        condition_val = ensure_rvalue(ctx, "if_cond_rval", condition_val);

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "then");
        LLVMBasicBlockRef else_block
            = else_node != NULL ? LLVMAppendBasicBlockInContext(ctx->context, current_func, "else") : NULL;
        LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "if_merge");

        // If it's an i32, we need to check if it's != 0 to get an i1
        LLVMValueRef cond_val = condition_val.value;
        condition_val = cast_typed_value_to_desc(
            ctx,
            condition_val,
            get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0})
        );

        if (else_node)
        {
            LLVMBuildCondBr(ctx->builder, cond_val, then_block, else_block);
        }
        else
        {
            LLVMBuildCondBr(ctx->builder, cond_val, then_block, merge_block);
        }

        // --- Emit 'then' block ---
        LLVMPositionBuilderAtEnd(ctx->builder, then_block);
        process_ast_node(ctx, then_node);
        if (ctx->errors.fatal)
        {
            return;
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, merge_block);
        }

        // --- Emit 'else' block if present ---
        if (else_node != NULL)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, else_block);
            process_ast_node(ctx, else_node);
            if (ctx->errors.fatal)
            {
                return;
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
            {
                LLVMBuildBr(ctx->builder, merge_block);
            }
        }

        // --- Continue from merge block ---
        LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
        break;
    }
    case AST_NODE_RETURN_STATEMENT:
    {
        process_return_statement(ctx, node);
        break;
    }
    case AST_NODE_GOTO_STATEMENT:
    {
        char const * label_name = node->goto_statement.label->text;
        LLVMBasicBlockRef target = generator_get_or_create_label(ctx, label_name);
        LLVMBuildBr(ctx->builder, target);

        // Start a new basic block for any code after goto (which is technically unreachable
        // unless there's a label).
        LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef unreachable = LLVMAppendBasicBlockInContext(ctx->context, func, "unreachable");
        LLVMPositionBuilderAtEnd(ctx->builder, unreachable);

        break;
    }
    case AST_NODE_LABELED_STATEMENT:
    {
        // LabeledIdentifier children: [Identifier, Statement]

        c_grammar_node_t const * label_node = node->labeled_statement.label;
        c_grammar_node_t const * statement_node = node->labeled_statement.statement;

        char const * label_name = label_node->text;
        LLVMBasicBlockRef label_block = generator_get_or_create_label(ctx, label_name);

        // If the current block doesn't have a terminator, branch to the label block
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, label_block);
        }

        // Continue building from the label block
        LLVMPositionBuilderAtEnd(ctx->builder, label_block);

        // Process the statement part of the labeled statement
        process_ast_node(ctx, statement_node);
        if (ctx->errors.fatal)
        {
            return;
        }
        break;
    }
    case AST_NODE_POSTFIX_PARTS:
    case AST_NODE_STRUCT_DEFINITION:
    case AST_NODE_UNION_DEFINITION:
    {
        /* Probably a bug to see these nodes at this level. */
        debug_error("Received AST node type %s at top level", get_node_type_name_from_type(node->type));
        break;
    }

    case AST_NODE_ASSIGNMENT:
    case AST_NODE_FLOAT_BASE:
    case AST_NODE_INTEGER_LITERAL:
    case AST_NODE_FLOAT_LITERAL:
    case AST_NODE_STRING_LITERAL:
    case AST_NODE_LITERAL_SUFFIX:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_NAMED_DECL_SPECIFIERS:
    case AST_NODE_TYPE_SPECIFIER:
    case AST_NODE_TYPEDEF_SPECIFIER:
    case AST_NODE_UNARY_OPERATOR:
    case AST_NODE_UNARY_EXPRESSION_PREFIX:
    case AST_NODE_DECLARATOR:
    case AST_NODE_DIRECT_DECLARATOR:
    case AST_NODE_DECLARATOR_SUFFIX:
    case AST_NODE_POINTER:
    case AST_NODE_RELATIONAL_OPERATOR:
    case AST_NODE_RELATIONAL_EXPRESSION:
    case AST_NODE_EQUALITY_OPERATOR:
    case AST_NODE_EQUALITY_EXPRESSION:
    case AST_NODE_BITWISE_EXPRESSION:
    case AST_NODE_LOGICAL_EXPRESSION:
    case AST_NODE_SHIFT_OPERATOR:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_ARITHMETIC_OPERATOR:
    case AST_NODE_ARITHMETIC_EXPRESSION:
    case AST_NODE_OPTIONAL_ARGUMENT_LIST:
    case AST_NODE_POSTFIX_EXPRESSION:
    case AST_NODE_POSTFIX_OPERATOR:
    case AST_NODE_ARRAY_SUBSCRIPT:
    case AST_NODE_MEMBER_ACCESS_DOT:
    case AST_NODE_MEMBER_ACCESS_ARROW:
    case AST_NODE_CAST_EXPRESSION:
    case AST_NODE_TYPE_NAME:
    case AST_NODE_INITIALIZER_LIST:
    case AST_NODE_CHARACTER_LITERAL:
    case AST_NODE_SWITCH_CASE:
    case AST_NODE_DEFAULT_STATEMENT:
    case AST_NODE_ASSIGNMENT_OPERATOR:
    case AST_NODE_INTEGER_BASE:
    case AST_NODE_INIT_DECLARATOR:
    case AST_NODE_OPTIONAL_KW_EXTENSION:
    case AST_NODE_INIT_DECLARATOR_LIST:
    case AST_NODE_TERNARY_OPERATION:
    case AST_NODE_ENUM_DEFINITION:
    case AST_NODE_ENUMERATOR:
    case AST_NODE_COMMA_EXPRESSION:
    case AST_NODE_CONDITIONAL_EXPRESSION:
    case AST_NODE_FUNCTION_POINTER_DECLARATOR:
    case AST_NODE_DESIGNATION:
    case AST_NODE_COMPOUND_LITERAL:
    case AST_NODE_STRUCT_DECLARATOR:
    case AST_NODE_STRUCT_DECLARATOR_BITFIELD:
    case AST_NODE_STRUCT_TYPE_REF:
    case AST_NODE_UNION_TYPE_REF:
    case AST_NODE_ENUM_TYPE_REF:
    case AST_NODE_STRUCT_DECLARATION:
    case AST_NODE_STRUCT_DECLARATION_LIST:
    case AST_NODE_ASM_STATEMENT:
    case AST_NODE_STRUCT_DECLARATOR_LIST:
    case AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST:
    case AST_NODE_CASE_LABELS:
    case AST_NODE_SWITCH_BODY_STATEMENTS:
    case AST_NODE_TYPEDEF_INIT_DECLARATION_LIST:
    case AST_NODE_ATTRIBUTE_LIST:
    case AST_NODE_INITIALIZER:
    case AST_NODE_ASM_NAMES:
    case AST_NODE_TYPEDEF_DECLARATOR:
    case AST_NODE_TYPEDEF_DIRECT_DECLARATOR:
    case AST_NODE_TYPEDEF_INIT_DECLARATOR:
    case AST_NODE_DECLARATOR_SUFFIX_LIST:
    case AST_NODE_POINTER_LIST:
    case AST_NODE_INITIALIZER_LIST_ENTRY:
    case AST_NODE_ENUMERATOR_LIST:
    case AST_NODE_ATTRIBUTE:
    case AST_NODE_BITWISE_OPERATOR:
    case AST_NODE_LOGICAL_OPERATOR:
    case AST_NODE_ABSTRACT_DECLARATOR:
    case AST_NODE_STORAGE_CLASS_SPECIFIER:
    case AST_NODE_FUNCTION_SPECIFIER:
    case AST_NODE_TYPE_QUALIFIER:
    case AST_NODE_TYPE_QUALIFERS:
    case AST_NODE_DECLARATION_SPECIFIERS:
    case AST_NODE_STORAGE_CLASS_SPECIFIERS:
    case AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER:
    case AST_NODE_TYPE_SPECIFIERS:
    case AST_NODE_PARAMETER_LIST:
    case AST_NODE_ELLIPSIS:
    default:
        print_ast_with_label(node, "unhandled");
        debug_error("%s: Unhandled node", __func__);
        break;
    }
}

static void
process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (ctx->errors.fatal)
    {
        return; /* Stop processing if a fatal error has occurred */
    }

    debug_info("%s node type: %s (%u)\n", __func__, get_node_type_name_from_node(node), node->type);

    _process_ast_node(ctx, node);
    debug_info("processed: %s", get_node_type_name_from_node(node));
}

// --- LLVM IR Helper Functions ---

/**
 * @brief Writes the LLVM IR module to a file in human-readable format.
 * @param module The LLVM module to write.
 * @param file_path The path to the output file.
 * @return 0 on success, -1 on failure.
 */
int
write_llvm_ir_to_file(LLVMModuleRef module, char const * file_path)
{
    if (!module || !file_path)
    {
        debug_error("Invalid module or file path for writing IR.");
        return -1;
    }

    char * error_message = NULL;
    // LLVMPrintModuleToFile writes human-readable IR.
    if (LLVMPrintModuleToFile(module, file_path, &error_message))
    {
        debug_error("Failed to write LLVM IR to file '%s': %s", file_path, error_message);
        LLVMDisposeMessage(error_message); // Dispose the error message string
        return -1;
    }

    // If successful, error_message will be NULL.
    debug_info("IRGen: Successfully wrote LLVM IR to %s", file_path);
    return 0;
}

/**
 * @brief Compiles the LLVM module to an object file or assembly file.
 * @param module The LLVM module to compile.
 * @param file_path The path to the output file.
 * @param march The target architecture (e.g., "x86-64").
 * @param file_type The type of file to emit (LLVMObjectFile or LLVMAssemblyFile).
 * @return 0 on success, -1 on failure.
 */
int
emit_to_file(LLVMModuleRef module, char const * file_path, char const * march, LLVMCodeGenFileType file_type)
{
    if (!module || !file_path)
    {
        debug_error("Invalid module or file path for emission.");
        return -1;
    }

    // Initialize X86 target
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    char * error = NULL;
    char * triple = LLVMGetDefaultTargetTriple();

    // If march is specified, we might want to adjust the triple or find a specific target.
    // For now, we'll support "x86-64" by ensuring the triple matches.
    if (march && strcmp(march, "x86-64") == 0)
    {
        // On many systems, the default triple will already be x86_64-...
        // If we want to be explicit, we can override it.
        // LLVMDisposeMessage(triple);
        // triple = strdup("x86_64-pc-linux-gnu"); // Example explicit triple
    }

    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(triple, &target, &error))
    {
        debug_error("Failed to get target from triple '%s': %s", triple, error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return -1;
    }

    // Create target machine
    // Use generic CPU and no features for now.
    LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
        target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
    );

    if (!target_machine)
    {
        debug_error("Failed to create target machine.");
        LLVMDisposeMessage(triple);
        return -1;
    }

    // Set module's data layout and triple
    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
    char * data_layout_str = LLVMCopyStringRepOfTargetData(data_layout);
    LLVMSetDataLayout(module, data_layout_str);
    LLVMSetTarget(module, triple);

    // Emit to file
    if (LLVMTargetMachineEmitToFile(target_machine, module, (char *)file_path, file_type, &error))
    {
        debug_error("Failed to emit file '%s': %s", file_path, error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(data_layout_str);
        LLVMDisposeTargetData(data_layout);
        LLVMDisposeTargetMachine(target_machine);
        LLVMDisposeMessage(triple);
        return -1;
    }

    debug_info(
        "IRGen: Successfully emitted %s to %s", (file_type == LLVMObjectFile) ? "object code" : "assembly", file_path
    );

    // Cleanup
    LLVMDisposeMessage(data_layout_str);
    LLVMDisposeTargetData(data_layout);
    LLVMDisposeTargetMachine(target_machine);
    LLVMDisposeMessage(triple);

    return 0;
}

/**
 * @brief Gets the LLVM ValueRef representing the pointer to a variable.
 * Looks up the symbol in the symbol table.
 * @param ctx The IR generator context.
 * @param identifier_node The AST node for the identifier.
 * @param out_type Pointer to store the found LLVMTypeRef (element type).
 * @return The LLVM ValueRef (pointer) if found, NULL otherwise.
 */
static TypedValue
get_variable_pointer(ir_generator_ctx_t * ctx, c_grammar_node_t const * identifier_node)
{
    if (identifier_node == NULL || identifier_node->type != AST_NODE_IDENTIFIER || identifier_node->text == NULL)
    {
        debug_error("Invalid identifier node for get_variable_pointer.");
        return NullTypedValue;
    }

    TypedValue res = NullTypedValue;
    debug_info("%s: %s", __func__, identifier_node->text);
    generator_lookup_symbol_value(ctx, identifier_node->text, &res);

    return res;
}

static TypedValue
process_integer_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * _node)
{
    ast_node_integer_literal_t const * int_node = &_node->integer_lit;
    TypeSpecifier specifier = {
        .is_int = true,
        .is_unsigned = int_node->integer_literal.is_unsigned,
        .long_count = int_node->integer_literal.long_count,
    };
    TypeDescriptor const * type_desc = get_or_create_builtin_type(ctx->type_descriptors, specifier, (TypeQualifier){0});
    LLVMValueRef val
        = LLVMConstInt(type_desc->llvm_type, int_node->integer_literal.value, !int_node->integer_literal.is_unsigned);

    return create_typed_value(val, type_desc, false);
}

static TypedValue
process_float_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * _node)
{
    ast_node_float_literal_t const * float_node = &_node->float_lit;
    unsigned long_count = float_node->float_literal.type == FLOAT_LITERAL_TYPE_LONG_DOUBLE;
    bool is_double = float_node->float_literal.type == FLOAT_LITERAL_TYPE_DOUBLE
                     || float_node->float_literal.type == FLOAT_LITERAL_TYPE_LONG_DOUBLE;
    TypeSpecifier specifier = {
        .is_float = float_node->float_literal.type == FLOAT_LITERAL_TYPE_FLOAT,
        .is_double = is_double,
        .long_count = long_count,
    };
    TypeDescriptor const * type_desc = get_or_create_builtin_type(ctx->type_descriptors, specifier, (TypeQualifier){0});
    LLVMValueRef val = LLVMConstReal(type_desc->llvm_type, float_node->float_literal.value);

    return create_typed_value(val, type_desc, false);
}

static TypedValue
process_string_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node->text == NULL)
        return NullTypedValue;

    char const * decoded = decode_string(node->text);
    size_t len = strlen(decoded);

    // 1. Get the base element type (char)
    TypeDescriptor const * char_type
        = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_char = true}, (TypeQualifier){0});

    // 2. We need a Pointer-to-Char descriptor for the returned TypedValue
    TypeDescriptor const * ptr_to_char
        = get_or_create_pointer_type(ctx->type_descriptors, char_type, (TypeQualifier){0});

    LLVMValueRef global_str;

    if (LLVMGetInsertBlock(ctx->builder) != NULL)
    {
        // LLVMBuildGlobalStringPtr returns a ptr to the i8 array (i8*)
        global_str = LLVMBuildGlobalStringPtr(ctx->builder, decoded, "str_tmp");
    }
    else
    {
        // For global scope, we need the array descriptor for the initializer size
        TypeDescriptor const * array_type = get_or_create_array_type(ctx->type_descriptors, char_type, len + 1);

        LLVMValueRef global = LLVMAddGlobal(ctx->module, array_type->llvm_type, "str_tmp");
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(global, true);
        LLVMSetInitializer(global, LLVMConstStringInContext(ctx->context, decoded, (unsigned)len, false));

        // Create a constant GEP to get the pointer to the first element
        LLVMValueRef indices[2]
            = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};
        global_str = LLVMConstInBoundsGEP2(array_type->llvm_type, global, indices, 2);
    }

    free((char *)decoded);

    // 3. Return the pointer value with the pointer descriptor
    return create_typed_value(global_str, ptr_to_char, false);
}

static TypedValue
process_character_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node->text == NULL)
    {
        return NullTypedValue;
    }

    char const * raw_text = node->text;
    // Character literal content (no quotes), e.g., "a" or "\\n"
    char value = 0;
    if (raw_text[0] == '\\')
    {
        switch (raw_text[1])
        {
        case 'n':
            value = '\n';
            break;
        case 't':
            value = '\t';
            break;
        case 'r':
            value = '\r';
            break;
        case '0':
            value = '\0';
            break;
        case '\\':
            value = '\\';
            break;
        case '\'':
            value = '\'';
            break;
        case '\"':
            value = '\"';
            break;
        default:
            value = raw_text[1];
            break;
        }
    }
    else
    {
        value = raw_text[0];
    }

    TypeDescriptor const * type_desc
        = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_char = true}, (TypeQualifier){0});

    return create_typed_value(LLVMConstInt(type_desc->llvm_type, value, false), type_desc, false);
}

static TypedValue
process_postfix_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * base_node = node->postfix_expression.base_expression;
    c_grammar_node_t const * postfix_parts_node = node->postfix_expression.postfix_parts;

    // 1. Resolve the base value.
    // In C, postfix expressions group left-to-right.
    // We start with the base and then "pipe" the result through each suffix.
    TypedValue current_val = process_expression(ctx, base_node);
    if (current_val.value == NULL)
    {
        return current_val;
    }

    for (size_t i = 0; i < postfix_parts_node->list.count; ++i)
    {
        c_grammar_node_t * suffix = postfix_parts_node->list.children[i];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"

        switch (suffix->type)
        {
        case AST_NODE_OPTIONAL_ARGUMENT_LIST:
        {
            // FUNCTION CALL: (args...)
            // Resolve what we are calling. If it's a function directly,
            // we call it. If it's a pointer to a function, we load it first.
            debug_info("%s handling args list", __func__);
            dump_typed_value("current_val before rvalue", current_val);
            TypedValue callable = ensure_rvalue(ctx, "func_ptr_load", current_val);
            debug_info("%s rvalue", __func__);
            dump_typed_value("callable", callable);

            TypeDescriptor const * func_desc = NULL;
            if (callable.type_info->kind == NCC_TYPE_KIND_FUNCTION)
            {
                func_desc = callable.type_info;
            }
            else if (
                callable.type_info->kind == NCC_TYPE_KIND_POINTER
                && callable.type_info->pointee->kind == NCC_TYPE_KIND_FUNCTION
            )
            {
                func_desc = callable.type_info->pointee;
            }

            if (!func_desc)
            {
                ir_gen_error(&ctx->errors, suffix, "Expression is not a function or function pointer");
                return NullTypedValue;
            }

            // Process Arguments
            size_t num_args = suffix->list.count;
            LLVMValueRef * call_args = num_args > 0 ? calloc(num_args, sizeof(LLVMValueRef)) : NULL;

            for (size_t j = 0; j < num_args; ++j)
            {
                TypedValue arg = process_expression(ctx, suffix->list.children[j]);
                if (arg.value == NULL)
                {
                    debug_error("Failed to process condition for IfStatement.");
                    return arg;
                }

                // Auto-cast to parameter type if available
                if (j < func_desc->function_metadata.param_count)
                {
                    arg = cast_typed_value_to_desc(ctx, arg, func_desc->function_metadata.params[j]);
                }
                else
                {
                    arg = ensure_rvalue(ctx, "vararg_load", arg);
                }
                call_args[j] = arg.value;
            }

            LLVMValueRef call_val = LLVMBuildCall2(
                ctx->builder,
                func_desc->llvm_type,
                callable.value,
                call_args,
                (unsigned)num_args,
                is_void_return(func_desc) ? "" : "call_tmp"
            );

            free(call_args);
            current_val = create_typed_value(call_val, func_desc->function_metadata.return_type, false);
            break;
        }

        case AST_NODE_ARRAY_SUBSCRIPT:
        {
            // ARRAY ACCESS: [index]
            // C rule: a[i] is identical to *(a + i).
            // Array types decay to pointers here.
            if (current_val.value == NULL)
            {
                debug_error("%s: NULL value while processing array subscript", __func__);
                return NullTypedValue;
            }
            if (current_val.type_info->kind == NCC_TYPE_KIND_ARRAY)
            {
                // Decay array to pointer to first element
                current_val = cast_typed_value_to_desc(
                    ctx,
                    current_val,
                    get_or_create_pointer_type(
                        ctx->type_descriptors, current_val.type_info->pointee, (TypeQualifier){0}
                    )
                );
            }

            current_val = ensure_rvalue(ctx, "ptr_subscript_load", current_val);
            current_val = process_array_subscript(ctx, suffix, current_val);
            break;
        }

        case AST_NODE_MEMBER_ACCESS_DOT:
        case AST_NODE_MEMBER_ACCESS_ARROW:
        {
            // MEMBER ACCESS: .member or ->member
            bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);
            char const * member_name = suffix->identifier.identifier->text;
            dump_typed_value("member_access", current_val);

            if (is_arrow)
            {
                if (current_val.type_info == NULL)
                {
                    debug_error("member access current value has no type");
                    return NullTypedValue;
                }
                // -> implies a pointer dereference first
                current_val = ensure_rvalue(ctx, "arrow_deref", current_val);
                if (current_val.type_info->kind != NCC_TYPE_KIND_POINTER)
                {
                    ir_gen_error(&ctx->errors, suffix, "Arrow operator used on non-pointer");
                    return NullTypedValue;
                }
                // Move the context to the struct being pointed to
                typed_value_switch_to_pointee(&current_val);
            }

            TypeDescriptor const * struct_desc = current_val.type_info;
            if (struct_desc->kind != NCC_TYPE_KIND_STRUCT && struct_desc->kind != NCC_TYPE_KIND_UNION)
            {
                ir_gen_error(&ctx->errors, suffix, "Member access on non-struct type");
                return NullTypedValue;
            }

            int field_idx = type_descriptor_find_struct_field_index_from_desc(struct_desc, member_name);
            if (field_idx < 0)
            {
                ir_gen_error(&ctx->errors, suffix, "Struct has no member named '%s'", member_name);
                return NullTypedValue;
            }

            struct_field_t const * field = type_descriptor_get_struct_field_type(struct_desc, field_idx);

            // Create the GEP
            LLVMValueRef indices[2]
                = {LLVMConstInt(ctx->ref_type.i32_type, 0, false),
                   LLVMConstInt(ctx->ref_type.i32_type, field->bitfield.storage_index, false)};

            LLVMValueRef member_ptr = LLVMBuildInBoundsGEP2(
                ctx->builder, struct_desc->llvm_type, current_val.value, indices, 2, "memberptr"
            );

            current_val = create_typed_value(member_ptr, field->type_desc, true);
            current_val.bitfield = field->bitfield;
            dump_typed_value("member_access", current_val);

            break;
        }

        case AST_NODE_POSTFIX_OPERATOR:
        {
            // POSTFIX: i++ / i--
            if (!current_val.is_lvalue)
            {
                ir_gen_error(&ctx->errors, suffix, "Postfix operator requires lvalue");
                return NullTypedValue;
            }

            if (current_val.type_info->qualifiers.is_const)
            {
                ir_gen_error(&ctx->errors, suffix, "Cannot increment/decrement const variable");
                return NullTypedValue;
            }

            TypedValue original_val = ensure_rvalue(ctx, "postfix_orig", current_val);
            LLVMValueRef modified;

            if (current_val.type_info->kind == NCC_TYPE_KIND_POINTER)
            {
                LLVMValueRef one = LLVMConstInt(ctx->ref_type.i32_type, 1, false);
                modified = LLVMBuildInBoundsGEP2(
                    ctx->builder, current_val.type_info->pointee->llvm_type, original_val.value, &one, 1, "ptr_step"
                );
            }
            else
            {
                LLVMValueRef one = is_floating_kind(current_val.type_info)
                                       ? LLVMConstReal(current_val.type_info->llvm_type, 1.0)
                                       : LLVMConstInt(current_val.type_info->llvm_type, 1, false);

                if (suffix->op.postfix.op == POSTFIX_OP_INC)
                    modified = is_floating_kind(current_val.type_info)
                                   ? LLVMBuildFAdd(ctx->builder, original_val.value, one, "inc")
                                   : LLVMBuildAdd(ctx->builder, original_val.value, one, "inc");
                else
                    modified = is_floating_kind(current_val.type_info)
                                   ? LLVMBuildFSub(ctx->builder, original_val.value, one, "dec")
                                   : LLVMBuildSub(ctx->builder, original_val.value, one, "dec");
            }

            aligned_store(ctx, ctx->builder, modified, current_val.type_info->llvm_type, current_val.value);
            current_val = original_val; // Result is the value BEFORE modification
            break;
        }
        }

#pragma GCC diagnostic pop
    }

    return current_val;
}

static TypedValue
process_cast_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // AST structure for CastExpression: [TypeName, CastExpression]
    c_grammar_node_t const * type_name_node = node->cast_expression.type_name;
    c_grammar_node_t const * inner_expr_node = node->cast_expression.expression;

    // TypeName children are [SpecifierQualifierList, AbstractDeclarator?]
    c_grammar_node_t const * spec_qual = type_name_node->type_name.specifier_qualifier_list;
    // TODO: Proper support for when the first child of spec_qual is a AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER.
    c_grammar_node_t const * abstract_decl = type_name_node->type_name.abstract_declarator;

    TypeDescriptor const * cast_to_type = resolve_type_descriptor(ctx, spec_qual, abstract_decl);
    TypedValue val_to_cast = process_expression(ctx, inner_expr_node);
    if (val_to_cast.value == NULL)
    {
        debug_error("Failed to process inner expression in cast.");
        return val_to_cast;
    }
    val_to_cast = ensure_rvalue(ctx, "cast_rval", val_to_cast);
    val_to_cast = cast_typed_value_to_desc(ctx, val_to_cast, cast_to_type);

    return val_to_cast;
}

static TypedValue
process_assignment(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * lhs_node = node->binary_expression.left;
    c_grammar_node_t const * rhs_node = node->binary_expression.right;

    TypedValue lhs_res = NullTypedValue;

    // Check if LHS is a PostfixExpression with array subscript or member access
    debug_info(
        "process_assignment: lhs_node type=%d (%s)", lhs_node->type, get_node_type_name_from_type(lhs_node->type)
    );
    if (lhs_node->type == AST_NODE_POSTFIX_EXPRESSION)
    {
        if (lhs_node->postfix_expression.postfix_parts != NULL
            && lhs_node->postfix_expression.postfix_parts->list.count > 0)
        {
            c_grammar_node_t const * first_suffix = lhs_node->postfix_expression.postfix_parts->list.children[0];
            if (first_suffix->type == AST_NODE_MEMBER_ACCESS_DOT || first_suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
            {
                TypedValue base_val = process_expression(ctx, lhs_node->postfix_expression.base_expression);
                if (base_val.type_info != NULL)
                {
                    bool is_const = base_val.type_info->qualifiers.is_const;
                    if (base_val.type_info->kind == NCC_TYPE_KIND_POINTER && base_val.type_info->pointee != NULL)
                    {
                        is_const = base_val.type_info->pointee->qualifiers.is_const;
                    }
                    if (is_const)
                    {
                        ir_gen_error(&ctx->errors, lhs_node, "Cannot assign to member of const variable");
                        return NullTypedValue;
                    }
                }
            }
        }

        lhs_res = process_expression(ctx, lhs_node);
        if (lhs_res.value == NULL)
        {
            return NullTypedValue;
        }
    }
    else if (lhs_node->type == AST_NODE_UNARY_EXPRESSION_PREFIX)
    {
        c_grammar_node_t const * op_node = lhs_node->unary_expression_prefix.op;
        if (op_node != NULL && op_node->op.unary.op == UNARY_OP_DEREF)
        {
            lhs_res = process_expression(ctx, lhs_node->unary_expression_prefix.operand);
            if (lhs_res.value == NULL)
            {
                debug_error("Failed to process LHS expression in assignment.");
                return NullTypedValue;
            }
            // ptr_val.value is the address of the pointer
            // We load it to get the address of the target
            lhs_res.value = aligned_load(ctx, ctx->builder, lhs_res.type_info->llvm_type, lhs_res.value, "ptr_deref");

            if (!typed_value_switch_to_pointee(&lhs_res))
            {
                ir_gen_error(&ctx->errors, op_node, "Failed to switch LHS to pointee type in assignment.");
                debug_error("Failed to switch LHS to pointee type.");
                return NullTypedValue;
            }
            lhs_res.is_lvalue = true;
        }
    }
    else
    {
        // Simple variable assignment
        lhs_res = get_variable_pointer(ctx, lhs_node);
        debug_info(
            "process_assignment (simple var): got type_info=%p, const=%d",
            (void *)lhs_res.type_info,
            lhs_res.type_info->qualifiers.is_const
        );
    }

    if (lhs_res.value == NULL)
    {
        debug_error("Could not get pointer for LHS in assignment.");
        return lhs_res;
    }
    if (!lhs_res.is_lvalue)
    {
        debug_error("expected LHS of assignment to be an lvalue, but it isn't");
        print_ast_with_label(lhs_node, "LHS");
    }

    debug_info(
        "assignment LHS type_info qualifiers: const=%d, volatile=%d",
        lhs_res.type_info->qualifiers.is_const,
        lhs_res.type_info->qualifiers.is_volatile
    );

    if (lhs_res.type_info->qualifiers.is_const)
    {
        ir_gen_error(&ctx->errors, lhs_node, "Cannot assign to const variable");

        return NullTypedValue;
    }

    // Check for compound assignment operators (+=, -=, *=, /=, %=, etc.)
    c_grammar_node_t const * op_node = node->binary_expression.op;
    assignment_operator_type_t assign_op_type = op_node->op.assign.op;

    bool is_compound = (assign_op_type != ASSIGN_OP_SIMPLE);

    TypedValue rhs_res;

    if (is_compound)
    {
        // For compound assignment, load current LHS value
        TypedValue lhs_rres = ensure_rvalue(ctx, "assign_compound_lhs_rval", lhs_res);
        LLVMValueRef lhs_value = lhs_rres.value;
        rhs_res = process_expression(ctx, rhs_node);
        if (rhs_res.value == NULL)
        {
            debug_error("Failed to process RHS expression in compound assignment.");
            return NullTypedValue;
        }

        rhs_res = ensure_rvalue(ctx, "assign_compound_rhs_rval", rhs_res);

        // Determine if this is a floating point operation
        bool is_float = is_floating_kind(lhs_res.type_info);

        // Perform the operation
        switch (assign_op_type)
        {
        case ASSIGN_OP_SIMPLE:
            /* Do nothing. Shouldn't happen. Avoids compiler warning. */
            break;
        case ASSIGN_OP_ADD:
            rhs_res.value = is_float ? LLVMBuildFAdd(ctx->builder, lhs_value, rhs_res.value, "fadd_tmp")
                                     : LLVMBuildAdd(ctx->builder, lhs_value, rhs_res.value, "add_tmp");
            break;
        case ASSIGN_OP_SUB:
            rhs_res.value = is_float ? LLVMBuildFSub(ctx->builder, lhs_value, rhs_res.value, "fsub_tmp")
                                     : LLVMBuildSub(ctx->builder, lhs_value, rhs_res.value, "sub_tmp");
            break;
        case ASSIGN_OP_MUL:
            rhs_res.value = is_float ? LLVMBuildFMul(ctx->builder, lhs_value, rhs_res.value, "fmul_tmp")
                                     : LLVMBuildMul(ctx->builder, lhs_value, rhs_res.value, "mul_tmp");
            break;
        case ASSIGN_OP_DIV:
            rhs_res.value = is_float ? LLVMBuildFDiv(ctx->builder, lhs_value, rhs_res.value, "fdiv_tmp")
                                     : LLVMBuildSDiv(ctx->builder, lhs_value, rhs_res.value, "div_tmp");
            break;
        case ASSIGN_OP_MOD:
            rhs_res.value = LLVMBuildSRem(ctx->builder, lhs_value, rhs_res.value, "rem_tmp");
            break;
        case ASSIGN_OP_AND:
            rhs_res.value = LLVMBuildAnd(ctx->builder, lhs_value, rhs_res.value, "and_tmp");
            break;
        case ASSIGN_OP_OR:
            rhs_res.value = LLVMBuildOr(ctx->builder, lhs_value, rhs_res.value, "or_tmp");
            break;
        case ASSIGN_OP_XOR:
            rhs_res.value = LLVMBuildXor(ctx->builder, lhs_value, rhs_res.value, "xor_tmp");
            break;
        case ASSIGN_OP_SHL:
            rhs_res.value = LLVMBuildShl(ctx->builder, lhs_value, rhs_res.value, "shl_tmp");
            break;
        case ASSIGN_OP_SHR:
            rhs_res.value = LLVMBuildLShr(ctx->builder, lhs_value, rhs_res.value, "lshr_tmp");
            break;
        default:
            debug_error("Unknown compound assignment operator.");
            return NullTypedValue;
        }
    }
    else
    {
        // Process the RHS expression to get its LLVM ValueRef.
        rhs_res = process_expression(ctx, rhs_node);
        if (rhs_res.value == NULL)
        {
            debug_error("Failed to process RHS expression in assignment.");
            return rhs_res;
        }
        rhs_res = ensure_rvalue(ctx, "assign_rhs_rval", rhs_res);
    }

    // Generate the store instruction.
    // if (is_bitfield_assign && bitfield_storage_unit_type != NULL)
    struct_bitfield_data_t const bitfield = lhs_res.bitfield;
    if (bitfield.bit_width > 0)
    {
        unsigned bit_width = bitfield.bit_width;
        unsigned bit_offset = bitfield.bit_offset;

        debug_info("assigning to bitfield");
        /* * 1. Target the storage unit.
         * bitfield_storage_unit_ptr is the GEP result (the address of the i32/i64).
         * bitfield_storage_unit_type is the type of that storage unit (e.g., i32).
         */
        // TypedValue lhs_res_rval = ensure_rvalue(ctx, "assign_bitfield_lhs_rval", lhs_res);
        LLVMValueRef container
            = aligned_load(ctx, ctx->builder, lhs_res.type_info->llvm_type, lhs_res.value, "assign_bitfield_lhs_rval");

        LLVMTypeRef storage_type = lhs_res.type_info->llvm_type;

        // 2. LOAD only the affected storage unit
        LLVMValueRef current_val = container;

        // 3. PREPARE MASKS
        // width_mask: (1 << width) - 1
        unsigned long long width_mask = (bit_width == 64) ? ~0ULL : (1ULL << bit_width) - 1;
        // clear_mask: ~ (width_mask << offset)
        unsigned long long clear_mask = ~(width_mask << bit_offset);

        LLVMValueRef llvm_clear_mask = LLVMConstInt(storage_type, clear_mask, false);
        LLVMValueRef llvm_width_mask = LLVMConstInt(storage_type, width_mask, false);
        LLVMValueRef llvm_offset = LLVMConstInt(ctx->ref_type.i32_type, bit_offset, false);

        // 4. MODIFY
        // Clear the old bits
        LLVMValueRef cleared = LLVMBuildAnd(ctx->builder, current_val, llvm_clear_mask, "bf_clear");

        // Mask the RHS value to ensure it doesn't overflow its width
        TypedValue casted_rhs = cast_typed_value_to_desc(ctx, rhs_res, lhs_res.type_info);
        LLVMValueRef masked_rhs = LLVMBuildAnd(ctx->builder, casted_rhs.value, llvm_width_mask, "bf_rhs_clip");

        // Shift new value into position
        LLVMValueRef shifted_rhs = LLVMBuildShl(ctx->builder, masked_rhs, llvm_offset, "bf_shift");

        // Combine
        LLVMValueRef final_val = LLVMBuildOr(ctx->builder, cleared, shifted_rhs, "bf_merge");

        // 5. STORE only the affected storage unit back
        aligned_store(ctx, ctx->builder, final_val, storage_type, lhs_res.value);
    }
    else
    {
        // Ensure the RHS is cast to the LHS type before storing
        rhs_res = cast_typed_value_to_desc(ctx, rhs_res, lhs_res.type_info);
        aligned_store(ctx, ctx->builder, rhs_res.value, lhs_res.type_info->llvm_type, lhs_res.value);
    }

    rhs_res.is_lvalue = false;
    return rhs_res;
}

static TypedValue
process_identifier(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    debug_info("%s node type: %s", __func__, get_node_type_name_from_type(node->type));

    // Handle built-in boolean constants
    if (node != NULL && node->text != NULL)
    {
        if (strcmp(node->text, "true") == 0)
        {
            TypeDescriptor const * bool_desc = get_or_create_builtin_type(
                ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){.is_const = true}
            );
            return create_typed_value(LLVMConstInt(bool_desc->llvm_type, 1, false), bool_desc, false);
        }
        if (strcmp(node->text, "false") == 0)
        {
            TypeDescriptor const * bool_desc = get_or_create_builtin_type(
                ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0}
            );
            return create_typed_value(LLVMConstInt(bool_desc->llvm_type, 0, false), bool_desc, false);
        }
    }

    // Get the variable's pointer and its element type from the symbol table.
    TypedValue var_res = get_variable_pointer(ctx, node);

    if (var_res.value != NULL)
    {
        // Check if the symbol is an integer constant (like enum values)
        // These are global i32 values, not pointers - we can just return them directly
        // But only for globals that are marked as constants (e.g., enum values, const globals),
        // not for regular static/global variables (which can be modified)
        if (LLVMGetTypeKind(var_res.type_info->llvm_type) == LLVMIntegerTypeKind && LLVMIsAGlobalValue(var_res.value))
        {
            LLVMValueRef initializer = LLVMGetInitializer(var_res.value);
            // Only return initializer directly for actual constants (LLVMIsGlobalConstant)
            // For non-const globals like "static int x;", we need to load from the global
            if (initializer != NULL && LLVMIsGlobalConstant(var_res.value))
            {
                var_res.value = initializer;

                return var_res;
            }
        }

        // Check if the type is an array (for file-scope or local arrays)
        if (var_res.type_info->kind == NCC_TYPE_KIND_ARRAY)
        {
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
            indices[1] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
            TypedValue arr_res = create_typed_value(
                LLVMBuildInBoundsGEP2(
                    ctx->builder, var_res.type_info->llvm_type, var_res.value, indices, 2, "array_ptr"
                ),
                var_res.type_info,
                false
            );

            return arr_res;
        }

        return var_res;
    }
    else
    {
        debug_info("%s: no symbol found - check functions for %s", __func__, node->text);
        // Check if it's a function name - return the function pointer

        struct function_decl_entry * decl_entry = find_function_declaration(ctx, node->text);
        if (decl_entry != NULL)
        {
            dump_typed_value("existing_func", decl_entry->func);
            return decl_entry->func;
        }
        /* TOOD: Else what? */
    }

    debug_error("NULL element type for variable '%s'.", node->text);
    ir_gen_error(&ctx->errors, node, "Unknown variable: '%s'", node->text);

    return NullTypedValue;
}

static TypedValue
process_bitwise_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Bitwise ops from chainl1: [LHS, RHS], operator is implied by node type
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "bitwise_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    rhs_res = ensure_rvalue(ctx, "bitwise_rhs_rval", rhs_res);
    if (rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    TypeDescriptor const * lhs_type = lhs_res.type_info;
    TypeDescriptor const * rhs_type = rhs_res.type_info;

    TypedValue res = lhs_res;

    // Handle type promotion for integer operands - both sides must match
    if (is_integer_kind(lhs_type) && is_integer_kind(rhs_type))
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type->llvm_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type->llvm_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_res.value = LLVMBuildZExt(ctx->builder, rhs_res.value, lhs_type->llvm_type, "promote_rhs");
            rhs_res.type_info = lhs_type;
            res.type_info = lhs_type;
            /* Default to lhs value just so a value is always assigned.*/
            res.value = lhs_res.value;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_res.value = LLVMBuildZExt(ctx->builder, lhs_res.value, rhs_type->llvm_type, "promote_lhs");
            lhs_res.type_info = rhs_type;
            res.type_info = rhs_type;
            /* Default to rhs value just so a value is always assigned.*/
            res.value = rhs_res.value;
        }
    }

    c_grammar_node_t const * op_node = node->binary_expression.op;
    bitwise_operator_type_t operator= op_node->op.bitwise.op;

    switch (operator)
    {
    case BITWISE_OP_AND:
        res.value = LLVMBuildAnd(ctx->builder, lhs_res.value, rhs_res.value, "and_tmp");
        break;
    case BITWISE_OP_OR:
        res.value = LLVMBuildOr(ctx->builder, lhs_res.value, rhs_res.value, "or_tmp");
        break;
    case BITWISE_OP_XOR:
        res.value = LLVMBuildXor(ctx->builder, lhs_res.value, rhs_res.value, "xor_tmp");
        break;
    }

    return res;
}

static TypedValue
process_shift_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "shift_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    if (rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    rhs_res = ensure_rvalue(ctx, "shift_rhs_rval", rhs_res);

    // Ensure shift amount has same integer width as lhs
    TypeDescriptor const * lhs_type = lhs_res.type_info;
    TypeDescriptor const * rhs_type = rhs_res.type_info;

    TypedValue res = lhs_res; /* Default to LHS value so something is always assigned. */

    if (is_integer_kind(lhs_type) && is_integer_kind(rhs_type))
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type->llvm_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type->llvm_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_res.value = LLVMBuildZExt(ctx->builder, rhs_res.value, lhs_type->llvm_type, "promote_shift_rhs");
        }
        else if (rhs_bits > lhs_bits)
        {
            // Shift amount larger than lhs width: truncate to lhs width
            rhs_res.value = LLVMBuildTrunc(ctx->builder, rhs_res.value, lhs_type->llvm_type, "trunc_shift_rhs");
        }
    }

    c_grammar_node_t const * op_node = node->binary_expression.op;
    shift_operator_type_t operator= op_node->op.shift.op;

    switch (operator)
    {
    case SHIFT_OP_LL:
        res.value = LLVMBuildShl(ctx->builder, lhs_res.value, rhs_res.value, "shl_tmp");
        break;
    case SHIFT_OP_AR:
        res.value = LLVMBuildAShr(ctx->builder, lhs_res.value, rhs_res.value, "ashr_tmp");
        break;
    }

    return res;
}

static TypedValue
process_arithmetic_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "arith_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    if (rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    rhs_res = ensure_rvalue(ctx, "arith_rhs_rval", rhs_res);
    TypeDescriptor const * lhs_type = lhs_res.type_info;
    TypeDescriptor const * rhs_type = rhs_res.type_info;

    bool is_float_op = is_floating_kind(lhs_type) || is_floating_kind(rhs_type);

    // Handle type promotion for integer operands
    // If lhs is wider than rhs (e.g., long vs int), promote rhs to match
    TypedValue res = lhs_res;

    if (!is_float_op && is_integer_kind(lhs_type) && is_integer_kind(rhs_type))
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type->llvm_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type->llvm_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_res.value = LLVMBuildSExt(ctx->builder, rhs_res.value, lhs_type->llvm_type, "promote_rhs");
            res.type_info = lhs_type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_res.value = LLVMBuildSExt(ctx->builder, lhs_res.value, rhs_type->llvm_type, "promote_lhs");
            res.type_info = rhs_type;
        }
    }

    debug_info("result bit width: %u", LLVMGetIntTypeWidth(lhs_type->llvm_type));
    c_grammar_node_t const * op_node = node->binary_expression.op;
    arithmetic_operator_type_t operator= op_node->op.arith.op;

    switch (operator)
    {
    case ARITH_OP_ADD:
        res.value = is_float_op ? LLVMBuildFAdd(ctx->builder, lhs_res.value, rhs_res.value, "arith_fadd_tmp")
                                : LLVMBuildAdd(ctx->builder, lhs_res.value, rhs_res.value, "arith_add_tmp");
        break;
    case ARITH_OP_SUB:
        res.value = is_float_op ? LLVMBuildFSub(ctx->builder, lhs_res.value, rhs_res.value, "arith_fsub_tmp")
                                : LLVMBuildSub(ctx->builder, lhs_res.value, rhs_res.value, "arith_sub_tmp");
        break;
    case ARITH_OP_MUL:
        res.value = is_float_op ? LLVMBuildFMul(ctx->builder, lhs_res.value, rhs_res.value, "arith_fmul_tmp")
                                : LLVMBuildMul(ctx->builder, lhs_res.value, rhs_res.value, "arith_mul_tmp");
        break;
    case ARITH_OP_DIV:
        res.value = is_float_op ? LLVMBuildFDiv(ctx->builder, lhs_res.value, rhs_res.value, "arith_fdiv_tmp")
                                : LLVMBuildSDiv(ctx->builder, lhs_res.value, rhs_res.value, "arith_div_tmp");
        break;
    case ARITH_OP_MOD:
        res.value = LLVMBuildSRem(ctx->builder, lhs_res.value, rhs_res.value, "arith_rem_tmp");
        break;
    }

    res.is_lvalue = false;

    return res;
}

static TypedValue
process_relational_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "rel_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    if (rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    rhs_res = ensure_rvalue(ctx, "rel_rhs_r_val", rhs_res);

    TypeDescriptor const * lhs_type = lhs_res.type_info;

    rhs_res = cast_typed_value_to_desc(ctx, rhs_res, lhs_type);

    bool is_float_op = is_floating_kind(lhs_type);

    c_grammar_node_t const * op_node = node->binary_expression.op;
    relational_operator_type_t operator= op_node->op.rel.op;

    TypeDescriptor const * bool_type
        = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0});
    LLVMValueRef value = NULL;

    switch (operator)
    {
    case REL_OP_LT:
        value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs_res.value, rhs_res.value, "flt_tmp")
                            : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs_res.value, rhs_res.value, "lt_tmp");
        break;
    case REL_OP_GT:
        value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs_res.value, rhs_res.value, "fgt_tmp")
                            : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs_res.value, rhs_res.value, "gt_tmp");
        break;
    case REL_OP_LE:
        value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs_res.value, rhs_res.value, "fle_tmp")
                            : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs_res.value, rhs_res.value, "le_tmp");
        break;
    case REL_OP_GE:
        value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs_res.value, rhs_res.value, "fge_tmp")
                            : LLVMBuildICmp(ctx->builder, LLVMIntSGE, lhs_res.value, rhs_res.value, "ge_tmp");
        break;
    default:
        ir_gen_error(&ctx->errors, op_node, "Unsupported relational operator");
        return NullTypedValue;
    }

    TypedValue res = create_typed_value(value, bool_type, false);

    return res;
}

static TypedValue
process_equality_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    if (lhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "eq_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    if (rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    rhs_res = ensure_rvalue(ctx, "eq_rhs_rval", rhs_res);

    TypeDescriptor const * lhs_type = lhs_res.type_info;
    TypeDescriptor const * rhs_type = rhs_res.type_info;

    bool is_float_op = is_floating_kind(lhs_type);

    // Handle type promotion for integer operands - both sides must match
    if (is_integer_kind(lhs_type) && is_integer_kind(rhs_type))
    {
        rhs_res = cast_typed_value_to_desc(ctx, rhs_res, lhs_type);
    }

    c_grammar_node_t const * op_node = node->binary_expression.op;
    equality_operator_type_t operator= op_node->op.eq.op;
    debug_info("%s: now comparing results operator %d", __func__, operator);

    LLVMValueRef value = NULL;

    switch (operator)
    {
    case EQ_OP_EQ:
        value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs_res.value, rhs_res.value, "feq_tmp")
                            : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs_res.value, rhs_res.value, "eq_tmp");
        break;
    case EQ_OP_NE:
        value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs_res.value, rhs_res.value, "fne_tmp")
                            : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_res.value, rhs_res.value, "ne_tmp");
        break;
    default:
        ir_gen_error(&ctx->errors, op_node, "Unsupported equality operator");
        return NullTypedValue;
    }

    TypeDescriptor const * bool_type
        = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0});
    TypedValue res = create_typed_value(value, bool_type, false);

    dump_typed_value("equality_result", res);

    return res;
}

static TypedValue
process_logical_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * op_node = node->binary_expression.op;
    bool is_or = op_node->op.logical.op == LOGICAL_OP_OR;
    c_grammar_node_t const * lhs_node = node->binary_expression.left;
    c_grammar_node_t const * rhs_node = node->binary_expression.right;

    LLVMBasicBlockRef rhs_block = LLVMAppendBasicBlockInContext(
        ctx->context, LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder)), "logical_rhs"
    );
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(
        ctx->context, LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder)), "logical_merge"
    );

    TypedValue lhs_res = process_expression(ctx, lhs_node);
    if (lhs_res.value == NULL)
    {
        debug_error("LHS processing of logical expression failed");
        return NullTypedValue;
    }
    lhs_res = ensure_rvalue(ctx, "log_lhs_rval", lhs_res);

    TypeDescriptor const * bool_type
        = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0});
    // Convert to i1
    if (!is_integer_kind(lhs_res.type_info) || LLVMGetIntTypeWidth(lhs_res.type_info->llvm_type) != 1)
    {
        // This handles ints (is x != 0?), pointers (is ptr != null?), etc.
        LLVMValueRef zero = LLVMConstNull(lhs_res.type_info->llvm_type);
        lhs_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_res.value, zero, "to_bool");
        lhs_res.type_info = bool_type;
    }

    LLVMValueRef res_alloca = LLVMBuildAlloca(ctx->builder, bool_type->llvm_type, "logical_res");

    aligned_store(ctx, ctx->builder, lhs_res.value, bool_type->llvm_type, res_alloca);
    if (is_or)
    {
        LLVMBuildCondBr(ctx->builder, lhs_res.value, merge_block, rhs_block);
    }
    else
    {
        LLVMBuildCondBr(ctx->builder, lhs_res.value, rhs_block, merge_block);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, rhs_block);

    TypedValue rhs_res = process_expression(ctx, rhs_node);
    if (rhs_res.value == NULL)
    {
        debug_error("RHS processing of logical expression failed");
        return NullTypedValue;
    }
    rhs_res = ensure_rvalue(ctx, "log_rhs_rval", rhs_res);

    if (is_integer_kind(rhs_res.type_info) || LLVMGetIntTypeWidth(lhs_res.type_info->llvm_type) != 1)
    {
        // This handles ints (is x != 0?), pointers (is ptr != null?), etc.
        LLVMValueRef zero = LLVMConstNull(rhs_res.type_info->llvm_type);
        rhs_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs_res.value, zero, "to_bool");
        rhs_res.type_info = bool_type;
    }

    aligned_store(ctx, ctx->builder, rhs_res.value, bool_type->llvm_type, res_alloca);
    LLVMBuildBr(ctx->builder, merge_block);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_block);

    return create_typed_value(res_alloca, bool_type, true);
}

static TypedValue
process_conditional_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Conditional expression: condition ? true_expr : false_expr
    c_grammar_node_t const * condition_node = node->conditional_expression.condition_expression;
    c_grammar_node_t const * ternary_operation = node->conditional_expression.ternary_operation;
    c_grammar_node_t const * true_expr_node = ternary_operation->ternary_operation.true_expression;
    c_grammar_node_t const * false_expr_node = ternary_operation->ternary_operation.false_expression;

    if (condition_node == NULL || true_expr_node == NULL || false_expr_node == NULL)
    {
        debug_error("Invalid conditional expression");
        return NullTypedValue;
    }

    // Get current function and create blocks
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

    LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "cond_then");
    LLVMBasicBlockRef else_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "cond_else");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "cond_merge");

    // Evaluate condition
    TypedValue cond_res = process_expression(ctx, condition_node);
    if (cond_res.value == NULL)
    {
        return NullTypedValue;
    }
    cond_res = ensure_rvalue(ctx, "shift_cond_rval", cond_res);

    // Convert condition to i1 if needed
    cond_res = cast_typed_value_to_desc(
        ctx,
        cond_res,
        get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0})
    );

    // Branch to then or else
    LLVMBuildCondBr(ctx->builder, cond_res.value, then_block, else_block);

    // Generate then block
    LLVMPositionBuilderAtEnd(ctx->builder, then_block);
    TypedValue true_res = process_expression(ctx, true_expr_node);
    if (true_res.value == NULL)
    {
        return NullTypedValue;
    }
    true_res = ensure_rvalue(ctx, "conditional_then", true_res);

    // After processing true_expr (which might be a nested ternary), the builder
    // is positioned at the nested ternary's merge block. Save this block
    // before branching to our merge block.
    LLVMBasicBlockRef true_block = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_block);

    // Generate else block
    LLVMPositionBuilderAtEnd(ctx->builder, else_block);
    TypedValue false_res = process_expression(ctx, false_expr_node);
    if (false_res.value == NULL)
    {
        return false_res;
    }
    false_res = ensure_rvalue(ctx, "conditional_else", false_res);

    // After processing false_expr (which might be a nested ternary), the builder
    // is positioned at the nested ternary's merge block. Save this block
    // before branching to our merge block.
    LLVMBasicBlockRef false_block = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_block);

    // Merge and create phi node
    LLVMPositionBuilderAtEnd(ctx->builder, merge_block);

    TypedValue res = create_typed_value(
        LLVMBuildPhi(ctx->builder, true_res.type_info->llvm_type, "cond_result"), true_res.type_info, false
    );

    // Add phi operands using the actual blocks where the expressions ended
    LLVMAddIncoming(res.value, &true_res.value, &true_block, 1);
    LLVMAddIncoming(res.value, &false_res.value, &false_block, 1);

    return res;
}

static TypedValue
process_compound_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // CompoundLiteral: (type){initializer-list}
    // e.g., (struct Pos){.x = 1, .y = 2} or (union Data){.x = 1}
    // Structure: TypeName + InitializerList
    // First child is TypeName, second is InitializerList
    c_grammar_node_t const * type_name_node = node->compound_literal.type_name;
    c_grammar_node_t const * init_list_node = node->compound_literal.initializer_list;

    /* Extract type name - check struct/union keyword first, then typedef */
    char const * type_name = NULL;
    bool is_typedef = false;

    if (type_name_node->type == AST_NODE_TYPE_NAME)
    {
        c_grammar_node_t const * qualifier_list = type_name_node->type_name.specifier_qualifier_list;

        for (size_t i = 0; i < qualifier_list->list.count && !type_name; ++i)
        {
            c_grammar_node_t const * child = qualifier_list->list.children[i];

            if (child->type == AST_NODE_TYPEDEF_SPECIFIER)
            {
                /* Try typedef */
                type_name = extract_typedef_name(child);
                if (type_name != NULL)
                {
                    is_typedef = true;
                }
            }
            else
            {
                /* Try struct/union keyword */
                type_name = extract_struct_or_union_or_enum_tag(child);
            }
        }
    }

    if (type_name == NULL)
    {
        debug_error("Could not extract type name from compound literal");
        return NullTypedValue;
    }

    /* Look up the type - struct list or typedef list */
    TypeDescriptor const * compound_type_desc = is_typedef ? generator_find_typedef_type_descriptor(ctx, type_name)
                                                           : generator_find_type_descriptor_by_tag(ctx, type_name);
    if (compound_type_desc == NULL || compound_type_desc->llvm_type == NULL)
    {
        debug_error("Unknown type '%s' in compound literal", type_name);
        return NullTypedValue;
    }

    // Create a temporary local variable (alloca) for the compound literal
    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, compound_type_desc->llvm_type, "compound_literal_tmp");
    if (alloca_inst == NULL)
    {
        debug_error("Failed to allocate compound literal");
        return NullTypedValue;
    }

    // Initialize using the initializer list
    if (init_list_node->type == AST_NODE_INITIALIZER_LIST)
    {
        process_initializer_list(ctx, alloca_inst, compound_type_desc, init_list_node, NULL);
    }

    return create_typed_value(alloca_inst, compound_type_desc, true);
}

static TypeDescriptor const *
get_type_descriptor_from_specifier_list(ir_generator_ctx_t * ctx, c_grammar_node_t const * qualifier_list)
{
    TypeDescriptor const * type_desc = NULL;

    TypeSpecifierValidationResult validation_result = validate_type_specifiers(qualifier_list);
    if (validation_result.is_valid == false)
    {
        debug_error("%s: Invalid type specifier list", __func__);
        return NULL;
    }
    if (validation_result.is_native_type)
    {
        TypeSpecifier specs = build_type_specifiers(qualifier_list);
        if (!type_specifier_is_valid(specs))
        {
            debug_error("%s: invalid type specs");
            type_specifier_dump(specs, DEBUG_LEVEL_ERROR);
        }
        type_desc = get_or_create_builtin_type(ctx->type_descriptors, specs, (TypeQualifier){0});
    }
    else if (qualifier_list->list.count == 1)
    {
        // for (size_t i = 0; i < qualifier_list->list.count && target_type == NULL; i++)
        {
            c_grammar_node_t * child = qualifier_list->list.children[0];

            debug_info("qualifier list child type: %s", get_node_type_name_from_node(child));

            // Handle terminal type specifier (e.g., "int", "char")
            if (child->type == AST_NODE_TYPE_SPECIFIER)
            {
                type_desc = resolve_type_descriptor(ctx, qualifier_list, NULL);
            }
            // Handle Identifier (struct name like "Point" in sizeof(struct Point))
            else if (child->type == AST_NODE_IDENTIFIER)
            {
                type_desc = generator_find_type_descriptor_by_tag(ctx, child->text);
            }
            // Handle TypedefSpecifier (typedef name in sizeof(MyType))
            else if (child->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
            {
                c_grammar_node_t const * specifier = child->typedef_specifier_qualifier.typedef_specifier;
                char const * typedef_name = extract_typedef_name(specifier);
                if (typedef_name != NULL)
                {
                    type_desc = generator_find_typedef_type_descriptor(ctx, typedef_name);
                }
            }
            else if (
                child->type == AST_NODE_STRUCT_TYPE_REF || child->type == AST_NODE_UNION_TYPE_REF
                || child->type == AST_NODE_ENUM_TYPE_REF
            )
            {
                char const * tag = extract_struct_or_union_or_enum_tag(child);
                debug_info("%s: looking up struct/union/enum tag '%s'", __func__, tag);
                if (tag != NULL)
                {
                    type_desc = generator_find_type_descriptor_by_tag(ctx, tag);
                }
            }
        }
    }

    return type_desc;
}

static TypedValue
process_unary_expression_prefix(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Unary structure: [Operator, Operand]
    c_grammar_node_t const * op = node->unary_expression_prefix.op;
    c_grammar_node_t const * operand_node = node->unary_expression_prefix.operand;

    switch (op->op.unary.op)
    {
    case UNARY_OP_ADDR:
    {
        // For &compound_literal, we need to create a pointer to the temp
        // The compound literal code returns a loaded value, but we need the pointer
        if (operand_node->type == AST_NODE_COMPOUND_LITERAL)
        {
            TypedValue v = process_compound_literal(ctx, operand_node);

            if (v.value == NULL)
            {
                ir_gen_error(&ctx->errors, operand_node, "Cannot take the address of an rvalue");
                return NullTypedValue;
            }

            // --- The Bridge Logic ---
            TypeDescriptor const * base_desc = v.type_info;

            if (base_desc == NULL)
            {
                debug_error("No type descriptor found for compound literal, attempting fallback");
                return NullTypedValue;
            }

            v = create_typed_value(
                v.value, get_or_create_pointer_type(ctx->type_descriptors, base_desc, (TypeQualifier){0}), false
            );

            return v;
        }
        TypedValue v = process_expression(ctx, operand_node);
        if (v.value == NULL)
        {
            return NullTypedValue;
        }

        v.is_lvalue = false;
        // For &member or &array[i], process the expression which returns a pointer
        return v;
    }

    case UNARY_OP_DEREF:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_info("operand dereference failed");
            return NullTypedValue;
        }

        if (operand_res.type_info->kind != NCC_TYPE_KIND_POINTER)
        {
            ir_gen_error(
                &ctx->errors,
                operand_node,
                "Error: Dereference operand is not a pointer (value: %p)\n",
                (void *)operand_res.value
            );
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "un_op_deref", operand_res);
        if (!typed_value_switch_to_pointee(&operand_res))
        {
            ir_gen_error(&ctx->errors, operand_node, "Error: Failed to switch to pointee type for dereference");
            return NullTypedValue;
        }
        operand_res.is_lvalue = true;

        return operand_res;
    }

    case UNARY_OP_MINUS:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_error("Operand processing failed for unary minus");
            return NullTypedValue;
        }
        if (is_floating_kind(operand_res.type_info))
        {
            operand_res.value = LLVMBuildFNeg(ctx->builder, operand_res.value, "fneg_tmp");
        }
        else
        {
            operand_res.value = LLVMBuildNeg(ctx->builder, operand_res.value, "neg_tmp");
        }
        operand_res.is_lvalue = false;

        return operand_res;
    }

    case UNARY_OP_NOT:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_error("Operand processing failed for unary not");
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "un_not_rval", operand_res);

        // 1. Comparison produces an i1 (1-bit integer)
        LLVMValueRef zero = LLVMConstNull(operand_res.type_info->llvm_type);
        LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand_res.value, zero, "is_zero_tmp");
        TypeDescriptor const * bool_desc = type_descriptor_get_bool_type(ctx->type_descriptors, false);

        return create_typed_value(is_zero, bool_desc, false);
    }

    case UNARY_OP_BITNOT:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        if (operand_res.value == NULL)
        {
            debug_error("Operand processing failed for unary bitnot");
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "bit_not_rval", operand_res);

        operand_res.value = LLVMBuildNot(ctx->builder, operand_res.value, "bitnot_tmp");
        operand_res.is_lvalue = false;
        return operand_res;
    }

    case UNARY_OP_INC:
    case UNARY_OP_DEC:
    {
        TypedValue var_res = process_expression(ctx, operand_node);

        if (var_res.value == NULL)
        {
            return NullTypedValue;
        }

        if (!var_res.is_lvalue)
        {
            ir_gen_error(&ctx->errors, operand_node, "Cannot increment/decrement non-lvalue");
            return NullTypedValue;
        }

        if (var_res.type_info->qualifiers.is_const)
        {
            ir_gen_error(&ctx->errors, operand_node, "Cannot increment/decrement const variable");
            return NullTypedValue;
        }

        TypedValue rvalue_res = ensure_rvalue(ctx, "unary_inc_dev_rval", var_res);
        LLVMValueRef original_val = rvalue_res.value;
        LLVMValueRef one = LLVMConstInt(ctx->ref_type.i32_type, 1, false);

        LLVMValueRef new_val;
        if (op->op.unary.op == UNARY_OP_INC)
        {
            if (is_floating_kind(var_res.type_info))
                new_val = LLVMBuildFAdd(
                    ctx->builder, original_val, LLVMConstReal(var_res.type_info->llvm_type, 1.0), "inc_tmp"
                );
            else
                new_val = LLVMBuildAdd(ctx->builder, original_val, one, "inc_tmp");
        }
        else
        {
            if (is_floating_kind(var_res.type_info))
                new_val = LLVMBuildFSub(
                    ctx->builder, original_val, LLVMConstReal(var_res.type_info->llvm_type, 1.0), "dec_tmp"
                );
            else
                new_val = LLVMBuildSub(ctx->builder, original_val, one, "dec_tmp");
        }
        aligned_store(ctx, ctx->builder, new_val, var_res.type_info->llvm_type, var_res.value);
        var_res.value = new_val;
        var_res.is_lvalue = false;

        return var_res;
    }

    case UNARY_OP_PLUS:
    {
        TypedValue var_res = process_expression(ctx, operand_node);
        if (var_res.value == NULL)
        {
            debug_error("Operand processing failed for unary unary");
            return NullTypedValue;
        }
        var_res.is_lvalue = false;

        return var_res;
    }

    case UNARY_OP_SIZEOF:
    {
        TypeDescriptor const * target_type = NULL;

        // Check if operand is a TypeName (e.g., sizeof(int) or sizeof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            // TypeName contains TypeSpecifier(s), possibly with struct/union keyword
            c_grammar_node_t const * qualifier_list = operand_node->type_name.specifier_qualifier_list;
            target_type = get_type_descriptor_from_specifier_list(ctx, qualifier_list);
        }
        else
        {
            debug_info("Operand node type for sizeof: %s", get_node_type_name_from_node(operand_node));
            TypedValue operand_res = process_expression(ctx, operand_node);
            if (operand_res.value == NULL)
            {
                debug_error("Operand processing failed for unary sizeof");
                return NullTypedValue;
            }

            target_type = operand_res.type_info;
        }
        debug_info("unary operator getting size of type: %p", target_type);
        uint64_t sizeof_type_bytes = get_type_size(ctx, target_type);
        TypeDescriptor const * sizeof_desc = type_descriptor_get_uint64_type(ctx->type_descriptors, true);
        LLVMValueRef val = LLVMConstInt(sizeof_desc->llvm_type, sizeof_type_bytes, false);

        return create_typed_value(val, sizeof_desc, false);
    }
    case UNARY_OP_ALIGNOF:
    {
        // alignof is similar to sizeof but returns alignment
        TypeDescriptor const * target_type = NULL;

        // Handle TypeName (e.g., alignof(int) or alignof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            c_grammar_node_t const * qualifier_list = operand_node->type_name.specifier_qualifier_list;
            target_type = get_type_descriptor_from_specifier_list(ctx, qualifier_list);
        }
        else
        {
            debug_info("Operand node type for alignof: %s", get_node_type_name_from_node(operand_node));
            TypedValue operand_res = process_expression(ctx, operand_node);
            if (operand_res.value == NULL)
            {
                debug_error("Operand processing failed for unary alignof");
                return NullTypedValue;
            }

            target_type = operand_res.type_info;
        }
        debug_info("unary operator getting alignment");
        uint64_t alignment = get_type_alignment_desc(target_type);
        TypeDescriptor const * alignment_desc = type_descriptor_get_uint64_type(ctx->type_descriptors, true);
        LLVMValueRef val = LLVMConstInt(alignment_desc->llvm_type, alignment, false);

        return create_typed_value(val, alignment_desc, false);
    }
    default:
    {
        debug_error("Unknown unary operator %u.", op->op.unary.op);
        return NullTypedValue;
    }
    }

    return NullTypedValue; /* Shouldn't happen. */
}

static TypedValue
process_comma_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    /* Comma expression: evaluate all expressions, return the last value. */
    TypedValue result = NullTypedValue;
    for (size_t i = 0; i < node->list.count; ++i)
    {
        result = process_expression(ctx, node->list.children[i]);
        if (result.value == NULL)
        {
            return NullTypedValue;
        }
    }
    return result;
}

/**
 * @brief Processes an expression AST node and returns its LLVM ValueRef.
 * This function recursively handles various expression types.
 */
static TypedValue
_process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NullTypedValue;
    }

    fprintf(stderr, "%s node type: %s (%u)\n", __func__, get_node_type_name_from_node(node), node->type);

    switch (node->type)
    {
    case AST_NODE_INTEGER_LITERAL:
    {
        return process_integer_literal(ctx, node);
    }
    case AST_NODE_FLOAT_LITERAL:
    {
        return process_float_literal(ctx, node);
    }
    case AST_NODE_STRING_LITERAL:
    {
        return process_string_literal(ctx, node);
    }
    case AST_NODE_CHARACTER_LITERAL:
    {
        return process_character_literal(ctx, node);
    }
    case AST_NODE_POSTFIX_EXPRESSION:
    {
        return process_postfix_expression(ctx, node);
    }
    case AST_NODE_CAST_EXPRESSION:
    {
        return process_cast_expression(ctx, node);
    }
    case AST_NODE_ASSIGNMENT:
    {
        return process_assignment(ctx, node);
    }
    case AST_NODE_IDENTIFIER:
    {
        return process_identifier(ctx, node);
    }
    case AST_NODE_BITWISE_EXPRESSION:
    {
        return process_bitwise_expression(ctx, node);
    }
    case AST_NODE_SHIFT_EXPRESSION:
    {
        return process_shift_expression(ctx, node);
    }
    case AST_NODE_ARITHMETIC_EXPRESSION:
    {
        return process_arithmetic_expression(ctx, node);
    }
    case AST_NODE_RELATIONAL_EXPRESSION:
    {
        return process_relational_expression(ctx, node);
    }
    case AST_NODE_EQUALITY_EXPRESSION:
    {
        return process_equality_expression(ctx, node);
    }
    case AST_NODE_LOGICAL_EXPRESSION:
    {
        return process_logical_expression(ctx, node);
    }
    case AST_NODE_CONDITIONAL_EXPRESSION:
    {
        return process_conditional_expression(ctx, node);
    }
    case AST_NODE_COMMA_EXPRESSION:
    {
        return process_comma_expression(ctx, node);
    }
    case AST_NODE_UNARY_EXPRESSION_PREFIX:
    {
        return process_unary_expression_prefix(ctx, node);
    }
    case AST_NODE_OPTIONAL_ARGUMENT_LIST:
    case AST_NODE_POSTFIX_PARTS:
    {
        debug_error("got %s in %s", get_node_type_name_from_type(node->type), __func__);
        /* Shouldn't happen. */
        break;
    }
    case AST_NODE_COMPOUND_LITERAL:
    {
        return process_compound_literal(ctx, node);
    }

    case AST_NODE_INITIALIZER:
    {
        /* Either get an InitializerList or an AssignmentExpression, which is one of two other node types. */
        if (node->list.count == 0)
        {
            debug_error("initializer node has no children");
            return NullTypedValue;
        }
        return process_expression(ctx, node->list.children[0]);
    }

    case AST_NODE_EXPRESSION_STATEMENT:
    {
        c_grammar_node_t const * expr_node = node->expression_statement.expression;

        if (expr_node == NULL)
        {
            debug_error("expression statement has no expression");
            return NullTypedValue;
        }
        return process_expression(ctx, expr_node);
    }

    case AST_NODE_INITIALIZER_LIST:
    case AST_NODE_TRANSLATION_UNIT:
    case AST_NODE_FUNCTION_DEFINITION:
    case AST_NODE_COMPOUND_STATEMENT:
    case AST_NODE_DECLARATION:
    case AST_NODE_INTEGER_BASE:
    case AST_NODE_FLOAT_BASE:
    case AST_NODE_LITERAL_SUFFIX:
    case AST_NODE_NAMED_DECL_SPECIFIERS:
    case AST_NODE_TYPE_SPECIFIER:
    case AST_NODE_TYPEDEF_SPECIFIER:
    case AST_NODE_UNARY_OPERATOR:
    case AST_NODE_ARITHMETIC_OPERATOR:
    case AST_NODE_SHIFT_OPERATOR:
    case AST_NODE_RELATIONAL_OPERATOR:
    case AST_NODE_EQUALITY_OPERATOR:
    case AST_NODE_DECLARATOR:
    case AST_NODE_DIRECT_DECLARATOR:
    case AST_NODE_DECLARATOR_SUFFIX:
    case AST_NODE_POINTER:
    case AST_NODE_POSTFIX_OPERATOR:
    case AST_NODE_ARRAY_SUBSCRIPT:
    case AST_NODE_MEMBER_ACCESS_DOT:
    case AST_NODE_MEMBER_ACCESS_ARROW:
    case AST_NODE_INIT_DECLARATOR:
    case AST_NODE_IF_STATEMENT:
    case AST_NODE_SWITCH_STATEMENT:
    case AST_NODE_WHILE_STATEMENT:
    case AST_NODE_DO_WHILE_STATEMENT:
    case AST_NODE_FOR_STATEMENT:
    case AST_NODE_GOTO_STATEMENT:
    case AST_NODE_BREAK_STATEMENT:
    case AST_NODE_CONTINUE_STATEMENT:
    case AST_NODE_RETURN_STATEMENT:
    case AST_NODE_TYPE_NAME:
    case AST_NODE_STRUCT_DEFINITION:
    case AST_NODE_UNION_DEFINITION:
    case AST_NODE_ENUM_DEFINITION:
    case AST_NODE_STRUCT_TYPE_REF:
    case AST_NODE_UNION_TYPE_REF:
    case AST_NODE_ENUM_TYPE_REF:
    case AST_NODE_TYPEDEF_DECLARATION:
    case AST_NODE_LABELED_STATEMENT:
    case AST_NODE_CASE_LABEL:
    case AST_NODE_SWITCH_CASE:
    case AST_NODE_DEFAULT_STATEMENT:
    case AST_NODE_ASSIGNMENT_OPERATOR:
    case AST_NODE_OPTIONAL_KW_EXTENSION:
    case AST_NODE_INIT_DECLARATOR_LIST:
    case AST_NODE_TERNARY_OPERATION:
    case AST_NODE_ENUMERATOR:
    case AST_NODE_FUNCTION_POINTER_DECLARATOR:
    case AST_NODE_DESIGNATION:
    case AST_NODE_STRUCT_DECLARATOR:
    case AST_NODE_STRUCT_DECLARATOR_BITFIELD:
    case AST_NODE_STRUCT_DECLARATION:
    case AST_NODE_STRUCT_DECLARATION_LIST:
    case AST_NODE_EXTERNAL_DECLARATIONS:
    case AST_NODE_EXTERNAL_DECLARATION:
    case AST_NODE_TOP_LEVEL_DECLARATION:
    case AST_NODE_PREPROCESSOR_DIRECTIVE:
    case AST_NODE_ASM_STATEMENT:
    case AST_NODE_STRUCT_DECLARATOR_LIST:
    case AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST:
    case AST_NODE_CASE_LABELS:
    case AST_NODE_SWITCH_BODY_STATEMENTS:
    case AST_NODE_TYPEDEF_INIT_DECLARATION_LIST:
    case AST_NODE_ATTRIBUTE_LIST:
    case AST_NODE_ASM_NAMES:
    case AST_NODE_TYPEDEF_DECLARATOR:
    case AST_NODE_TYPEDEF_DIRECT_DECLARATOR:
    case AST_NODE_TYPEDEF_INIT_DECLARATOR:
    case AST_NODE_DECLARATOR_SUFFIX_LIST:
    case AST_NODE_POINTER_LIST:
    case AST_NODE_INITIALIZER_LIST_ENTRY:
    case AST_NODE_ENUMERATOR_LIST:
    case AST_NODE_ATTRIBUTE:
    case AST_NODE_BITWISE_OPERATOR:
    case AST_NODE_LOGICAL_OPERATOR:
    case AST_NODE_ABSTRACT_DECLARATOR:
    case AST_NODE_STORAGE_CLASS_SPECIFIER:
    case AST_NODE_STORAGE_CLASS_SPECIFIERS:
    case AST_NODE_FUNCTION_SPECIFIER:
    case AST_NODE_TYPE_QUALIFIER:
    case AST_NODE_TYPE_QUALIFERS:
    case AST_NODE_DECLARATION_SPECIFIERS:
    case AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER:
    case AST_NODE_TYPE_SPECIFIERS:
    case AST_NODE_PARAMETER_LIST:
    case AST_NODE_ELLIPSIS:
    case AST_NODE_PREPROCESSOR_LINE_MARKER:
    default:
        debug_error("%s: Node type: %s is not supported at this level", __func__, get_node_type_name_from_node(node));
        break;
    }
    return NullTypedValue; // Return NULL if expression processing failed or not implemented.
}

static TypedValue
_process_expression_impl(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, int line)
{
    if (ctx->errors.fatal)
    {
        return NullTypedValue;
    }

    debug_info("%s from line: %u", __func__, line);
    print_ast_with_label(node, __func__);

    TypedValue result = _process_expression(ctx, node);

    if (result.value != NULL && result.type_info == NULL)
    {
        debug_error(
            "expression result has a value but no type descriptor after evaluating node: %s",
            get_node_type_name_from_node(node)
        );
        print_ast(node);
        result = NullTypedValue;
    }
    else if (ctx->errors.fatal)
    {
        debug_error("fatal error encountered");
        result = NullTypedValue;
    }
    debug_info("%s returning to line: %u", __func__, line);
    dump_typed_value("process_expression result", result);
    print_ast_with_label(node, "done with");

    return result;
}
