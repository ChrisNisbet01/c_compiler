#include "llvm_ir_generator.h"

#include "ast_node_name.h"
#include "ast_print.h"
#include "binary_expression_processor.h"
#include "c_grammar_ast.h"
#include "compound_literal_processor.h"
#include "debug.h"
#include "declaration_handler.h"
#include "enum_evaluation.h"
#include "generator_lists.h"
#include "ir_utils.h"
#include "type_utils.h"
#include "unary_operations.h"

// Helper function to get natural alignment for a type
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);

// Helper to parse stack alignment from data layout string (e.g., "e-m:e-...-S128")
// Returns 0 if not found, otherwise returns alignment in bytes
static uint32_t
parse_stack_alignment_from_layout(char const * layout_str)
{
    if (layout_str == NULL)
    {
        return 0;
    }
    // Find 'S' at the end of the data layout string (before the final '-')
    char * s_ptr = strrchr(layout_str, 'S');
    if (s_ptr != NULL && s_ptr > layout_str && *(s_ptr - 1) == '-')
    {
        return (uint32_t)atoi(s_ptr + 1) / 8;
    }
    return 0;
}

TypedValue NullTypedValue;

// --- Function declaration tracking ---

static struct function_decl_entry *
find_function_declaration(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL || name == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < ctx->function_declarations.count; ++i)
    {
        struct function_decl_entry * entry = &ctx->function_declarations.entries[i];
        if (entry->name != NULL && strcmp(entry->name, name) == 0)
        {
            LLVMValueRef func = LLVMGetNamedFunction(ctx->module, name);
            if (entry->func.value == NULL)
            {
                entry->func = create_typed_value(func, entry->func.type_info, false);
            }

            return entry;
        }
    }

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

    // Check if function already exists
    struct function_decl_entry * existing = find_function_declaration(ctx, name);

    if (existing != NULL)
    {
        // Function already declared - check for signature mismatch
        if (!function_signatures_match(
                existing->func.type_info->pointee->llvm_type, func.type_info->pointee->llvm_type
            ))
        {
            return true; // Conflict detected
        }

        // Check for redefinition
        if (existing->has_definition && has_definition)
        {
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

    return false;
}

TypedValue
ensure_lvalue(ir_generator_ctx_t * ctx, char const * name, TypedValue val)
{
    if (val.is_lvalue)
    {
        return val;
    }

    // 1. Create space on the stack
    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, val.type_info->llvm_type, name);
    LLVMSetAlignment(alloca_inst, 8);

    // 2. Store the data (val.value) into the address (alloca_inst)
    LLVMBuildStore(ctx->builder, val.value, alloca_inst);

    // 3. Return a new TypedValue where 'value' is now the POINTER
    TypedValue lval = val;
    lval.is_lvalue = true;
    lval.value = alloca_inst;
    return lval;
}

TypedValue
ensure_rvalue(ir_generator_ctx_t * ctx, char const * label, TypedValue val)
{
    if (val.value == NULL)
    {
        return val;
    }
    if (!val.is_lvalue)
    {
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

void
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
    if (desc->kind == NCC_TYPE_KIND_STRUCT || desc->kind == NCC_TYPE_KIND_UNION)
    {
        LLVMValueRef size = LLVMSizeOf(desc->llvm_type);
        LLVMValueRef zero = LLVMConstNull(ctx->ref_type.i8_type);
        uint32_t align = get_type_alignment_desc(ctx->data_layout, desc);
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
                           LLVMConstInt(ctx->ref_type.i32_type, field->storage_index, false)};

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
                unsigned storage_idx = desc->struct_metadata.members.members[local_index].storage_index;

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
    if (kind == NCC_TYPE_KIND_STRUCT || kind == NCC_TYPE_KIND_UNION)
    {
        LLVMValueRef size = LLVMSizeOf(element_type);
        LLVMValueRef zero = LLVMConstNull(ctx->ref_type.i8_type);
        // Use TypeDescriptor's knowledge of alignment if available, or fallback
        unsigned alignment = get_type_alignment_desc(ctx->data_layout, type_desc);
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
                               LLVMConstInt(ctx->ref_type.i32_type, field->storage_index, false)};
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
            {
                (*outer_index)++;
            }

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
register_incomplete_struct_or_union_definition(
    ir_generator_ctx_t * ctx, char const * tag, TypeQualifier quals, type_kind_t kind
)
{
    if (ctx == NULL || tag == NULL)
    {
        return NULL;
    }

    bool is_union = kind == TYPE_KIND_UNION || kind == TYPE_KIND_UNTAGGED_UNION;

    if (kind == TYPE_KIND_STRUCT || kind == TYPE_KIND_UNION)
    {
        /* Return existing tagged type if already registered */
        type_info_t * existing = generator_lookup_tagged_entry_by_tag_and_kind(ctx, tag, kind);
        if (existing != NULL)
        {
            return existing;
        }
    }

    type_info_t opaque = {0};
    opaque.tag = strdup(tag);
    opaque.kind = kind;
    struct_or_union_members_st * members = NULL;
    bool is_complete = false;
    LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, tag);
    opaque.type_desc = register_struct_type(ctx->type_descriptors, struct_type, quals, is_union, is_complete, members);
    type_info_t const * registered = generator_add_type_info(ctx, opaque);

    if (registered == NULL)
    {
        free((void *)opaque.tag);
        return NULL;
    }

    return registered;
}

static void
struct_or_union_members_cleanup(struct_or_union_members_st * members)
{
    for (size_t i = 0; i < members->num_members; i++)
    {
        struct_field_t * entry = &members->members[i];

        free(entry->name);
    }
    free(members->members);
}

static struct_or_union_members_st
extract_struct_or_union_members_type_descriptor(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    struct_or_union_members_st object_members = {0};

    if (type_child == NULL
        || (type_child->type != AST_NODE_STRUCT_DEFINITION && type_child->type != AST_NODE_UNION_DEFINITION))
    {
        debug_error("Need struct or union definition, but got: %s", get_node_type_name_from_node(type_child));
        return object_members;
    }

    c_grammar_node_t const * members_node = type_child->struct_definition.declaration_list;

    if (members_node == NULL || members_node->list.count == 0)
    {
        debug_error("no members declaration");
        return object_members;
    }

    size_t max_num_members = members_node->list.count;
    struct_field_t * members = calloc(max_num_members, sizeof(*members));
    if (members == NULL)
    {
        return object_members;
    }

    unsigned num_members = 0;

    for (size_t i = 0; i < members_node->list.count; i++)
    {
        c_grammar_node_t * struct_decl = members_node->list.children[i];
        if (struct_decl == NULL || struct_decl->type != AST_NODE_STRUCT_DECLARATION)
        {
            continue;
        }

        c_grammar_node_t const * specifier_qualifier_list = struct_decl->struct_declaration.specifier_qualifier_list;
        c_grammar_node_t const * declarator_list = struct_decl->struct_declaration.declarator_list;

        if (specifier_qualifier_list == NULL || specifier_qualifier_list->list.count == 0)
        {
            continue;
        }

        c_grammar_node_t const * type_spec = NULL;
        type_spec = specifier_qualifier_list;

        if (declarator_list == NULL || declarator_list->list.count == 0)
        {
            // Resolve the type specifier to see if it's a struct/union
            TypeDescriptor const * nested_type = resolve_type_descriptor(ctx, specifier_qualifier_list, NULL);

            if (nested_type && (nested_type->kind == NCC_TYPE_KIND_STRUCT || nested_type->kind == NCC_TYPE_KIND_UNION))
            {
                if (num_members + nested_type->struct_metadata.members.num_members >= max_num_members)
                {
                    max_num_members += nested_type->struct_metadata.members.num_members;
                    members = realloc(members, max_num_members * sizeof(*members));
                    if (members == NULL)
                    {
                        debug_error("malloc failure");
                        return object_members;
                    }
                }

                // Pull members from the nested type into the current list
                for (size_t m = 0; m < nested_type->struct_metadata.members.num_members; m++)
                {
                    // Copy the member descriptor
                    struct_field_t const * nested_mem = &nested_type->struct_metadata.members.members[m];

                    // Note: You may need to adjust storage_index or offsets here
                    // depending on how your LLVM struct builder handles flattening.
                    struct_field_t new_member = *nested_mem;
                    new_member.name = strdup(nested_mem->name);
                    unsigned type_bits;
                    struct_field_t * previous_member = NULL;
                    if (num_members > 0)
                    {
                        previous_member = &members[num_members - 1];
                        type_bits = LLVMGetIntTypeWidth(previous_member->type_desc->llvm_type);
                    }
                    else
                    {
                        type_bits = LLVMGetIntTypeWidth(nested_mem->type_desc->llvm_type);
                    }

                    if (m == 0 || previous_member == NULL
                        || (strlen(nested_mem->name) > 0 && nested_mem->bitfield.bit_width == 0)
                        || (strlen(previous_member->name) == 0 && previous_member->bitfield.bit_width == 0)
                        || nested_mem->bitfield.bit_width + previous_member->bitfield.bit_offset
                                   + previous_member->bitfield.bit_width
                               > type_bits)
                    {
                        if (previous_member == NULL)
                        {
                            new_member.storage_index = 0;
                        }
                        else if (m > 0 && nested_type->kind == NCC_TYPE_KIND_UNION)
                        {
                            new_member.storage_index = previous_member->storage_index;
                        }
                        else
                        {
                            new_member.storage_index
                                = (previous_member == NULL) ? 0 : (previous_member->storage_index + 1);
                        }
                    }
                    else
                    {
                        new_member.storage_index = previous_member->storage_index;
                        new_member.bitfield.bit_offset
                            = previous_member->bitfield.bit_offset + previous_member->bitfield.bit_width;
                    }
                    members[num_members] = new_member;
                    num_members++;
                }

                continue; // Move to the next member in the parent struct
            }

            continue;
        }

        c_grammar_node_t const * struct_decl_node = declarator_list->list.children[0];

        struct_field_t new_member = {0};

        if (struct_decl_node->type == AST_NODE_STRUCT_DECLARATOR && struct_decl_node->list.count > 0)
        {
            c_grammar_node_t * decl = struct_decl_node->list.children[0];

            if (decl->type == AST_NODE_STRUCT_DECLARATOR_BITFIELD)
            {
                if (decl->list.count < 1 || decl->list.count > 2)
                {
                    continue;
                }
                size_t width_idx;
                if (decl->list.count == 1)
                {
                    width_idx = 0;
                    new_member.name = strdup("");
                }
                else
                {
                    width_idx = 1;
                    c_grammar_node_t const * bf_decl = decl->list.children[0];
                    if (bf_decl->type == AST_NODE_DECLARATOR)
                    {
                        c_grammar_node_t const * direct_decl = bf_decl->declarator.direct_declarator;
                        if (direct_decl && direct_decl->list.count > 0)
                        {
                            c_grammar_node_t * ident = direct_decl->list.children[0];
                            if (ident && ident->type == AST_NODE_IDENTIFIER && ident->text != NULL)
                            {
                                new_member.name = strdup(ident->text);
                            }
                        }
                    }
                }

                c_grammar_node_t * width_node = decl->list.children[width_idx];

                if (width_node->type == AST_NODE_INTEGER_LITERAL)
                {
                    new_member.bitfield.bit_width = (unsigned)width_node->integer_lit.integer_literal.value;
                }

                new_member.type_desc = resolve_type_descriptor(ctx, type_spec, NULL);
                if (new_member.type_desc == NULL)
                {
                    free(new_member.name);
                    continue;
                }

                unsigned type_bits;
                struct_field_t * previous_member = NULL;

                if (num_members > 0)
                {
                    previous_member = &members[num_members - 1];
                    type_bits = LLVMGetIntTypeWidth(previous_member->type_desc->llvm_type);
                }
                else
                {
                    type_bits = LLVMGetIntTypeWidth(new_member.type_desc->llvm_type);
                }

                if (previous_member == NULL || (strlen(new_member.name) > 0 && new_member.bitfield.bit_width == 0)
                    || (strlen(previous_member->name) == 0 && previous_member->bitfield.bit_width == 0)
                    || LLVMGetTypeKind(new_member.type_desc->llvm_type)
                           != LLVMGetTypeKind(previous_member->type_desc->llvm_type)
                    || new_member.bitfield.bit_width + previous_member->bitfield.bit_offset
                               + previous_member->bitfield.bit_width
                           > type_bits)
                {
                    new_member.storage_index = (previous_member == NULL) ? 0 : (previous_member->storage_index + 1);
                }
                else
                {
                    new_member.storage_index = previous_member->storage_index;
                    new_member.bitfield.bit_offset
                        = previous_member->bitfield.bit_offset + previous_member->bitfield.bit_width;
                }
                members[num_members] = new_member;
                num_members++;
            }
            else if (decl->type == AST_NODE_DECLARATOR)
            {
                new_member.type_desc = resolve_type_descriptor(ctx, type_spec, decl);

                if (new_member.type_desc == NULL)
                {
                    continue;
                }

                c_grammar_node_t const * direct_decl = decl->declarator.direct_declarator;
                if (direct_decl->list.count > 0)
                {
                    c_grammar_node_t * ident = direct_decl->list.children[0];
                    if (ident && ident->type == AST_NODE_IDENTIFIER && ident->text != NULL)
                    {
                        new_member.name = strdup(ident->text);
                    }
                }
                if (new_member.name == NULL)
                {
                    continue;
                }

                struct_field_t * previous_member = NULL;

                if (num_members > 0)
                {
                    previous_member = &members[num_members - 1];
                }
                if (previous_member == NULL || type_child->type == AST_NODE_UNION_DEFINITION)
                {
                    new_member.storage_index = 0;
                }
                else
                {
                    new_member.storage_index = previous_member->storage_index + 1;
                }
                members[num_members] = new_member;
                num_members++;
            }
        }
    }

    object_members.members = members;
    object_members.num_members = num_members;

    return object_members;
}

static type_info_t const *
add_members_to_incomplete_struct_union(
    ir_generator_ctx_t * ctx, type_info_t const * existing, c_grammar_node_t const * type_child
)
{
    struct_or_union_members_st members = extract_struct_or_union_members_type_descriptor(ctx, type_child);

    if (members.num_members == 0)
    {
        ir_gen_error(&ctx->errors, type_child, "Empty struct/union definition");
        return NULL;
    }

    struct_field_t * last_field = &members.members[members.num_members - 1];
    unsigned num_storage_units = last_field->storage_index + 1;
    TypeDescriptor const ** field_types = calloc(num_storage_units, sizeof(*field_types));
    if (field_types == NULL)
    {
        struct_or_union_members_cleanup(&members);
        debug_error("%s: Memory allocation failed", __func__);

        return NULL;
    }
    // First pass: track largest type and highest alignment per storage unit
    uint64_t * field_aligns = calloc(num_storage_units, sizeof(*field_aligns));
    if (field_aligns == NULL)
    {
        free(field_types);
        struct_or_union_members_cleanup(&members);
        debug_error("%s: Memory allocation failed", __func__);

        return NULL;
    }
    TypeDescriptor const ** max_align_types = calloc(num_storage_units, sizeof(*max_align_types));
    if (max_align_types == NULL)
    {
        free(field_types);
        free(field_aligns);
        struct_or_union_members_cleanup(&members);
        debug_error("%s: Memory allocation failed", __func__);

        return NULL;
    }
    for (size_t i = 0; i < members.num_members; i++)
    {
        struct_field_t * field = &members.members[i];

        unsigned idx = field->storage_index;

        uint64_t field_align = get_type_alignment_desc(ctx->data_layout, field->type_desc);
        if (field_types[idx] == NULL
            || get_type_size_desc(ctx->data_layout, field->type_desc)
                   > get_type_size_desc(ctx->data_layout, field_types[idx]))
        {
            field_types[idx] = field->type_desc;
        }
        if (max_align_types[idx] == NULL || field_align > field_aligns[idx])
        {
            max_align_types[idx] = field->type_desc;
            field_aligns[idx] = field_align;
        }
    }

    // Second pass: ensure correct alignment by wrapping if needed
    LLVMTypeRef * llvm_field_types = calloc(num_storage_units, sizeof(*llvm_field_types));
    if (llvm_field_types == NULL)
    {
        free(field_types);
        free(field_aligns);
        free(max_align_types);
        return NULL;
    }
    for (unsigned i = 0; i < num_storage_units; i++)
    {
        llvm_field_types[i] = field_types[i]->llvm_type;

        // Calculate how much the actual type falls short of the required alignment block
        uint64_t actual_size = get_type_size_desc(ctx->data_layout, field_types[i]);
        uint64_t padded_size = (actual_size + (field_aligns[i] - 1)) & ~(field_aligns[i] - 1);

        if (actual_size < padded_size)
        {
            uint64_t padding_bytes_needed = padded_size - actual_size;
            TypeDescriptor const * i8_type_desc = type_descriptor_get_int8_type(ctx->type_descriptors, false);
            LLVMTypeRef wrapper_types[2];
            wrapper_types[0] = field_types[i]->llvm_type;
            wrapper_types[1] = LLVMArrayType(i8_type_desc->llvm_type, padding_bytes_needed);
            LLVMTypeRef wrapper = LLVMStructCreateNamed(ctx->context, "");
            LLVMStructSetBody(wrapper, wrapper_types, 2, false);
            llvm_field_types[i] = wrapper;
        }
    }
    free(field_aligns);
    free(max_align_types);
    LLVMStructSetBody(existing->type_desc->llvm_type, llvm_field_types, num_storage_units, false);

    free(llvm_field_types);

    type_descriptor_complete_struct(ctx->type_descriptors, existing->type_desc, &members);

    struct_or_union_members_cleanup(&members);

    return existing;
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

    type_info_t const * existing = generator_lookup_tagged_entry_by_tag_and_kind(ctx, tag, kind);
    if (existing == NULL)
    {
        /* Register an opaque type now, in case the struct has members that refer to itself. */
        existing = register_incomplete_struct_or_union_definition(ctx, tag, quals, kind);
        if (existing == NULL)
        {
            debug_error("%s: failed to register opaque type", __func__);
            return NULL;
        }
    }

    if (!existing->type_desc->struct_metadata.is_complete)
    {
        existing = add_members_to_incomplete_struct_union(ctx, existing, type_child);

        return existing;
    }

    ir_gen_error(&ctx->errors, type_child, "Redefinition of %s %s", kind == TYPE_KIND_STRUCT ? "struct" : "union", tag);
    return NULL;
}

static type_info_t const *
register_untagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, type_kind_t kind
)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    char const * tag = generate_anon_name(ctx, (kind == TYPE_KIND_UNTAGGED_STRUCT) ? "struct" : "union");
    type_info_t const * existing = register_incomplete_struct_or_union_definition(ctx, tag, (TypeQualifier){0}, kind);

    if (existing == NULL)
    {
        debug_error("%s: failed to register opaque untagged type", __func__);
        return NULL;
    }
    existing = add_members_to_incomplete_struct_union(ctx, existing, type_child);

    return existing;
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

        return register_untagged_struct_or_union_definition(ctx, type_child, kind);
    }

    type_kind_t kind = (type_child->type == AST_NODE_STRUCT_DEFINITION) ? TYPE_KIND_STRUCT : TYPE_KIND_UNION;

    return register_tagged_struct_or_union_definition(ctx, type_child, struct_tag, kind, (TypeQualifier){0});
}

static type_info_t const *
register_untagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node)
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
    enum_info.type_desc = type_descriptor_get_enum_type(ctx->type_descriptors);

    return generator_add_type_info(ctx, enum_info);
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
    enum_info.type_desc = type_descriptor_get_enum_type(ctx->type_descriptors);
    enum_info.tag = strdup(tag);

    return generator_add_type_info(ctx, enum_info);
}

type_info_t const *
register_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node)
{
    char const * tag = search_ast_for_type_tag(enum_node);
    if (tag != NULL)
    {
        return register_tagged_enum_definition(ctx, enum_node, tag);
    }
    return register_untagged_enum_definition(ctx, enum_node);
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
    source_location_tracker_t * loc_tracker,
    char const * target_triple
)
{
    ir_generator_ctx_t * ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        debug_error("Failed to allocate memory for context.");
        return NULL;
    }

    // Initialize error collection (any error will be fatal since max_errors=1)
    ir_gen_error_collection_init(&ctx->errors, 1, parse_ctx, module_name, loc_tracker);

    ctx->generation_flags = flags;

    // Initialize LLVM

    ctx->context = LLVMContextCreate();
    if (!ctx->context)
    {
        debug_error("Failed to create LLVM context.");
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

    ctx->type_descriptors = type_descriptors_create_registry(ctx->context, ctx->data_layout, ctx->builder);
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

    // Set target triple and data layout for proper ABI handling
    char const * triple;
    char const * triple_to_free = NULL;

    if (target_triple != NULL)
    {
        // Convert common march values to full triples
        if (strcmp(target_triple, "x86-64") == 0 || strcmp(target_triple, "x86_64") == 0)
        {
            triple = "x86_64-pc-linux-gnu";
        }
        else if (strcmp(target_triple, "x86") == 0 || strcmp(target_triple, "i386") == 0)
        {
            triple = "i386-pc-linux-gnu";
        }
        else if (strcmp(target_triple, "aarch64") == 0 || strcmp(target_triple, "arm64") == 0)
        {
            triple = "aarch64-pc-linux-gnu";
        }
        else
        {
            triple = target_triple;
        }
    }
    else
    {
        triple = LLVMGetDefaultTargetTriple();
        triple_to_free = triple;
    }

    LLVMSetTarget(ctx->module, triple);

    LLVMTargetRef target;
    char * error = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &error) == 0)
    {
        LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
            target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
        );
        if (target_machine != NULL)
        {
            LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
            char * layout_str = LLVMCopyStringRepOfTargetData(data_layout);
            LLVMSetDataLayout(ctx->module, layout_str);

            // Get stack alignment from data layout string (e.g., "S128" = 16 bytes)
            ctx->stack_alignment = parse_stack_alignment_from_layout(layout_str);
            if (ctx->stack_alignment == 0)
            {
                // Fallback to default x86_64 stack alignment
                ctx->stack_alignment = 16;
            }

            LLVMDisposeMessage(layout_str);
            LLVMDisposeTargetMachine(target_machine);
        }
    }
    if (error)
    {
        LLVMDisposeMessage(error);
    }
    if (triple_to_free != NULL)
    {
        LLVMDisposeMessage((char *)triple_to_free);
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

        if (char_desc == NULL)
        {
            debug_error("failed to create char desc");
            return NULL;
        }
        TypeDescriptor const * arr_desc = get_or_create_array_type(ctx->type_descriptors, char_desc, len + 1);
        TypedValue val = create_typed_value(ptr, arr_desc, false);
        generator_add_symbol(ctx, "__FILE__", val);
    }

    /* Create va_list for x86_64 System V ABI. */
    {
        // Create struct.__va_list_tag = { i32 gp_offset, i32 fp_offset, ptr overflow_arg_area, ptr reg_save_area }
        LLVMTypeRef va_list_tag_type = LLVMStructCreateNamed(ctx->context, "struct.__va_list_tag");
        // Create struct_field_t entries for the struct type
        // Note: type_desc must be set for calculate_composite_size to work correctly
        TypeDescriptor const * i32_desc
            = get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_int = true}, (TypeQualifier){0});
        TypeDescriptor const * ptr_desc = get_or_create_pointer_type(
            ctx->type_descriptors, type_descriptor_get_int8_type(ctx->type_descriptors, false), (TypeQualifier){0}
        );

        LLVMTypeRef fields[4] = {
            i32_desc->llvm_type, // gp_offset
            i32_desc->llvm_type, // fp_offset
            ptr_desc->llvm_type, // overflow_arg_area
            ptr_desc->llvm_type, // reg_save_area
        };
        LLVMStructSetBody(va_list_tag_type, fields, 4, false);

        struct_field_t field_entries[4]
            = {{.name = "gp_offset", .type_desc = i32_desc},
               {.name = "fp_offset", .type_desc = i32_desc},
               {.name = "overflow_arg_area", .type_desc = ptr_desc},
               {.name = "reg_save_area", .type_desc = ptr_desc}};

        struct_or_union_members_st members = {.num_members = 4, .members = field_entries};

        // Register the struct type
        TypeDescriptor const * va_list_tag_desc
            = register_struct_type(ctx->type_descriptors, va_list_tag_type, (TypeQualifier){0}, false, true, &members);

        // Create [1 x struct.__va_list_tag] array type (va_list is typedef'd as array of 1)
        TypeDescriptor const * va_list_array_desc
            = get_or_create_array_type(ctx->type_descriptors, va_list_tag_desc, 1);

        // Register __builtin_va_list as the array type
        if (generator_find_typedef_type_descriptor(ctx, "__builtin_va_list") == NULL)
        {
            scope_typedef_entry_t typedef_entry
                = {.name = strdup("__builtin_va_list"), .type_desc = va_list_array_desc};
            generator_add_typedef_entry(ctx, typedef_entry);
        }
        if (ctx->generation_flags.generate_default_variables)
        {
            if (generator_find_typedef_type_descriptor(ctx, "va_list") == NULL)
            {
                scope_typedef_entry_t unpreprocessed_typedef_entry
                    = {.name = strdup("va_list"), .type_desc = va_list_array_desc};
                generator_add_typedef_entry(ctx, unpreprocessed_typedef_entry);
            }
            if (generator_find_typedef_type_descriptor(ctx, "_Bool") == NULL)
            {
                scope_typedef_entry_t bool_entry = {
                    .name = strdup("_Bool"),
                    .type_desc = type_descriptor_get_bool_type(ctx->type_descriptors, true),
                };
                generator_add_typedef_entry(ctx, bool_entry);
            }
        }
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
    // Unwrap INITIALIZER wrapper nodes
    if (node->type == AST_NODE_INITIALIZER && node->list.count > 0)
    {
        node = node->list.children[0];
    }

    // Handle String Literals
    if (node->type == AST_NODE_STRING_LITERAL)
    {
        char const * str = decode_string(node->text);
        size_t len = strlen(str);

        // When initializing a pointer (e.g., char *p = "hello"), create a string
        // constant and return a constexpr GEP (pointer to first element).
        if (desc != NULL && desc->kind == NCC_TYPE_KIND_POINTER)
        {
            TypeDescriptor const * char_type = get_or_create_builtin_type(
                ctx->type_descriptors, (TypeSpecifier){.is_char = true}, (TypeQualifier){0}
            );
            TypeDescriptor const * array_type = get_or_create_array_type(ctx->type_descriptors, char_type, len + 1);

            LLVMValueRef global = LLVMAddGlobal(ctx->module, array_type->llvm_type, ".str");
            LLVMSetLinkage(global, LLVMPrivateLinkage);
            LLVMSetGlobalConstant(global, true);
            LLVMSetUnnamedAddr(global, LLVMGlobalUnnamedAddr);
            LLVMSetInitializer(global, LLVMConstStringInContext(ctx->context, str, (unsigned)len, false));

            LLVMValueRef indices[2]
                = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};
            LLVMValueRef gep = LLVMConstInBoundsGEP2(array_type->llvm_type, global, indices, 2);
            free((char *)str);
            return gep;
        }

        LLVMValueRef c = LLVMConstStringInContext(ctx->context, str, (unsigned)len, false);
        free((char *)str);
        return c;
    }

    // Handle Initializer Lists: {1, 2, 3}
    if (node->type == AST_NODE_INITIALIZER_LIST)
    {
        if (desc->kind == NCC_TYPE_KIND_ARRAY)
        {
            size_t entry_count = node->list.count;
            size_t array_size = desc->array_metadata.size;
            LLVMValueRef * elems = calloc(array_size, sizeof(*elems));

            size_t current_idx = 0;
            for (size_t e = 0; e < entry_count; e++)
            {
                c_grammar_node_t const * list_entry = node->list.children[e];
                c_grammar_node_t const * desig = list_entry->initializer_list_entry.designation;
                c_grammar_node_t const * init = list_entry->initializer_list_entry.initializer;

                if (desig != NULL)
                {
                    for (size_t d = 0; d < desig->list.count; d++)
                    {
                        c_grammar_node_t const * child = desig->list.children[d];
                        TypedValue idx_val = process_expression(ctx, child);

                        if (idx_val.value == NULL)
                        {
                            ir_gen_error(&ctx->errors, child, "Array designator must be constant");
                            return NULL;
                        }

                        if (!LLVMIsConstant(idx_val.value))
                        {
                            ir_gen_error(&ctx->errors, child, "Array designator value must be constant");
                            return NULL;
                        }

                        LLVMValueRef actual_const = idx_val.value;
                        if (LLVMGetValueKind(idx_val.value) == LLVMGlobalVariableValueKind)
                        {
                            actual_const = LLVMGetInitializer(idx_val.value);
                        }
                        current_idx = (size_t)LLVMConstIntGetZExtValue(actual_const);
                    }
                }

                if (current_idx < array_size)
                {
                    elems[current_idx] = evaluate_constant_initializer(ctx, desc->pointee, init);
                    if (elems[current_idx] != NULL && desc->pointee != NULL)
                    {
                        LLVMTypeRef target_ty = desc->pointee->llvm_type;
                        LLVMTypeRef val_ty = LLVMTypeOf(elems[current_idx]);
                        if (val_ty != target_ty && LLVMGetTypeKind(val_ty) == LLVMIntegerTypeKind
                            && LLVMGetTypeKind(target_ty) == LLVMIntegerTypeKind)
                        {
                            unsigned sw = LLVMGetIntTypeWidth(val_ty);
                            unsigned dw = LLVMGetIntTypeWidth(target_ty);
                            if (sw < dw)
                            {
                                unsigned long long raw = LLVMConstIntGetSExtValue(elems[current_idx]);
                                elems[current_idx] = LLVMConstInt(target_ty, raw, true);
                            }
                        }
                    }
                }
                current_idx++;
            }

            for (uint32_t i = 0; i < array_size; i++)
            {
                if (elems[i] == NULL)
                {
                    elems[i] = LLVMConstNull(desc->pointee->llvm_type);
                }
            }
            LLVMValueRef res = LLVMConstArray(desc->pointee->llvm_type, elems, array_size);
            free(elems);
            return res;
        }
        else if (desc->kind == NCC_TYPE_KIND_STRUCT)
        {
            size_t entry_count = node->list.count;
            size_t field_count = desc->struct_metadata.members.num_members;
            LLVMValueRef * fields = calloc(field_count, sizeof(*fields));

            size_t current_field = 0;
            for (size_t e = 0; e < entry_count; e++)
            {
                c_grammar_node_t const * list_entry = node->list.children[e];
                c_grammar_node_t const * desig = list_entry->initializer_list_entry.designation;
                c_grammar_node_t const * init = list_entry->initializer_list_entry.initializer;

                if (desig != NULL)
                {
                    for (size_t d = 0; d < desig->list.count; d++)
                    {
                        c_grammar_node_t const * child = desig->list.children[d];
                        if (child->type == AST_NODE_IDENTIFIER)
                        {
                            int idx = type_descriptor_find_struct_field_index_from_desc(desc, child->text);
                            if (idx >= 0)
                            {
                                current_field = (size_t)idx;
                            }
                        }
                    }
                }

                if (current_field < field_count)
                {
                    fields[current_field] = evaluate_constant_initializer(
                        ctx, desc->struct_metadata.members.members[current_field].type_desc, init
                    );
                    if (fields[current_field] != NULL)
                    {
                        LLVMTypeRef target_ty
                            = desc->struct_metadata.members.members[current_field].type_desc->llvm_type;
                        LLVMTypeRef val_ty = LLVMTypeOf(fields[current_field]);
                        if (LLVMGetTypeKind(target_ty) == LLVMPointerTypeKind
                            && LLVMGetTypeKind(val_ty) == LLVMIntegerTypeKind
                            && LLVMConstIntGetZExtValue(fields[current_field]) == 0)
                        {
                            fields[current_field] = LLVMConstNull(target_ty);
                        }
                        else if (
                            val_ty != target_ty && LLVMGetTypeKind(val_ty) == LLVMIntegerTypeKind
                            && LLVMGetTypeKind(target_ty) == LLVMIntegerTypeKind
                        )
                        {
                            unsigned sw = LLVMGetIntTypeWidth(val_ty);
                            unsigned dw = LLVMGetIntTypeWidth(target_ty);
                            if (sw < dw)
                            {
                                unsigned long long raw = LLVMConstIntGetSExtValue(fields[current_field]);
                                fields[current_field] = LLVMConstInt(target_ty, raw, true);
                            }
                            else if (sw > dw)
                            {
                                fields[current_field] = LLVMConstTrunc(fields[current_field], target_ty);
                            }
                        }
                    }
                }
                current_field++;
            }

            for (size_t i = 0; i < field_count; i++)
            {
                if (fields[i] == NULL)
                {
                    fields[i] = LLVMConstNull(desc->struct_metadata.members.members[i].type_desc->llvm_type);
                }
            }
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
        // Widen integer if needed to match target type
        if (desc != NULL && LLVMGetTypeKind(LLVMTypeOf(val.value)) == LLVMIntegerTypeKind
            && LLVMGetTypeKind(desc->llvm_type) == LLVMIntegerTypeKind)
        {
            unsigned src_width = LLVMGetIntTypeWidth(LLVMTypeOf(val.value));
            unsigned dst_width = LLVMGetIntTypeWidth(desc->llvm_type);
            if (src_width < dst_width)
            {
                unsigned long long raw_val = LLVMConstIntGetSExtValue(val.value);

                return LLVMConstInt(desc->llvm_type, raw_val, true);
            }
        }
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
    bool is_static,
    c_grammar_node_t const * initializer_expr_node
)
{
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

    // Handle unsized array with initializer list: int arr[] = {1, 2, 3};
    if (type_desc->kind == NCC_TYPE_KIND_ARRAY && type_desc->array_metadata.size == 0 && initializer_expr_node
        && initializer_expr_node->type == AST_NODE_INITIALIZER_LIST)
    {
        size_t array_size = initializer_expr_node->list.count;
        final_desc = get_or_create_array_type(ctx->type_descriptors, type_desc->pointee, array_size);
    }

    // 2. Create the Global Identity
    global_var = LLVMGetNamedGlobal(ctx->module, var_name);
    if (global_var == NULL)
    {
        global_var = LLVMAddGlobal(ctx->module, final_desc->llvm_type, var_name);
    }
    if (is_static)
    {
        LLVMSetLinkage(global_var, LLVMInternalLinkage);
    }
    else
    {
        LLVMSetLinkage(global_var, LLVMExternalLinkage);
    }
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
    // 1. Resolve the TypeDescriptor immediately.
    // This handles pointers, arrays, functions, and typedefs in one go.
    TypeDescriptor const * type_desc = resolve_type_descriptor(ctx, decl_specifiers, declarator_node);
    if (type_desc == NULL)
    {
        ir_gen_error(&ctx->errors, decl_specifiers, "Unknown type");
        return;
    }

    // 2. Extract Identifier (Variable Name)
    char const * var_name = search_for_identifier(declarator_node);
    if (var_name == NULL)
    {
        ir_gen_error(&ctx->errors, decl_specifiers, "Unspecified dentifier");
        return;
    }

    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    bool is_global = (current_block == NULL);
    bool is_static = false;
    bool is_extern = false;

    if (decl_specifiers != NULL && decl_specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        is_static = decl_specifiers->decl_specifiers.storage.has_static;
        is_extern = decl_specifiers->decl_specifiers.storage.has_extern;
    }

    // 3. Global / Static Storage
    if (type_desc->kind == NCC_TYPE_KIND_FUNCTION)
    {
        // Check if the function is already in the LLVM module
        LLVMValueRef func = LLVMGetNamedFunction(ctx->module, var_name);

        if (!func)
        {
            func = LLVMAddFunction(ctx->module, var_name, type_desc->llvm_type);

            // Standard C function declarations have "External" linkage by default
            if (is_static)
            {
                LLVMSetLinkage(func, LLVMInternalLinkage);
            }
            else
            {
                LLVMSetLinkage(func, LLVMExternalLinkage);
            }
        }

        // Add it to your symbol table so the compiler can resolve calls to it.
        // Functions are usually treated as R-values (the address of the function).
        TypedValue func_val = create_typed_value(func, type_desc, false);
        generator_add_symbol(ctx, var_name, func_val);

        return;
    }

    // Handle extern variable declarations at file scope (without initializer)
    if (is_extern && is_global)
    {
        c_grammar_node_t const * init_expr = (init_decl_initializer && init_decl_initializer->list.count > 0)
                                                 ? init_decl_initializer->list.children[0]
                                                 : NULL;
        if (init_expr == NULL)
        {
            /* Create an external reference — defined in another translation unit */
            LLVMValueRef global = LLVMGetNamedGlobal(ctx->module, var_name);
            if (global == NULL)
            {
                global = LLVMAddGlobal(ctx->module, type_desc->llvm_type, var_name);
                LLVMSetLinkage(global, LLVMExternalLinkage);
            }
            TypedValue val = create_typed_value(global, type_desc, true);
            generator_add_symbol(ctx, var_name, val);
            return;
        }
    }

    if (is_static || is_global)
    {
        c_grammar_node_t const * init_expr = (init_decl_initializer && init_decl_initializer->list.count > 0)
                                                 ? init_decl_initializer->list.children[0]
                                                 : NULL;

        create_global_variable(ctx, type_desc, var_name, false, is_static, init_expr);

        return;
    }

    // 4. Local Storage (Stack)
    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, type_desc->llvm_type, var_name);

    // Set alignment based on type kind:
    // - Structs/Unions: use the struct's calculated alignment (max of member alignments)
    // - Arrays: use stack alignment (minimum of stack alignment and element alignment)
    // - Other types: LLVM defaults to ABI alignment (already set by LLVMBuildAlloca)
    if (type_desc->kind == NCC_TYPE_KIND_STRUCT || type_desc->kind == NCC_TYPE_KIND_UNION)
    {
        // Use the struct's calculated alignment from calculate_composite_size()
        uint32_t align = type_desc->struct_metadata.alignment;

        LLVMSetAlignment(alloca_inst, align);
    }
    else if (type_desc->kind == NCC_TYPE_KIND_ARRAY && type_desc->pointee != NULL)
    {
        // Stack arrays: use max of stack alignment or element alignment
        // Clang uses stack alignment (e.g., 16 on x86_64) for stack arrays
        uint32_t elem_align = get_type_alignment_desc(ctx->data_layout, type_desc->pointee);
        uint32_t align = (elem_align > ctx->stack_alignment) ? elem_align : ctx->stack_alignment;

        LLVMSetAlignment(alloca_inst, align);
    }

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
        /* Shouldn't this be a compile error? */
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
                ir_gen_error(&ctx->errors, node, "Function '%s' already defined.", func_name);
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
        func = LLVMAddFunction(ctx->module, func_name, type_desc->llvm_type);
    }

    bool is_static = false;
    if (decl_specifiers_node != NULL && decl_specifiers_node->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        is_static = decl_specifiers_node->decl_specifiers.storage.has_static;
    }
    if (is_static)
    {
        LLVMSetLinkage(func, LLVMInternalLinkage);
    }
    else
    {
        LLVMSetLinkage(func, LLVMExternalLinkage);
    }

    bool is_large_struct_ret = false;

    if (type_desc->kind == NCC_TYPE_KIND_FUNCTION
        && (type_desc->function_metadata.return_type->kind == NCC_TYPE_KIND_STRUCT
            || type_desc->function_metadata.return_type->kind == NCC_TYPE_KIND_UNION))
    {
        is_large_struct_ret = get_type_size_desc(ctx->data_layout, type_desc->function_metadata.return_type) > 16;
    }
    ctx->current_function_sret_ptr = NULL;
    if (is_large_struct_ret)
    {
        // The first LLVM parameter is the hidden sret pointer
        ctx->current_function_sret_ptr = LLVMGetParam(func, 0);

        // Create the sret attribute type tracking
        LLVMAttributeRef sret_attr = LLVMCreateTypeAttribute(
            LLVMGetGlobalContext(),
            LLVMGetEnumAttributeKindForName("sret", 4),
            type_desc->function_metadata.return_type->llvm_type // Must match the structural type it points to
        );

        // On a function declaration/definition, parameter index 1 is the 1st parameter
        LLVMAddAttributeAtIndex(func, 1, sret_attr);
    }

    // Create a basic block for the function's entry point.
    LLVMBasicBlockRef entry_block = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry_block);

    /* Add inlinehint attribute if function has inline specifier */
    if (decl_specifiers_node->type == AST_NODE_NAMED_DECL_SPECIFIERS
        && decl_specifiers_node->decl_specifiers.function_specifier != NULL)
    {
        // 1. Create the attribute
        unsigned int kind = LLVMGetEnumAttributeKindForName("inlinehint", 10);
        LLVMAttributeRef attr = LLVMCreateEnumAttribute(ctx->context, kind, 0);
        // 2. Add it to the FUNCTION index, not the parameter index
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex, attr);
    }

    // --- Handle function parameters: allocate space and store arguments ---
    c_grammar_node_t const * param_list = search_parameters_list_in_declarator(declarator_node);

    if (param_list == NULL)
    {
        debug_error("failed to find_parameter list");
        generator_scope_pop(ctx);
        return;
    }
    parameter_definitions_t params = extract_function_parameters(ctx, param_list);

    int llvm_arg_idx = is_large_struct_ret ? 1 : 0;
    for (size_t i = 0; i < params.count; ++i)
    {
        char const * p_name = params.names[i];
        TypeDescriptor const * p_type_desc = params.types[i];
        LLVMValueRef alloca_inst
            = LLVMBuildAlloca(ctx->builder, p_type_desc->llvm_type, p_name != NULL ? p_name : "fn_param");

        if (p_type_desc->kind == NCC_TYPE_KIND_STRUCT || p_type_desc->kind == NCC_TYPE_KIND_UNION)
        {
            uint64_t size = get_type_size_desc(ctx->data_layout, p_type_desc);

            if (size > 16)
            {
                // CASE 1: Large Struct (> 16 bytes)
                // The parameter in LLVM is actually a 'ptr' to the struct data.
                LLVMValueRef hidden_ptr = LLVMGetParam(func, llvm_arg_idx++);

                // Use memcpy to move data from the hidden pointer to our local stack
                // This preserves "pass-by-value" semantics (the function gets its own copy)
                LLVMValueRef size_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), size, false);
                LLVMBuildMemCpy(ctx->builder, alloca_inst, 8, hidden_ptr, 8, size_val);
            }
            else
            {
                CoercedType coerced = type_desc->function_metadata.coerced_params[i];

                // CASE 2: Small Struct (1-16 bytes)
                // The struct is shattered across 1 or 2 registers (i64, etc.)
                for (int p = 0; p < coerced.count; p++)
                {
                    LLVMValueRef reg_val = LLVMGetParam(func, llvm_arg_idx++);
                    stitch_param_part(ctx->type_descriptors, alloca_inst, reg_val, p);
                }
            }
        }
        else
        {
            // CASE 3: Standard Scalars (int, ptr, float)
            LLVMValueRef reg_val = LLVMGetParam(func, llvm_arg_idx++);
            LLVMBuildStore(ctx->builder, reg_val, alloca_inst);
        }

        if (p_name != NULL)
        {
            TypedValue p_val = create_typed_value(alloca_inst, p_type_desc, true);
            generator_add_symbol(ctx, p_name, p_val);
        }
    }

    parameter_definitions_cleanup(&params);

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
        if (is_large_struct_ret
            || LLVMGetTypeKind(type_desc->function_metadata.return_type->llvm_type) == LLVMVoidTypeKind)
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            /* Be sure to use the return type of the function. */
            LLVMBuildRet(ctx->builder, LLVMConstNull(type_desc->function_metadata.return_type->llvm_type));
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
        ir_gen_error(&ctx->errors, node, "Internal Error: No return type descriptor found for current function");
        return;
    }

    if (ctx->current_function_sret_ptr != NULL)
    {
        // sret function: store return value through the hidden pointer and ret void
        if (expr_node != NULL)
        {
            TypedValue return_value = process_expression(ctx, expr_node);
            if (return_value.value == NULL)
            {
                ir_gen_error(&ctx->errors, node, "failed to process return expression.");
                return;
            }
            return_value = ensure_lvalue(ctx, "sret_store", return_value);
            LLVMBuildMemCpy(
                ctx->builder,
                ctx->current_function_sret_ptr,
                8,
                return_value.value,
                8,
                LLVMConstInt(
                    LLVMInt64TypeInContext(ctx->context), get_type_size_desc(ctx->data_layout, expected_ret_desc), false
                )
            );
        }
        LLVMBuildRetVoid(ctx->builder);
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
            ir_gen_warning(&ctx->errors, node, "Return without value in non-void function.");

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
    c_grammar_node_t const * specifiers_list = decl_specifiers->decl_specifiers.type_specifiers;

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

static void
process_typedef_declaration(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    /* Handle TypedefDeclaration node: [KwExtension, DeclarationSpecifiers, InitDeclaratorList] */
    c_grammar_node_t const * decl_specs = node->declaration.declaration_specifiers;
    c_grammar_node_t const * init_declarator_list = node->declaration.init_declarator_list;

    /* Iterate over all TypedefInitDeclarators */
    for (size_t i = 0; i < init_declarator_list->list.count; ++i)
    {
        c_grammar_node_t const * typedef_init_decl = init_declarator_list->list.children[i];

        /* TypedefInitDeclarator -> TypedefDeclarator -> Identifier (via find_typedef_name_node) */
        c_grammar_node_t const * typedef_decl = typedef_init_decl->init_declarator.declarator;

        TypeDescriptor const * typedef_type_desc = resolve_type_descriptor(ctx, decl_specs, typedef_decl);
        if (typedef_type_desc == NULL)
        {
            debug_error("%s: Failed to get type desc for typedef", __func__);
            ir_gen_error(&ctx->errors, node, "Failed to resolve typedef.");
            return;
        }

        c_grammar_node_t const * name_node = find_typedef_name_node(typedef_decl);
        if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER || name_node->text == NULL)
        {
            debug_error("%s: Failed to find typedef name", __func__);
            ir_gen_error(&ctx->errors, node, "Failed to find typedef name.");
            return;
        }

        char const * typedef_name = name_node->text;
        TypeDescriptor const * existing_desc = generator_find_typedef_type_descriptor(ctx, typedef_name);

        if (existing_desc != NULL)
        {
            if (existing_desc != typedef_type_desc)
            {
                ir_gen_error(&ctx->errors, node, "Redefinition of typedef '%s'.", typedef_name);
                return;
            }
        }
        else
        {
            scope_typedef_entry_t typedef_entry = {
                .name = strdup(typedef_name),
                .type_desc = typedef_type_desc,
            };
            generator_add_typedef_entry(ctx, typedef_entry);
        }
    }
}

static void
process_preprocessor_line_marker(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    ast_node_preprocessor_line_marker_t const * marker = &node->line_marker;

    /* 2. Update the tracker with the mapping */
    source_location_tracker_add_entry(
        ctx->errors.loc_tracker, node->source_data.view, marker->line_number, marker->filename
    );

    /* 3. Handle include stack based on flags */
    for (size_t i = 0; i < marker->flags_count; i++)
    {
        if (marker->flags[i] == 1) /* Enter include */
        {
            source_location_tracker_push_include(ctx->errors.loc_tracker, marker->filename, marker->line_number);
        }
        else if (marker->flags[i] == 2) /* Exit include */
        {
            source_location_tracker_pop_include(ctx->errors.loc_tracker);
        }
    }
}

static void
process_for_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
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
    process_ast_node(ctx, init_node);
    if (ctx->errors.fatal)
    {
        ctx->break_target = old_break_target;
        ctx->continue_target = old_continue_target;
        return;
    }

    LLVMBuildBr(ctx->builder, cond_block);

    // 2. Emit Cond block
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
}

static void
process_while_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
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
}

static void
process_do_while_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
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
}

static void
process_translation_unit(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node->translation_unit.external_declarations == NULL)
    {
        debug_error("Translation unit is missing external declarations.");
        return;
    }
    generator_scope_push(ctx);
    process_ast_node(ctx, node->translation_unit.external_declarations);
    generator_scope_pop(ctx);
}

static void
process_external_declarations(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Process each external declaration (could be variable or function)
    if (node->list.children)
    {
        for (size_t i = 0; i < node->list.count; ++i)
        {
            process_ast_node(ctx, node->list.children[i]);
        }
    }
}

static void
process_external_declaration(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node->external_declaration.preprocessor_directive != NULL)
    {
        process_ast_node(ctx, node->external_declaration.preprocessor_directive);
    }
    else if (node->external_declaration.top_level_declaration != NULL)
    {
        process_ast_node(ctx, node->external_declaration.top_level_declaration);
    }
}

static void
process_compound_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Create new scope for this block
    generator_scope_push(ctx);

    for (size_t i = 0; i < node->list.count; ++i)
    {
        process_ast_node(ctx, node->list.children[i]);
    }

    // Pop block scope when exiting
    generator_scope_pop(ctx);
}

static void
process_top_level_declaration(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    process_ast_node(ctx, node->top_level_declaration.declaration);
}

static void
process_expression_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * expr_node = node->expression_statement.expression;

    process_expression(ctx, expr_node);
}

static void
process_case_label(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // CaseLabel: [case_expr, (optional) statement]
    // If there's a statement, process it
    if (node->list.count == 2)
    {
        c_grammar_node_t * stmt = node->list.children[1];
        process_ast_node(ctx, stmt);
    }
}

static void
process_break_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    (void)node;

    // Break statement: jump to the enclosing switch/loop's after block
    if (ctx->break_target)
    {
        LLVMBuildBr(ctx->builder, ctx->break_target);
    }
    else
    {
        debug_error("break statement not within a loop or switch.");
    }
}

static void
process_continue_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    (void)node;

    // Continue statement: jump to the enclosing loop's continue (post) block
    if (ctx->continue_target)
    {
        LLVMBuildBr(ctx->builder, ctx->continue_target);
    }
    else
    {
        debug_error("continue statement not within a loop.");
    }
}

static void
process_switch_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
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

    // Collect all case values
    typedef struct
    {
        long long value;
        LLVMValueRef const_val;
        LLVMBasicBlockRef target;
    } CaseEntry;

    CaseEntry * case_entries = NULL;
    size_t case_count = 0;

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

        // Collect each case value from this SwitchCase
        c_grammar_node_t const * switch_case_node = items[i].node;
        for (size_t j = 0; j < switch_case_node->switch_case.labels->list.count; j++)
        {
            c_grammar_node_t const * child = switch_case_node->switch_case.labels->list.children[j];
            if (child->list.count >= 1)
            {
                TypedValue case_val = process_expression(ctx, child->list.children[0]);
                if (case_val.value == NULL)
                {
                    free(case_entries);
                    return;
                }
                case_val = ensure_rvalue(ctx, "switch_case_rval", case_val);

                CaseEntry * tmp = realloc(case_entries, (case_count + 1) * sizeof(*tmp));
                if (tmp == NULL)
                {
                    free(case_entries);
                    return;
                }
                case_entries = tmp;
                case_entries[case_count].value = LLVMConstIntGetSExtValue(case_val.value);
                case_entries[case_count].const_val = case_val.value;
                case_entries[case_count].target = items[i].body_block ? items[i].body_block : fallthrough_target;
                case_count++;
            }
        }
    }

    // Sort case entries by value (LLVM requires strictly increasing order)
    for (size_t i = 0; i < case_count; i++)
    {
        for (size_t k = i + 1; k < case_count; k++)
        {
            if (case_entries[k].value < case_entries[i].value)
            {
                CaseEntry tmp = case_entries[i];
                case_entries[i] = case_entries[k];
                case_entries[k] = tmp;
            }
        }
    }

    // Add cases in sorted order
    for (size_t i = 0; i < case_count; i++)
    {
        LLVMAddCase(switch_inst, case_entries[i].const_val, case_entries[i].target);
    }

    free(case_entries);

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
}

static void
process_if_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
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

    condition_val = cast_typed_value_to_desc(
        ctx,
        condition_val,
        get_or_create_builtin_type(ctx->type_descriptors, (TypeSpecifier){.is_bool = true}, (TypeQualifier){0})
    );

    if (else_node)
    {
        LLVMBuildCondBr(ctx->builder, condition_val.value, then_block, else_block);
    }
    else
    {
        LLVMBuildCondBr(ctx->builder, condition_val.value, then_block, merge_block);
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
}

static void
process_goto_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    char const * label_name = node->goto_statement.label->text;
    LLVMBasicBlockRef target = generator_get_or_create_label(ctx, label_name);
    LLVMBuildBr(ctx->builder, target);

    // Start a new basic block for any code after goto (which is technically unreachable
    // unless there's a label).
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef unreachable = LLVMAppendBasicBlockInContext(ctx->context, func, "unreachable");
    LLVMPositionBuilderAtEnd(ctx->builder, unreachable);
}

static void
process_labeled_statement(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
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
        process_translation_unit(ctx, node);
        break;
    }
    case AST_NODE_EXTERNAL_DECLARATIONS:
    {
        process_external_declarations(ctx, node);
        break;
    }
    case AST_NODE_EXTERNAL_DECLARATION:
    {
        process_external_declaration(ctx, node);
        break;
    }
    case AST_NODE_TOP_LEVEL_DECLARATION:
    {
        process_top_level_declaration(ctx, node);
        break;
    }
    case AST_NODE_PREPROCESSOR_LINE_MARKER:
    {
        process_preprocessor_line_marker(ctx, node);
        break;
    }
    case AST_NODE_FUNCTION_DEFINITION:
    {
        process_function_definition(ctx, node);
        break;
    }
    case AST_NODE_COMPOUND_STATEMENT:
    {
        process_compound_statement(ctx, node);
        break;
    }
    case AST_NODE_EXPRESSION_STATEMENT:
    {
        process_expression_statement(ctx, node);
        break;
    }
    case AST_NODE_DECLARATION:
    {
        process_declaration(ctx, node);
        break;
    }
    case AST_NODE_TYPEDEF_DECLARATION:
    {
        process_typedef_declaration(ctx, node);
        break;
    }
    case AST_NODE_FOR_STATEMENT:
    {
        process_for_statement(ctx, node);
        break;
    }
    case AST_NODE_WHILE_STATEMENT:
    {
        process_while_statement(ctx, node);
        break;
    }
    case AST_NODE_DO_WHILE_STATEMENT:
    {
        process_do_while_statement(ctx, node);
        break;
    }
    case AST_NODE_CASE_LABEL:
    {
        process_case_label(ctx, node);
        break;
    }
    case AST_NODE_BREAK_STATEMENT:
    {
        process_break_statement(ctx, node);
        break;
    }
    case AST_NODE_CONTINUE_STATEMENT:
    {
        process_continue_statement(ctx, node);
        break;
    }
    case AST_NODE_SWITCH_STATEMENT:
    {
        process_switch_statement(ctx, node);
        break;
    }
    case AST_NODE_IF_STATEMENT:
    {
        process_if_statement(ctx, node);
        break;
    }
    case AST_NODE_RETURN_STATEMENT:
    {
        process_return_statement(ctx, node);
        break;
    }
    case AST_NODE_GOTO_STATEMENT:
    {
        process_goto_statement(ctx, node);
        break;
    }
    case AST_NODE_LABELED_STATEMENT:
    {
        process_labeled_statement(ctx, node);
        break;
    }

    case AST_NODE_PREPROCESSOR_DIRECTIVE:
    {
        // For now, we can ignore preprocessor directives in IR generation
        break;
    }
    case AST_NODE_POSTFIX_PARTS:
    case AST_NODE_STRUCT_DEFINITION:
    case AST_NODE_UNION_DEFINITION:
    case AST_NODE_ASSIGNMENT:
    case AST_NODE_FLOAT_BASE:
    case AST_NODE_INTEGER_LITERAL:
    case AST_NODE_FLOAT_LITERAL:
    case AST_NODE_STRING_LITERAL_PART:
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
    case AST_NODE_TYPE_QUALIFIERS:
    case AST_NODE_DECLARATION_SPECIFIERS:
    case AST_NODE_STORAGE_CLASS_SPECIFIERS:
    case AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER:
    case AST_NODE_TYPE_SPECIFIERS:
    case AST_NODE_PARAMETER_LIST:
    case AST_NODE_ELLIPSIS:
    case AST_NODE_VA_ARG_EXPRESSION:
    case AST_NODE_TYPEOF_SPECIFIER:
    case AST_NODE_OFFSETOF_MEMBER:
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

    _process_ast_node(ctx, node);
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
    LLVMValueRef val = LLVMConstRealOfString(type_desc->llvm_type, _node->list.children[0]->text);
    TypedValue res = create_typed_value(val, type_desc, false);

    return res;
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

static LLVMValueRef
get_llvm_va_start(ir_generator_ctx_t * ctx)
{
    char const * fn_name = "llvm.va_start";

    // 1. Try to find it if already declared
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fn_name);
    if (fn != NULL)
    {
        return fn;
    }

    // 2. Explicitly declare it WITHOUT mangling
    // va_start takes one parameter: a pointer (ptr)
    LLVMTypeRef param_types[] = {LLVMPointerTypeInContext(ctx->context, 0)};
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), param_types, 1, false);

    return LLVMAddFunction(ctx->module, fn_name, fn_type);
}

static LLVMValueRef
get_llvm_va_end(ir_generator_ctx_t * ctx)
{
    char const * fn_name = "llvm.va_end";

    // 1. Check if the intrinsic is already declared in the module
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fn_name);
    if (fn != NULL)
    {
        return fn;
    }

    // 2. Define the parameter type: a single pointer (ptr)
    // Using PointerTypeInContext ensures compatibility with Opaque Pointers
    LLVMTypeRef param_types[] = {LLVMPointerTypeInContext(ctx->context, 0)};

    // 3. Create the function type: void llvm.va_end(ptr)
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), param_types, 1, false);

    // 4. Add the function to the module with the generic intrinsic name
    // We avoid the intrinsic API here to prevent the .p0 mangling
    // that was confusing the x86 backend.
    return LLVMAddFunction(ctx->module, fn_name, fn_type);
}

static LLVMValueRef
get_llvm_va_copy(ir_generator_ctx_t * ctx)
{
    char const * fn_name = "llvm.va_copy";

    // 1. Check if already declared
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fn_name);
    if (fn != NULL)
    {
        return fn;
    }

    // 2. Define the parameter types: (ptr, ptr)
    // Both destination and source are pointers to the va_list tag
    LLVMTypeRef param_types[] = {LLVMPointerTypeInContext(ctx->context, 0), LLVMPointerTypeInContext(ctx->context, 0)};

    // 3. Create the function type: void llvm.va_copy(ptr, ptr)
    LLVMTypeRef fn_type = LLVMFunctionType(
        LLVMVoidTypeInContext(ctx->context),
        param_types,
        2, /* Two arguments */
        false
    );

    // 4. Add the function with the generic intrinsic name
    return LLVMAddFunction(ctx->module, fn_name, fn_type);
}

static TypedValue
process_va_start(ir_generator_ctx_t * ctx, c_grammar_node_t const * suffix)
{
    if (suffix->list.count < 1)
    {
        ir_gen_error(&ctx->errors, suffix, "va_start requires at least one argument");
        return NullTypedValue;
    }

    TypedValue va_list_val = process_expression(ctx, suffix->list.children[0]);
    if (va_list_val.value == NULL)
    {
        return NullTypedValue;
    }

    // Call llvm.va.start intrinsic
    LLVMValueRef fn = get_llvm_va_start(ctx);
    if (fn == NULL)
    {
        ir_gen_error(&ctx->errors, suffix, "va_start intrinsic not found");
        return NullTypedValue;
    }
    LLVMTypeRef i8_ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), &i8_ptr_type, 1, false);

    LLVMValueRef va_list_ptr = LLVMBuildBitCast(ctx->builder, va_list_val.value, i8_ptr_type, "va_list_cast");

    LLVMBuildCall2(ctx->builder, fn_type, fn, &va_list_ptr, 1, "");

    return NullTypedValue;
}

static TypedValue
process_va_end(ir_generator_ctx_t * ctx, c_grammar_node_t const * suffix)
{
    if (suffix->list.count < 1)
    {
        ir_gen_error(&ctx->errors, suffix, "va_end requires at least one argument");
        return NullTypedValue;
    }

    TypedValue va_list_val = process_expression(ctx, suffix->list.children[0]);
    if (va_list_val.value == NULL)
    {
        return NullTypedValue;
    }

    // Call llvm.va.end intrinsic
    LLVMValueRef fn = get_llvm_va_end(ctx);
    if (fn == NULL)
    {
        ir_gen_error(&ctx->errors, suffix, "va_end intrinsic not found");
        return NullTypedValue;
    }
    LLVMTypeRef i8_ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);

    // Cast the va_list to i8* (required by the intrinsic)
    LLVMValueRef va_list_ptr = LLVMBuildBitCast(ctx->builder, va_list_val.value, i8_ptr_type, "va_end_cast");

    // The intrinsic returns void
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), &i8_ptr_type, 1, false);
    LLVMBuildCall2(ctx->builder, fn_type, fn, &va_list_ptr, 1, "");

    return NullTypedValue;
}

static TypedValue
process_va_copy(ir_generator_ctx_t * ctx, c_grammar_node_t const * suffix)
{
    if (suffix->list.count < 2)
    {
        ir_gen_error(&ctx->errors, suffix, "va_copy requires two arguments");
        return NullTypedValue;
    }

    TypedValue dest_val = process_expression(ctx, suffix->list.children[0]);
    if (dest_val.value == NULL)
    {
        return NullTypedValue;
    }

    TypedValue src_val = process_expression(ctx, suffix->list.children[1]);
    if (src_val.value == NULL)
    {
        return NullTypedValue;
    }

    LLVMValueRef fn = get_llvm_va_copy(ctx);
    if (fn == NULL)
    {
        ir_gen_error(&ctx->errors, suffix, "va_copy intrinsic not found");
        return NullTypedValue;
    }

    LLVMValueRef args[2] = {dest_val.value, src_val.value};
    LLVMBuildCall2(ctx->builder, LLVMGetElementType(LLVMTypeOf(fn)), fn, args, 2, "");

    return NullTypedValue;
}

static TypedValue
process_va_arg_expr(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * va_list_node = node->va_arg_expression.args;
    c_grammar_node_t const * type_name_node = node->va_arg_expression.arg_type;
    TypedValue va_list_val = process_expression(ctx, va_list_node);

    if (va_list_val.value == NULL)
    {
        return NullTypedValue;
    }

    c_grammar_node_t const * spec_qual = type_name_node->type_name.specifier_qualifier_list;
    c_grammar_node_t const * abstract_decl = type_name_node->type_name.abstract_declarator;
    TypeDescriptor const * target_type = resolve_type_descriptor(ctx, spec_qual, abstract_decl);

    if (target_type == NULL)
    {
        ir_gen_error(&ctx->errors, type_name_node, "Could not resolve type in va_arg");
        return NullTypedValue;
    }

    LLVMValueRef result = LLVMBuildVAArg(ctx->builder, va_list_val.value, target_type->llvm_type, "va_arg_tmp");

    return create_typed_value(result, target_type, false);
}

LLVMValueRef
extract_param_part(ir_generator_ctx_t * ctx, TypedValue struct_val, int part_idx, LLVMTypeRef target_type)
{
    // 1. Get the address of the struct (from your TypedValue)
    // struct_val = ensure_rvalue(ctx, "struct_ptr_load", struct_val);
    LLVMValueRef addr = struct_val.value;

    // 2. Calculate the offset (0 or 8 bytes)
    LLVMValueRef offset = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), part_idx * 8, false);

    // 3. GEP to the chunk
    LLVMValueRef byte_ptr
        = LLVMBuildInBoundsGEP2(ctx->builder, LLVMInt8TypeInContext(ctx->context), addr, &offset, 1, "abi_extract_ptr");

    // 4. Cast and Load the value for the register
    LLVMValueRef typed_ptr
        = LLVMBuildBitCast(ctx->builder, byte_ptr, LLVMPointerType(target_type, 0), "abi_extract_cast");

    return LLVMBuildLoad2(ctx->builder, target_type, typed_ptr, "abi_part");
}

static TypedValue
process_function_call(ir_generator_ctx_t * ctx, c_grammar_node_t const * suffix, TypedValue current_val)
{
    TypedValue callable = ensure_rvalue(ctx, "func_ptr_load", current_val);

    TypeDescriptor const * func_desc = NULL;
    if (callable.type_info->kind == NCC_TYPE_KIND_FUNCTION)
    {
        func_desc = callable.type_info;
    }
    else if (
        callable.type_info->kind == NCC_TYPE_KIND_POINTER && callable.type_info->pointee->kind == NCC_TYPE_KIND_FUNCTION
    )
    {
        func_desc = callable.type_info->pointee;
    }

    if (!func_desc)
    {
        ir_gen_error(&ctx->errors, suffix, "Expression is not a function or function pointer");
        return NullTypedValue;
    }

    TypeDescriptor const * ret_desc = func_desc->function_metadata.return_type;
    bool is_large_struct_ret = false;
    LLVMValueRef sret_alloca = NULL;

    // ABI Rule: Check if the return value is a structure larger than 16 bytes
    if (ret_desc && (ret_desc->kind == NCC_TYPE_KIND_STRUCT || ret_desc->kind == NCC_TYPE_KIND_UNION))
    {
        uint64_t ret_size = get_type_size_desc(ctx->data_layout, ret_desc);
        if (ret_size > 16)
        {
            is_large_struct_ret = true;
        }
    }

    size_t num_args = suffix->list.count;
    // Account for the extra slot if we have a hidden pointer
    size_t max_llvm_params = (2 * num_args) + (is_large_struct_ret ? 1 : 0);
    LLVMValueRef * call_args = max_llvm_params > 0 ? calloc(max_llvm_params, sizeof(*call_args)) : NULL;
    size_t total_llvm_params = 0;

    // 1. Handle hidden sret argument if needed
    if (is_large_struct_ret)
    {
        // Allocate space on our stack frame for the return structure
        sret_alloca = LLVMBuildAlloca(ctx->builder, ret_desc->llvm_type, "sret_tmp");
        // Pass the pointer to this space as the FIRST argument
        call_args[total_llvm_params++] = sret_alloca;
    }

    // 2. Process Arguments (your existing loop)
    for (size_t j = 0; j < num_args; ++j)
    {
        TypedValue arg = process_expression(ctx, suffix->list.children[j]);
        if (arg.value == NULL)
        {
            return NullTypedValue;
        }

        TypeDescriptor const * type_to_check
            = j < func_desc->function_metadata.c_param_count ? func_desc->function_metadata.params[j] : arg.type_info;

        if (type_to_check->kind == NCC_TYPE_KIND_STRUCT || type_to_check->kind == NCC_TYPE_KIND_UNION)
        {
            uint64_t size = get_type_size_desc(ctx->data_layout, type_to_check);
            if (size > 16)
            {
                arg = ensure_lvalue(ctx, "abi_large_tmp", arg);
                call_args[total_llvm_params++] = arg.value;
            }
            else
            {
                arg = ensure_lvalue(ctx, "abi_small_tmp", arg);
                CoercedType coerced = get_coerced_llvm_types(ctx->type_descriptors, type_to_check);
                for (int p = 0; p < coerced.count; p++)
                {
                    call_args[total_llvm_params++] = extract_param_part(ctx, arg, p, coerced.types[p]);
                }
            }
        }
        else
        {
            TypedValue direct_arg = arg;
            if (j < func_desc->function_metadata.c_param_count)
            {
                direct_arg = cast_typed_value_to_desc(ctx, direct_arg, func_desc->function_metadata.params[j]);
            }
            direct_arg = ensure_rvalue(ctx, "arg_load", direct_arg);
            call_args[total_llvm_params++] = direct_arg.value;
        }
    }

    // 3. Construct the Call Instruction
    LLVMValueRef call_val = LLVMBuildCall2(
        ctx->builder,
        func_desc->llvm_type,
        callable.value,
        call_args,
        (unsigned)total_llvm_params,
        (is_void_return(func_desc) || is_large_struct_ret) ? "" : "call_tmp"
    );

    // If it's an sret call, we should attach the sret attribute to parameter index 0
    // to tell LLVM explicitly that it is a structure return pointer.
    if (is_large_struct_ret)
    {
        // LLVM 11+ way of attaching sret type attribute to parameter 0 (1st argument)
        LLVMAttributeRef sret_attr = LLVMCreateTypeAttribute(
            LLVMGetGlobalContext(), // or your explicit context reference
            LLVMGetEnumAttributeKindForName("sret", 4),
            ret_desc->llvm_type
        );
        LLVMAddCallSiteAttribute(call_val, 1, sret_attr); // Argument index 1 in C API maps to LLVM Param 0
    }

    free(call_args);

    // 4. Return value packaging
    if (is_large_struct_ret)
    {
        // For a large struct, the expression behaves as an LVALUE pointing to our stack buffer
        current_val = create_typed_value(sret_alloca, ret_desc, true);
    }
    else if (is_void_return(func_desc))
    {
        // Do not pass call_val! A void expression has no physical value register.
        current_val = NullTypedValue;
    }
    else
    {
        current_val = create_typed_value(call_val, ret_desc, false);
    }

    return current_val;
}

static TypedValue
process_postfix_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * base_node = node->postfix_expression.base_expression;
    c_grammar_node_t const * postfix_parts_node = node->postfix_expression.postfix_parts;

    // Check for va_start, va_end, va_copy (and their __builtin_ variants) before processing the base expression
    // These need special handling as they're not regular function calls
    if (base_node != NULL && base_node->type == AST_NODE_IDENTIFIER && base_node->text != NULL)
    {
        for (size_t i = 0; i < postfix_parts_node->list.count; ++i)
        {
            c_grammar_node_t * suffix = postfix_parts_node->list.children[i];

            if (suffix->type == AST_NODE_OPTIONAL_ARGUMENT_LIST)
            {
                if (strcmp(base_node->text, "va_start") == 0 || strcmp(base_node->text, "__builtin_va_start") == 0)
                {
                    return process_va_start(ctx, suffix);
                }
                else if (strcmp(base_node->text, "va_end") == 0 || strcmp(base_node->text, "__builtin_va_end") == 0)
                {
                    return process_va_end(ctx, suffix);
                }
                else if (strcmp(base_node->text, "va_copy") == 0 || strcmp(base_node->text, "__builtin_va_copy") == 0)
                {
                    return process_va_copy(ctx, suffix);
                }
                break;
            }
        }
    }

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
            current_val = process_function_call(ctx, suffix, current_val);
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
            bool is_arrow = suffix->type == AST_NODE_MEMBER_ACCESS_ARROW;
            char const * member_name = suffix->identifier.identifier->text;

            /* Easy extension to allow access to struct members using dot instead of arrow. */
            if (!is_arrow)
            {
                if (current_val.type_info->kind == NCC_TYPE_KIND_POINTER)
                {
                    is_arrow = true;
                }
            }

            if (is_arrow)
            {
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
                   LLVMConstInt(ctx->ref_type.i32_type, field->storage_index, false)};

            LLVMValueRef member_ptr = LLVMBuildInBoundsGEP2(
                ctx->builder, struct_desc->llvm_type, current_val.value, indices, 2, "memberptr"
            );

            current_val = create_typed_value(member_ptr, field->type_desc, true);
            current_val.bitfield = field->bitfield;

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
    }

    if (lhs_res.value == NULL)
    {
        debug_error("Could not get pointer for LHS in assignment.");
        return lhs_res;
    }
    if (!lhs_res.is_lvalue)
    {
        debug_error("expected LHS of assignment to be an lvalue, but it isn't");
    }

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
        rhs_res = complete_binary_expression(ctx, lhs_res, node, BINARY_OP_COMPOUND_ASSIGNMENT);
    }
    else
    {
        // Process the RHS expression to get its LLVM ValueRef.
        rhs_res = process_expression(ctx, rhs_node);
    }
    if (rhs_res.value == NULL)
    {
        debug_error("Failed to process RHS expression in assignment.");
        return rhs_res;
    }
    rhs_res = ensure_rvalue(ctx, "assign_rhs_rval", rhs_res);

    // Generate the store instruction.
    // if (is_bitfield_assign && bitfield_storage_unit_type != NULL)
    struct_bitfield_data_t const bitfield = lhs_res.bitfield;
    if (bitfield.bit_width > 0)
    {
        unsigned bit_width = bitfield.bit_width;
        unsigned bit_offset = bitfield.bit_offset;

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
        // Check if it's a function name - return the function pointer
        struct function_decl_entry * decl_entry = find_function_declaration(ctx, node->text);

        if (decl_entry != NULL)
        {
            return decl_entry->func;
        }
        /* TODO: Else what? */
    }

    debug_error("NULL element type for variable '%s'.", node->text);
    ir_gen_error(&ctx->errors, node, "Unknown variable: '%s'", node->text);

    return NullTypedValue;
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

    TypeDescriptor const * bool_type = type_descriptor_get_bool_type(ctx->type_descriptors, false);
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

    if (!is_integer_kind(rhs_res.type_info) || LLVMGetIntTypeWidth(rhs_res.type_info->llvm_type) != 1)
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

static TypeDescriptor const *
find_wider_type(TypeDescriptor const * type_a, TypeDescriptor const * type_b)
{
    if (type_a == NULL || type_b == NULL)
    {
        return type_a != NULL ? type_a : type_b;
    }

    /* If one is float and the other isn't, prefer float */
    if (is_floating_kind(type_a) && !is_floating_kind(type_b))
    {
        return type_a;
    }
    if (is_floating_kind(type_b) && !is_floating_kind(type_a))
    {
        return type_b;
    }

    /* If both are floats, prefer the wider one */
    if (is_floating_kind(type_a) && is_floating_kind(type_b))
    {
        unsigned a_size = LLVMGetIntTypeWidth(type_a->llvm_type);
        unsigned b_size = LLVMGetIntTypeWidth(type_b->llvm_type);
        return a_size >= b_size ? type_a : type_b;
    }

    /* If both are integers or pointers, prefer wider bit width */
    if (is_integer_kind(type_a) && is_integer_kind(type_b))
    {
        unsigned a_width = LLVMGetIntTypeWidth(type_a->llvm_type);
        unsigned b_width = LLVMGetIntTypeWidth(type_b->llvm_type);
        if (a_width > b_width)
        {
            return type_a;
        }
        if (b_width > a_width)
        {
            return type_b;
        }
        /* Same width: prefer unsigned */
        if (type_a->specifiers.is_unsigned && !type_b->specifiers.is_unsigned)
        {
            return type_a;
        }
        return type_b;
    }

    /* Default to type_a for other combinations (pointer, struct, etc.) */
    return type_a;
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

    // Unify types: if the two branches produce different types, promote
    // the narrower one to the wider type (C's usual arithmetic conversions).
    // The casts must be inserted in the predecessor blocks before their
    // terminators, so the phi will see the correct types.
    TypeDescriptor const * common_type = true_res.type_info;
    if (true_res.value != NULL && false_res.value != NULL && true_res.type_info != false_res.type_info
        && true_res.type_info->llvm_type != false_res.type_info->llvm_type)
    {
        common_type = find_wider_type(true_res.type_info, false_res.type_info);

        /* Insert cast in true_block (before terminator) */
        if (true_res.type_info->llvm_type != common_type->llvm_type)
        {
            LLVMValueRef true_term = LLVMGetBasicBlockTerminator(true_block);
            LLVMPositionBuilderBefore(ctx->builder, true_term);
            TypedValue casted_true = cast_typed_value_to_desc(ctx, true_res, common_type);
            true_res = casted_true;
            LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
        }

        /* Insert cast in false_block (before terminator) */
        if (false_res.type_info->llvm_type != common_type->llvm_type)
        {
            LLVMValueRef false_term = LLVMGetBasicBlockTerminator(false_block);
            LLVMPositionBuilderBefore(ctx->builder, false_term);
            TypedValue casted_false = cast_typed_value_to_desc(ctx, false_res, common_type);
            false_res = casted_false;
            LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
        }
    }

    TypedValue res
        = create_typed_value(LLVMBuildPhi(ctx->builder, common_type->llvm_type, "cond_result"), common_type, false);

    // Add phi operands using the actual blocks where the expressions ended
    LLVMAddIncoming(res.value, &true_res.value, &true_block, 1);
    LLVMAddIncoming(res.value, &false_res.value, &false_block, 1);

    return res;
}

TypeDescriptor const *
get_type_descriptor_from_type_name(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_name)
{
    c_grammar_node_t const * qualifier_list = type_name->type_name.specifier_qualifier_list;
    c_grammar_node_t const * abstract_declarator = type_name->type_name.abstract_declarator;

    return resolve_type_descriptor(ctx, qualifier_list, abstract_declarator);
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
    case AST_NODE_VA_ARG_EXPRESSION:
    {
        return process_va_arg_expr(ctx, node);
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
        return process_binary_expression(ctx, node, BINARY_OP_BITWISE);
    }
    case AST_NODE_SHIFT_EXPRESSION:
    {
        return process_binary_expression(ctx, node, BINARY_OP_SHIFT);
    }
    case AST_NODE_ARITHMETIC_EXPRESSION:
    {
        return process_binary_expression(ctx, node, BINARY_OP_ARITHMETIC);
    }
    case AST_NODE_RELATIONAL_EXPRESSION:
    {
        return process_binary_expression(ctx, node, BINARY_OP_RELATIONAL);
    }
    case AST_NODE_EQUALITY_EXPRESSION:
    {
        return process_binary_expression(ctx, node, BINARY_OP_EQUALITY);
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

    case AST_NODE_STRING_LITERAL_PART:
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
    case AST_NODE_TYPE_QUALIFIERS:
    case AST_NODE_DECLARATION_SPECIFIERS:
    case AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER:
    case AST_NODE_TYPE_SPECIFIERS:
    case AST_NODE_PARAMETER_LIST:
    case AST_NODE_ELLIPSIS:
    case AST_NODE_PREPROCESSOR_LINE_MARKER:
    case AST_NODE_TYPEOF_SPECIFIER:
    case AST_NODE_OFFSETOF_MEMBER:
    default:
        debug_error("%s: Node type: %s is not supported at this level", __func__, get_node_type_name_from_node(node));
        break;
    }
    return NullTypedValue; // Return NULL if expression processing failed or not implemented.
}

TypedValue
process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (ctx->errors.fatal)
    {
        return NullTypedValue;
    }

    TypedValue result = _process_expression(ctx, node);

    if (result.value != NULL && result.type_info == NULL)
    {
        debug_error(
            "expression result has a value but no type descriptor after evaluating node: %s",
            get_node_type_name_from_node(node)
        );
        result = NullTypedValue;
    }
    else if (ctx->errors.fatal)
    {
        debug_error("fatal error encountered");
        result = NullTypedValue;
    }

    return result;
}
