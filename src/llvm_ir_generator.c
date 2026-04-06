#include "llvm_ir_generator.h"

#include "ast_node_name.h"
#include "ast_print.h"
#include "c_grammar_ast.h" // Assumes this header defines c_grammar_node_t and its node types
#include "debug.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declare the context structure as it's used before its definition
// typedef struct ir_generator_ctx ir_generator_ctx_t; // Assuming this is declared in a header or elsewhere

// Forward declarations for functions used before definition
// Helper to map C types to LLVM types
static LLVMTypeRef
map_type(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator);

static void process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);

static LLVMValueRef process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);
static LLVMTypeRef find_type_by_tag(ir_generator_ctx_t * ctx, char const * name);
static type_info_t const * register_tagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, char const * tag, type_kind_t kind
);
static type_info_t const * register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child);
static type_info_t const * add_tagged_struct_or_union_type(
    ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind, struct_field_t * fields, size_t num_fields
);
static int find_struct_field_index(ir_generator_ctx_t * ctx, LLVMTypeRef struct_type, char const * field_name);
static LLVMValueRef
cast_value_to_type(ir_generator_ctx_t * ctx, LLVMValueRef value, LLVMTypeRef target_type, bool zero_extend);
static c_grammar_node_t const * find_direct_declarator(c_grammar_node_t const * declarator);
static LLVMValueRef get_variable_pointer(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * identifier_node,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
);

static char const *
search_for_identifier_in_ast_node(c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NULL;
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

// Helper function to extract struct/union tag from a TypeSpecifier node
// Returns the tag if found (struct or union definition with identifier child), or NULL
// Only returns non-NULL when struct/union definition node is present and it contains an Identifier.
static char const *
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

    if (spec_child->type != AST_NODE_STRUCT_DEFINITION && spec_child->type != AST_NODE_UNION_DEFINITION
        && spec_child->type != AST_NODE_ENUM_DEFINITION && spec_child->type != AST_NODE_STRUCT_TYPE_REF
        && spec_child->type != AST_NODE_UNION_TYPE_REF && spec_child->type != AST_NODE_ENUM_TYPE_REF)

    {
        return NULL;
    }

    /* Look for identifier in the definition. */
    return search_for_identifier_in_ast_node(spec_child);
}

// Returns the name if found (plain identifier, i.e., typedef name), or NULL
// Only returns non-NULL when there is NO struct/union keyword
static char const *
extract_typedef_name(c_grammar_node_t const * type_spec_node)
{
    if (type_spec_node == NULL || type_spec_node->list.count == 0)
    {
        return NULL;
    }

    if (type_spec_node->type != AST_NODE_TYPE_SPECIFIER)
    {
        return NULL;
    }

    /* First check: if struct/union/enum definition or type reference is present, return NULL */
    for (size_t j = 0; j < type_spec_node->list.count; ++j)
    {
        c_grammar_node_t const * spec_child = type_spec_node->list.children[j];

        if (spec_child->type == AST_NODE_STRUCT_DEFINITION || spec_child->type == AST_NODE_UNION_DEFINITION
            || spec_child->type == AST_NODE_ENUM_DEFINITION || spec_child->type == AST_NODE_STRUCT_TYPE_REF
            || spec_child->type == AST_NODE_UNION_TYPE_REF || spec_child->type == AST_NODE_ENUM_TYPE_REF)
        {
            /* Found struct/union/enum definition or type reference -> not a typedef */
            return NULL;
        }
    }

    /* No keyword - check for plain identifier (typedef) */
    return search_for_identifier_in_ast_node(type_spec_node);
}

// Helper function to get natural alignment for a type
static unsigned
get_type_alignment(LLVMTypeRef type)
{
    if (!type)
        return 1;

    LLVMTypeKind kind = LLVMGetTypeKind(type);
    switch (kind)
    {
    case LLVMIntegerTypeKind:
    {
        unsigned bits = LLVMGetIntTypeWidth(type);
        if (bits <= 8)
            return 1;
        if (bits <= 16)
            return 2;
        if (bits <= 32)
            return 4;
        if (bits <= 64)
            return 8;
        return 16;
    }
    case LLVMFloatTypeKind:
        return 4;
    case LLVMDoubleTypeKind:
        return 8;
    case LLVMX86_FP80TypeKind:
        return 16;
    case LLVMPointerTypeKind:
        return 8;
    case LLVMStructTypeKind:
    {
        unsigned count = LLVMCountStructElementTypes(type);
        if (count == 0)
            return 1;
        unsigned max_align = 1;
        for (unsigned i = 0; i < count; i++)
        {
            LLVMTypeRef elem = LLVMStructGetTypeAtIndex(type, i);
            unsigned elem_align = get_type_alignment(elem);
            if (elem_align > max_align)
                max_align = elem_align;
        }
        return max_align;
    }
    case LLVMArrayTypeKind:
    {
        LLVMTypeRef elem = LLVMGetElementType(type);
        return get_type_alignment(elem);
    }
    case LLVMHalfTypeKind:
    case LLVMFP128TypeKind:
    case LLVMPPC_FP128TypeKind:
    case LLVMLabelTypeKind:
    case LLVMFunctionTypeKind:
    case LLVMVectorTypeKind:
    case LLVMMetadataTypeKind:
    case LLVMTokenTypeKind:
    case LLVMScalableVectorTypeKind:
    case LLVMBFloatTypeKind:
    case LLVMX86_AMXTypeKind:
    case LLVMTargetExtTypeKind:
    case LLVMVoidTypeKind:
    default:
        return 1;
    }
}

// Helper function to get size in bytes for a type
static LLVMValueRef
get_type_size(ir_generator_ctx_t * ctx, LLVMTypeRef type)
{
    if (!type)
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);

    LLVMTypeKind kind = LLVMGetTypeKind(type);
    switch (kind)
    {
    case LLVMIntegerTypeKind:
    {
        unsigned bits = LLVMGetIntTypeWidth(type);
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), bits / 8, false);
    }
    case LLVMFloatTypeKind:
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 4, false);
    case LLVMDoubleTypeKind:
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 8, false);
    case LLVMX86_FP80TypeKind:
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 16, false);
    case LLVMPointerTypeKind:
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 8, false);
    case LLVMStructTypeKind:
    {
        unsigned count = LLVMCountStructElementTypes(type);
        if (count == 0)
            return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);

        unsigned struct_size = 0;
        unsigned max_alignment = 1;

        for (unsigned i = 0; i < count; i++)
        {
            LLVMTypeRef elem = LLVMStructGetTypeAtIndex(type, i);
            unsigned elem_align = get_type_alignment(elem);

            if (elem_align > max_alignment)
                max_alignment = elem_align;

            struct_size = (struct_size + elem_align - 1) & ~(elem_align - 1);
            unsigned elem_size = LLVMGetIntTypeWidth(elem) / 8;
            if (LLVMGetTypeKind(elem) == LLVMFloatTypeKind)
                elem_size = 4;
            else if (LLVMGetTypeKind(elem) == LLVMDoubleTypeKind)
                elem_size = 8;
            else if (LLVMGetTypeKind(elem) == LLVMPointerTypeKind)
                elem_size = 8;
            else if (LLVMGetTypeKind(elem) == LLVMStructTypeKind)
            {
                elem_size = 0;
            }
            struct_size += elem_size;
        }

        struct_size = (struct_size + max_alignment - 1) & ~(max_alignment - 1);
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), struct_size, false);
    }
    case LLVMArrayTypeKind:
    {
        unsigned count = LLVMGetArrayLength(type);
        LLVMValueRef elem_size = get_type_size(ctx, LLVMGetElementType(type));
        return LLVMConstMul(elem_size, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), count, false));
    }
    case LLVMHalfTypeKind:
    case LLVMFP128TypeKind:
    case LLVMPPC_FP128TypeKind:
    case LLVMLabelTypeKind:
    case LLVMFunctionTypeKind:
    case LLVMVectorTypeKind:
    case LLVMMetadataTypeKind:
    case LLVMTokenTypeKind:
    case LLVMScalableVectorTypeKind:
    case LLVMBFloatTypeKind:
    case LLVMX86_AMXTypeKind:
    case LLVMTargetExtTypeKind:
    case LLVMVoidTypeKind:
    default:
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    }
}

// Helper wrapper for LLVMBuildStore with proper alignment
static LLVMValueRef
aligned_store(LLVMBuilderRef builder, LLVMValueRef value, LLVMValueRef ptr)
{
    LLVMValueRef store = LLVMBuildStore(builder, value, ptr);
    LLVMTypeRef value_type = LLVMTypeOf(value);
    unsigned alignment = get_type_alignment(value_type);
    LLVMSetAlignment(store, alignment);
    return store;
}

// Helper wrapper for LLVMBuildLoad2 with proper alignment
static LLVMValueRef
aligned_load(LLVMBuilderRef builder, LLVMTypeRef ty, LLVMValueRef ptr, char const * name)
{
    LLVMValueRef load = LLVMBuildLoad2(builder, ty, ptr, name);
    unsigned alignment = get_type_alignment(ty);
    LLVMSetAlignment(load, alignment);
    return load;
}

// Helper function to safely get element type from a pointer, handling opaque pointers
static LLVMTypeRef
get_pointer_element_type(ir_generator_ctx_t * ctx, LLVMTypeRef ptr_type)
{
    if (!ptr_type || LLVMGetTypeKind(ptr_type) != LLVMPointerTypeKind)
        return NULL;

    LLVMTypeRef elem_type = LLVMGetElementType(ptr_type);
    if (!elem_type)
        return LLVMInt8TypeInContext(ctx->context);

    uintptr_t elem_ptr = (uintptr_t)elem_type;
    if (elem_ptr < 0x1000 || elem_ptr > 0x7FFFFFFFFFFF)
        return LLVMInt8TypeInContext(ctx->context);

    LLVMTypeKind tk = LLVMGetTypeKind(elem_type);
    if (tk != LLVMIntegerTypeKind && tk != LLVMFloatTypeKind && tk != LLVMDoubleTypeKind && tk != LLVMArrayTypeKind
        && tk != LLVMStructTypeKind && tk != LLVMVectorTypeKind && tk != LLVMHalfTypeKind && tk != LLVMBFloatTypeKind)
        return LLVMInt8TypeInContext(ctx->context);

    return elem_type;
}

// Helper to process array subscript - extracts index and generates GEP
static LLVMValueRef
process_array_subscript(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * subscript_node, LLVMValueRef base_ptr, LLVMTypeRef base_type
)
{
    if (ctx == NULL || base_ptr == NULL || base_type == NULL || subscript_node == NULL)
    {
        return NULL;
    }

    // Extract index from first child of ArraySubscript node
    LLVMValueRef index_val = NULL;
    if (subscript_node->list.count >= 1)
    {
        c_grammar_node_t * index_node = subscript_node->list.children[0];
        index_val = process_expression(ctx, index_node);
    }

    if (index_val == NULL)
    {
        return NULL;
    }

    // Determine element type and build GEP based on whether base is pointer or array
    LLVMTypeRef elem_type = NULL;
    LLVMValueRef elem_ptr = NULL;

    if (LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
    {
        // For pointer: load it first, then use single index GEP
        elem_type = get_pointer_element_type(ctx, base_type);
        if (elem_type == NULL)
        {
            return NULL;
        }

        LLVMValueRef ptr_val = aligned_load(ctx->builder, base_type, base_ptr, "ptr_load");
        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, elem_type, ptr_val, &index_val, 1, "arrayidx");
    }
    else if (LLVMGetTypeKind(base_type) == LLVMArrayTypeKind)
    {
        // For array: use [0, index] GEP
        elem_type = LLVMGetElementType(base_type);
        if (elem_type == NULL)
        {
            return NULL;
        }

        LLVMValueRef indices[2];
        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        indices[1] = index_val;
        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, base_type, base_ptr, indices, 2, "arrayidx");
    }
    else
    {
        debug_error("Invalid type for array subscript.");
        return NULL;
    }

    return elem_ptr;
}

static LLVMValueRef
handle_bitfield_extraction(
    ir_generator_ctx_t * ctx, LLVMValueRef current_val, type_info_t const * info, size_t member_index
)
{
    if (info && info->fields && member_index < info->field_count)
    {
        struct_field_t const * field = &info->fields[member_index];
        if (field->bit_width > 0)
        {
            // Extract: (storage >> bit_offset) & mask
            LLVMValueRef bit_offset_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field->bit_offset, false);
            LLVMValueRef mask_val
                = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), (1ULL << field->bit_width) - 1, false);
            LLVMValueRef shifted = LLVMBuildLShr(ctx->builder, current_val, bit_offset_val, "bf_shift");
            current_val = LLVMBuildAnd(ctx->builder, shifted, mask_val, "bf_mask");
        }
    }

    return current_val;
}

// Helper to process all postfix expression suffixes (array subscript, member access, function call, postfix ops)
// Returns the final value, and optionally updates out_ptr/out_type for assignment targets
static LLVMValueRef
process_postfix_suffixes(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * postfix_node,
    LLVMValueRef base_ptr,
    LLVMTypeRef base_type,
    LLVMValueRef base_val,
    c_grammar_node_t const * base_node,
    LLVMValueRef * out_ptr,
    LLVMTypeRef * out_type
)
{
    if (ctx == NULL || postfix_node == NULL)
    {
        return NULL;
    }

    LLVMValueRef current_ptr = base_ptr;
    LLVMTypeRef current_type = base_type;
    LLVMValueRef current_val = base_val;

    for (size_t i = 0; i < postfix_node->list.count; ++i)
    {
        c_grammar_node_t * suffix = postfix_node->list.children[i];

        // Handle ARRAY_SUBSCRIPT
        if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
        {
            LLVMValueRef new_ptr = process_array_subscript(ctx, suffix, current_ptr, current_type);
            if (new_ptr)
            {
                current_ptr = new_ptr;
                if (current_type)
                {
                    if (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                        current_type = get_pointer_element_type(ctx, current_type);
                    else if (LLVMGetTypeKind(current_type) == LLVMArrayTypeKind)
                        current_type = LLVMGetElementType(current_type);
                }
                current_val = NULL;
            }
        }
        // Handle FUNCTION_CALL
        else if (suffix->type == AST_NODE_FUNCTION_CALL)
        {
            size_t num_args = 0;
            LLVMValueRef * args = NULL;

            if (suffix->list.count > 0)
            {
                num_args = suffix->list.count;
                args = malloc(num_args * sizeof(*args));
                for (size_t j = 0; j < num_args; ++j)
                {
                    args[j] = process_expression(ctx, suffix->list.children[j]);
                }
            }

            if (!current_val)
            {
                if (base_node && base_node->type == AST_NODE_IDENTIFIER && base_node->text != NULL)
                {
                    char const * func_name = base_node->text;
                    current_val = LLVMGetNamedFunction(ctx->module, func_name);
                    if (!current_val)
                    {
                        LLVMTypeRef ret_type = LLVMInt32TypeInContext(ctx->context);
                        // Use a varargs function with no required arguments to support
                        // functions being called with different numbers of arguments (like printf)
                        LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, true);
                        current_val = LLVMAddFunction(ctx->module, func_name, func_type);
                    }
                }
                else
                {
                    free(args);
                    continue;
                }

                LLVMTypeRef func_type = LLVMGlobalGetValueType(current_val);
                char const * call_name = "";
                if (LLVMGetReturnType(func_type) != LLVMVoidTypeInContext(ctx->context))
                {
                    call_name = "call_tmp";
                }

                LLVMValueRef * call_args = (num_args > 0) ? args : NULL;
                current_val
                    = LLVMBuildCall2(ctx->builder, func_type, current_val, call_args, (unsigned)num_args, call_name);

                if (LLVMGetReturnType(func_type) == LLVMVoidTypeInContext(ctx->context))
                {
                    current_val = NULL;
                }
            }

            free(args);
        }
        // Handle MEMBER_ACCESS_DOT / MEMBER_ACCESS_ARROW
        else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
        {
            char const * member_name = search_for_identifier_in_ast_node(suffix);
            if (member_name == NULL)
            {
                debug_error("Could not find member name in member access AST node.");
                continue;
            }
            if (current_val || current_ptr)
            {
                LLVMTypeRef struct_type = NULL;
                bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);

                if (is_arrow && base_node && base_node->type == AST_NODE_IDENTIFIER && base_node->text != NULL)
                {
                    char const * tag = find_symbol_tag_name(ctx, base_node->text);
                    if (tag != NULL)
                    {
                        struct_type = find_type_by_tag(ctx, tag);
                    }
                }

                if (!struct_type && current_type)
                {
                    if (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                        struct_type = get_pointer_element_type(ctx, current_type);
                    else
                        struct_type = current_type;
                }

                if (struct_type && LLVMGetTypeKind(struct_type) == LLVMStructTypeKind)
                {
                    unsigned num_elements = LLVMCountStructElementTypes(struct_type);
                    unsigned member_index = 0;
                    unsigned storage_index = 0;
                    type_info_t * info = NULL;

                    info = scope_find_type_by_llvm_type(ctx->current_scope, struct_type);

                    if (info)
                    {
                        for (unsigned j = 0; j < info->field_count; j++)
                        {
                            if (info->fields[j].name && strcmp(info->fields[j].name, member_name) == 0)
                            {
                                if (info->fields[j].storage_index >= num_elements)
                                {
                                    debug_warning(
                                        "Storage index for field '%s' exceeds number of struct "
                                        "elements.",
                                        member_name
                                    );
                                    return NULL;
                                }
                                member_index = j;
                                storage_index = info->fields[j].storage_index;
                                break;
                            }
                        }
                    }

                    LLVMValueRef indices[2];
                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), storage_index, false);

                    if (is_arrow || (current_type && LLVMGetTypeKind(current_type) == LLVMPointerTypeKind))
                    {
                        if (is_arrow && current_ptr)
                            current_ptr = aligned_load(ctx->builder, current_type, current_ptr, "arrow_ptr");
                        else if (current_val)
                            current_ptr = LLVMBuildInBoundsGEP2(
                                ctx->builder, current_type, current_val, indices, 2, "memberptr"
                            );
                        else if (current_ptr)
                            /* For assignment targets: compute GEP from current_ptr (which is already a pointer to
                             * struct) */
                            current_ptr = LLVMBuildInBoundsGEP2(
                                ctx->builder, struct_type, current_ptr, indices, 2, "memberptr"
                            );
                    }
                    else if (current_val)
                    {
                        LLVMValueRef struct_ptr = LLVMBuildAlloca(ctx->builder, struct_type, "struct_tmp");
                        aligned_store(ctx->builder, current_val, struct_ptr);
                        current_ptr
                            = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr");
                    }
                    else if (current_ptr)
                    {
                        /* For assignment targets when current_type is not a pointer: compute GEP directly */
                        current_ptr
                            = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, current_ptr, indices, 2, "memberptr");
                    }

                    if (current_ptr)
                    {
                        current_type = LLVMStructGetTypeAtIndex(struct_type, storage_index);
                        current_val = aligned_load(ctx->builder, current_type, current_ptr, "member");
                        current_val = handle_bitfield_extraction(ctx, current_val, info, member_index);
                    }
                }
            }
        }
        // Handle POSTFIX_OPERATOR (i++, i--)
        else if (suffix->type == AST_NODE_POSTFIX_OPERATOR)
        {
            if (current_ptr && current_type)
            {
                LLVMValueRef current_v = aligned_load(ctx->builder, current_type, current_ptr, "postfix_val");

                LLVMTypeKind kind = LLVMGetTypeKind(current_type);
                LLVMValueRef one;
                LLVMValueRef new_val;

                if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                {
                    one = LLVMConstReal(current_type, 1.0);
                    if (suffix->op.postfix.op == POSTFIX_OP_INC)
                        new_val = LLVMBuildFAdd(ctx->builder, current_v, one, "postfix_inc");
                    else
                        new_val = LLVMBuildFSub(ctx->builder, current_v, one, "postfix_dec");
                }
                else
                {
                    one = LLVMConstInt(current_type, 1, false);
                    if (suffix->op.postfix.op == POSTFIX_OP_INC)
                        new_val = LLVMBuildAdd(ctx->builder, current_v, one, "postfix_inc");
                    else
                        new_val = LLVMBuildSub(ctx->builder, current_v, one, "postfix_dec");
                }

                aligned_store(ctx->builder, new_val, current_ptr);
                current_val = current_v;
            }
        }
    }

    if (out_ptr)
        *out_ptr = current_ptr;
    if (out_type)
        *out_type = current_type;

    return current_val;
}

// Label management functions
static LLVMBasicBlockRef
get_or_create_label(ir_generator_ctx_t * ctx, char const * name)
{
    if (!ctx || !name)
        return NULL;

    for (size_t i = 0; i < ctx->label_count; i++)
    {
        if (ctx->labels[i].name && strcmp(ctx->labels[i].name, name) == 0)
        {
            return ctx->labels[i].block;
        }
    }

    if (ctx->label_count >= ctx->label_capacity)
    {
        size_t new_cap = ctx->label_capacity == 0 ? 16 : ctx->label_capacity * 2;
        label_t * new_labels = realloc(ctx->labels, new_cap * sizeof(label_t));
        if (!new_labels)
            return NULL;
        ctx->labels = new_labels;
        ctx->label_capacity = new_cap;
    }

    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(ctx->context, current_func, name);

    ctx->labels[ctx->label_count].name = strdup(name);
    ctx->labels[ctx->label_count].block = block;
    ctx->label_count++;

    return block;
}

static void
clear_labels(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;
    for (size_t i = 0; i < ctx->label_count; i++)
    {
        free(ctx->labels[i].name);
    }
    ctx->label_count = 0;
}

static void
process_initializer_list(
    ir_generator_ctx_t * ctx,
    LLVMValueRef base_ptr,
    LLVMTypeRef element_type,
    c_grammar_node_t const * initializer_node,
    int * outer_index
)
{
    if (initializer_node == NULL || base_ptr == NULL || element_type == NULL)
    {
        return;
    }

    LLVMTypeKind kind = LLVMGetTypeKind(element_type);

    // For structs, zero-initialize the entire struct first (including padding)
    // Then process explicit initializers
    if (kind == LLVMStructTypeKind)
    {
        LLVMTypeRef int8_type = LLVMInt8TypeInContext(ctx->context);
        LLVMValueRef size = LLVMSizeOf(element_type);
        LLVMValueRef zero = LLVMConstNull(int8_type);
        LLVMBuildMemSet(ctx->builder, base_ptr, zero, size, get_type_alignment(element_type));
    }

    // Use a local index for processing leaf elements at this level
    int local_index = 0;

    for (size_t i = 0; i < initializer_node->list.count; ++i)
    {
        c_grammar_node_t const * child = initializer_node->list.children[i];

        if (child->list.count == 0 && child->type != AST_NODE_INTEGER_LITERAL)
        {
            continue;
        }

        // Handle Designation nodes (designated initializers like .x = value or .pos.x = value)
        if (child->type == AST_NODE_DESIGNATION)
        {
            // Designation contains a list of field names (identifiers)
            // The next child in the list is the actual value
            if (i + 1 >= initializer_node->list.count)
            {
                continue;
            }

            c_grammar_node_t const * designation = child;
            c_grammar_node_t const * value_node = initializer_node->list.children[i + 1];

            // Handle nested designations (e.g., .pos.x = value has 2 identifiers: pos, x)
            LLVMValueRef current_ptr = base_ptr;
            LLVMTypeRef current_type = element_type;
            LLVMTypeKind current_kind = kind;
            int field_indices[16]; // Max nesting depth
            int field_count = 0;
            int final_local_index = 0;
            LLVMTypeRef final_type = element_type;

            if (designation->list.count > 0)
            {
                // Process each field in the designation path
                for (size_t d = 0; d < designation->list.count; d++)
                {
                    c_grammar_node_t const * field_ident = designation->list.children[d];
                    if (field_ident->type == AST_NODE_IDENTIFIER && field_ident->text != NULL)
                    {
                        char const * field_name = field_ident->text;

                        // For structs, find the field index by name
                        if (current_kind == LLVMStructTypeKind)
                        {
                            int field_idx = find_struct_field_index(ctx, current_type, field_name);
                            if (field_idx < 0)
                            {
                                // Field not found
                                break;
                            }
                            field_indices[field_count++] = field_idx;

                            // Get the type of this field
                            LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(current_type, (unsigned)field_idx);

                            // If there are more fields after this, navigate to the nested struct
                            // For the final field, only navigate if we're processing a nested InitializerList
                            if (d + 1 < designation->list.count)
                            {
                                // More fields coming - navigate to nested struct
                                LLVMValueRef indices[2];
                                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field_idx, false);
                                current_ptr = LLVMBuildInBoundsGEP2(
                                    ctx->builder, current_type, current_ptr, indices, 2, "nested_ptr"
                                );
                                current_type = field_type;
                                current_kind = LLVMGetTypeKind(current_type);
                            }
                            else
                            {
                                // This is the final field - store info for simple value case
                                final_local_index = field_idx;
                                final_type = field_type;
                            }
                        }
                    }
                }
            }

            // Process the value and store it at the designated position
            // Check if value is an InitializerList (nested initializer like .inner = {.x = 1, .y = 2})
            if (value_node->type == AST_NODE_INITIALIZER_LIST)
            {
                // Nested initializer - recursively process
                // For nested initializers, we need current_ptr to point to the final field
                // Navigate to the final field if not already done
                if (field_count > 0 && final_local_index >= 0)
                {
                    LLVMValueRef indices[2];
                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), final_local_index, false);
                    current_ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, element_type, current_ptr, indices, 2, "nested_init_field_ptr"
                    );
                    current_type = final_type;
                }

                if (field_count > 0 && current_ptr && current_type)
                {
                    process_initializer_list(ctx, current_ptr, current_type, value_node, NULL);
                }
            }
            else
            {
                // Simple value (not an InitializerList)
                LLVMValueRef value = process_expression(ctx, (c_grammar_node_t *)value_node);
                if (value && field_count > 0)
                {
                    LLVMValueRef elem_ptr;
                    if (field_count > 1)
                    {
                        // Nested field - current_ptr already points to the parent struct's field
                        // We need to get element 0 of that nested struct (since it's the final target)
                        LLVMValueRef indices[2];
                        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                        indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), final_local_index, false);
                        elem_ptr = LLVMBuildInBoundsGEP2(
                            ctx->builder, current_type, current_ptr, indices, 2, "nested_init_ptr"
                        );
                    }
                    else
                    {
                        // Single field
                        LLVMValueRef indices[2];
                        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                        indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), final_local_index, false);
                        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");
                    }

                    // Cast the value to the final field type if needed
                    value = cast_value_to_type(ctx, value, final_type, false);

                    aligned_store(ctx->builder, value, elem_ptr);
                }
            }

            // Skip the value node since we already processed it
            i++;
            local_index++;
            if (outer_index)
            {
                (*outer_index)++;
            }
            continue;
        }

        // If child is an INITIALIZER_LIST, create GEP to the row and recurse
        if (child->type == AST_NODE_INITIALIZER_LIST && kind == LLVMArrayTypeKind)
        {
            LLVMTypeRef nested_element = LLVMGetElementType(element_type);
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), local_index, false);
            LLVMValueRef row_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "row_ptr");
            process_initializer_list(ctx, row_ptr, nested_element, child, NULL);
            local_index++;
            if (outer_index)
                (*outer_index)++;
            continue;
        }

        // If child is an ASSIGNMENT node, extract the inner expression
        if (child->type == AST_NODE_ASSIGNMENT)
        {
            child = child->rhs;
        }

        // For array types, create a GEP to the element and recurse
        if (kind == LLVMArrayTypeKind && child->type != AST_NODE_INTEGER_LITERAL && child->list.count > 0)
        {
            LLVMTypeRef nested_element = LLVMGetElementType(element_type);
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), local_index, false);
            LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");
            process_initializer_list(ctx, elem_ptr, nested_element, child, &local_index);
        }
        // Process leaf values - store to array or struct member
        else
        {
            LLVMValueRef value = process_expression(ctx, (c_grammar_node_t *)child);
            if (value)
            {
                LLVMValueRef indices[2];
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), local_index, false);

                LLVMValueRef elem_ptr
                    = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");

                // For structs, cast the value to the member type
                if (kind == LLVMStructTypeKind)
                {
                    LLVMTypeRef member_type = LLVMStructGetTypeAtIndex(element_type, (unsigned)local_index);
                    if (member_type)
                    {
                        value = cast_value_to_type(ctx, value, member_type, false);
                    }
                }

                aligned_store(ctx->builder, value, elem_ptr);
                local_index++;
                if (outer_index)
                {
                    (*outer_index)++;
                }
            }
        }
    }
}

static bool
register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node)
{
    if (ctx == NULL || enum_node == NULL || enum_node->type != AST_NODE_ENUM_DEFINITION)
    {
        return false;
    }

    // EnumDefinition structure: [Identifier?, Enumerator, Enumerator, ...]
    // The enumerators contain the enum constant names and values

    // Check if first child is an Identifier (tag name) or Enumerator
    size_t start_idx;
    for (start_idx = 0; start_idx < enum_node->list.count; start_idx++)
    {
        c_grammar_node_t * child = enum_node->list.children[start_idx];
        if (child->type == AST_NODE_ENUMERATOR)
        {
            break;
        }
    }
    if (start_idx == enum_node->list.count)
    {
        return false;
    }

    // Enumerate values and register them as global constants
    int current_value = 0;

    for (size_t i = start_idx; i < enum_node->list.count; ++i)
    {
        c_grammar_node_t * child = enum_node->list.children[i];

        if (child->type == AST_NODE_ENUMERATOR && child->list.count >= 1)
        {
            // Enumerator = [Identifier] or [Identifier, Assign, IntegerLiteral]
            c_grammar_node_t * name_node = child->list.children[0];

            if (name_node && name_node->type == AST_NODE_IDENTIFIER && name_node->text != NULL)
            {
                char const * enum_name = name_node->text;

                // Check if there's an explicit value assignment
                // The enumerator could be [Identifier, Value] or [Identifier, Assign, Value]
                c_grammar_node_t * value_node = NULL;

                if (child->list.count == 2)
                {
                    // [Identifier, Value]
                    value_node = child->list.children[1];
                }
                else if (child->list.count >= 3)
                {
                    // [Identifier, Assign, Value]
                    value_node = child->list.children[2];
                }

                if (value_node)
                {
                    // Walk down the expression tree to find the integer literal
                    if (value_node->type == AST_NODE_INTEGER_LITERAL)
                    {
                        current_value = (int)value_node->integer_lit.integer_literal.value;
                    }
                    else if (value_node->lhs)
                    {
                        // Try lhs recursively for wrapped expressions
                        c_grammar_node_t * node = (c_grammar_node_t *)value_node;
                        while (node && node->lhs)
                        {
                            if (node->type == AST_NODE_INTEGER_LITERAL)
                            {
                                current_value = (int)node->integer_lit.integer_literal.value;
                                break;
                            }
                            node = (c_grammar_node_t *)node->lhs;
                        }
                    }
                }

                // Create a global constant for this enum value
                LLVMValueRef const_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), current_value, true);
                LLVMValueRef global = LLVMAddGlobal(ctx->module, LLVMInt32TypeInContext(ctx->context), enum_name);
                LLVMSetInitializer(global, const_val);
                LLVMSetGlobalConstant(global, true);
                LLVMSetLinkage(global, LLVMInternalLinkage);

                // Also add to symbol table for immediate lookup
                add_symbol(ctx, enum_name, global, LLVMInt32TypeInContext(ctx->context), NULL);

                current_value++;
            }
        }
    }

    return true;
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

    // Store the enum tag in tagged_types if present
    type_info_t enum_info = {0};

    enum_info.tag = strdup(tag);
    enum_info.kind = TYPE_KIND_ENUM;
    enum_info.type = LLVMInt32TypeInContext(ctx->context);
    enum_info.fields = NULL;
    enum_info.field_count = 0;

    return scope_add_tagged_type(ctx->current_scope, enum_info);
}

typedef struct
{
    size_t num_members;
    struct_field_t * members;
} struct_or_union_members_st;

static struct_or_union_members_st
extract_struct_or_union_members(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    struct_or_union_members_st object_members = {0};

    if (type_child == NULL
        || (type_child->type != AST_NODE_STRUCT_DEFINITION && type_child->type != AST_NODE_UNION_DEFINITION))
    {
        return object_members;
    }

    // Check if we have the new AST structure with StructDeclarationList
    // StructDefinition has: [Identifier, StructDeclarationList] (new)
    //                    or: [Keyword, Identifier?, TypeSpec, StructDeclarator, ...] (old)
    c_grammar_node_t * members_node = NULL;

    for (size_t i = 0; i < type_child->list.count; i++)
    {
        c_grammar_node_t * child = type_child->list.children[i];
        if (child != NULL && child->type == AST_NODE_STRUCT_DECLARATION_LIST)
        {
            members_node = child;
            break;
        }
    }
    if (members_node == NULL)
    {
        return object_members;
    }

    // StructDeclarationList contains StructDeclaration nodes
    size_t max_num_members = members_node->list.count;
    if (max_num_members == 0)
    {
        return object_members;
    }

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

        // StructDeclaration has: [KwExtension TypeSpecifier StructDeclarator]
        if (struct_decl->list.count < 3)
        {
            continue;
        }

        c_grammar_node_t const * specifier_qualifier_list = struct_decl->struct_declaration.specifier_qualifier_list;
        c_grammar_node_t const * declarator_list = struct_decl->struct_declaration.declarator_list;
        if (declarator_list == NULL || declarator_list->list.count == 0 || specifier_qualifier_list == NULL
            || specifier_qualifier_list->list.count == 0)
        {
            continue;
        }
        c_grammar_node_t const * type_spec = specifier_qualifier_list->list.children[0];
        c_grammar_node_t const * struct_decl_node = declarator_list->list.children[0];

        if (type_spec == NULL || struct_decl_node == NULL || type_spec->type != AST_NODE_TYPE_SPECIFIER)
        {
            continue;
        }

        struct_field_t new_member = {0};

        if (struct_decl_node->type == AST_NODE_STRUCT_DECLARATOR && struct_decl_node->list.count > 0)
        {
            c_grammar_node_t * decl = struct_decl_node->list.children[0];
            if (decl == NULL)
            {
                continue;
            }

            if (decl->type == AST_NODE_STRUCT_DECLARATOR_BITFIELD)
            {
                // Bitfield handling
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
                        c_grammar_node_t const * direct_decl = find_direct_declarator(bf_decl);
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
                    new_member.bit_width = (unsigned)width_node->integer_lit.integer_literal.value;
                }

                new_member.type = map_type(ctx, type_spec, NULL);
                if (new_member.type == NULL)
                {
                    free(new_member.name);
                    continue;
                }

                unsigned type_bits;
                struct_field_t * previous_member = NULL;
                if (num_members > 0)
                {
                    previous_member = &members[num_members - 1];
                    type_bits = LLVMGetIntTypeWidth(previous_member->type);
                }
                else
                {
                    type_bits = LLVMGetIntTypeWidth(new_member.type);
                }
                if (previous_member == NULL || (strlen(new_member.name) > 0 && new_member.bit_width == 0)
                    || (strlen(previous_member->name) == 0 && previous_member->bit_width == 0)
                    || LLVMGetTypeKind(new_member.type) != LLVMGetTypeKind(previous_member->type)
                    || new_member.bit_width + previous_member->bit_offset + previous_member->bit_width > type_bits)
                {
                    new_member.storage_index = (previous_member == NULL) ? 0 : (previous_member->storage_index + 1);
                }
                else
                {
                    new_member.storage_index = previous_member->storage_index;
                    new_member.bit_offset = previous_member->bit_offset + previous_member->bit_width;
                }
                members[num_members] = new_member;
                num_members++;
            }
            else if (decl->type == AST_NODE_DECLARATOR)
            {
                new_member.type = map_type(ctx, type_spec, decl);

                c_grammar_node_t const * direct_decl = find_direct_declarator(decl);
                if (direct_decl && direct_decl->list.count > 0)
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
                new_member.storage_index = (previous_member == NULL) ? 0 : (previous_member->storage_index + 1);
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
register_tagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, char const * tag, type_kind_t kind
)
{
    if (ctx == NULL || tag == NULL)
    {
        return NULL;
    }

    if (find_type_by_tag(ctx, tag) != NULL)
    {
        /* Already defined. Is this an error? */
        return NULL;
    }

    struct_or_union_members_st members = extract_struct_or_union_members(ctx, type_child);

    if (members.num_members > 0)
    {
        return add_tagged_struct_or_union_type(ctx, tag, kind, members.members, members.num_members);
    }

    free(members.members);

    return NULL;
}

char *
generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix)
{
    char * name = malloc(64);
    // Format: .anon.struct.0, .anon.struct.1, etc.
    sprintf(name, ".anon.%s.%d", prefix, ctx->anon_counter++);
    return name;
}

static type_info_t const *
add_untagged_struct_or_union_type(
    ir_generator_ctx_t * ctx, type_kind_t kind, struct_field_t * fields, size_t num_fields
)
{
    if (ctx == NULL || fields == NULL || num_fields == 0)
    {
        return NULL;
    }

    type_info_t new_struct = {0};

    new_struct.tag = generate_anon_name(ctx, (kind == TYPE_KIND_UNTAGGED_STRUCT) ? "struct" : "union");
    new_struct.kind = kind;
    new_struct.field_count = num_fields;
    new_struct.fields = fields;
    new_struct.type = LLVMStructCreateNamed(ctx->context, new_struct.tag);

    struct_field_t * last_field = &new_struct.fields[new_struct.field_count - 1];
    unsigned num_storage_units = last_field->storage_index + 1;
    LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
    int current_storage_unit = -1;
    for (size_t i = 0; i < new_struct.field_count; i++)
    {
        struct_field_t * field = &fields[i];
        if (field->storage_index != (unsigned)current_storage_unit)
        {
            current_storage_unit = field->storage_index;
            field_types[current_storage_unit] = field->type;
        }
    }

    LLVMStructSetBody(new_struct.type, field_types, (unsigned)num_storage_units, false);
    free(field_types);

    return scope_add_untagged_type(ctx->current_scope, new_struct);
}

static type_info_t const *
register_untagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, type_kind_t kind
)
{
    struct_or_union_members_st members = extract_struct_or_union_members(ctx, type_child);

    if (members.num_members > 0)
    {
        return add_untagged_struct_or_union_type(ctx, kind, members.members, members.num_members);
    }

    free(members.members);

    return NULL;
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

    // Store the enum tag in tagged_types if present
    type_info_t enum_info = {0};

    enum_info.kind = TYPE_KIND_UNTAGGED_ENUM;
    enum_info.type = LLVMInt32TypeInContext(ctx->context);
    enum_info.fields = NULL;
    enum_info.field_count = 0;

    return scope_add_untagged_type(ctx->current_scope, enum_info);
}

static char const *
search_ast_for_type_tag(c_grammar_node_t const * definition_node)

{
    if (definition_node == NULL
        || (definition_node->type != AST_NODE_ENUM_DEFINITION && definition_node->type != AST_NODE_STRUCT_DEFINITION
            && definition_node->type != AST_NODE_UNION_DEFINITION))
    {
        return NULL;
    }

    return search_for_identifier_in_ast_node(definition_node);
}

static type_info_t const *
register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    if (type_child == NULL
        || (type_child->type != AST_NODE_STRUCT_DEFINITION && type_child->type != AST_NODE_UNION_DEFINITION))
    {
        return NULL;
    }

    char const * struct_tag = search_ast_for_type_tag(type_child);
    if (struct_tag == NULL)
    {
        return NULL;
    }

    type_kind_t kind = (type_child->type == AST_NODE_STRUCT_DEFINITION) ? TYPE_KIND_STRUCT : TYPE_KIND_UNION;

    return register_tagged_struct_or_union_definition(ctx, type_child, struct_tag, kind);
}

static LLVMTypeRef
find_type_by_tag(ir_generator_ctx_t * ctx, char const * name)
{
    // Try to find as struct first, then union
    type_info_t * info = scope_find_tagged_struct(ctx->current_scope, name);
    if (info == NULL)
    {
        info = scope_find_tagged_union(ctx->current_scope, name);
    }
    return info ? info->type : NULL;
}

static LLVMTypeRef
find_typedef_type(ir_generator_ctx_t * ctx, char const * name)
{
    LLVMTypeRef result = scope_find_typedef(ctx->current_scope, name);
    return result;
}

static int
find_struct_field_index(ir_generator_ctx_t * ctx, LLVMTypeRef struct_type, char const * field_name)
{
    if (!struct_type || !field_name)
        return -1;

    type_info_t * info = scope_find_type_by_llvm_type(ctx->current_scope, struct_type);

    if (!info)
        return -1;

    for (size_t i = 0; i < info->field_count; ++i)
    {
        if (info->fields[i].name && strcmp(info->fields[i].name, field_name) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

static LLVMValueRef
cast_value_to_type(ir_generator_ctx_t * ctx, LLVMValueRef value, LLVMTypeRef target_type, bool zero_extend)
{
    if (!value || !target_type)
        return value;

    LLVMTypeRef value_type = LLVMTypeOf(value);
    LLVMTypeKind value_kind = LLVMGetTypeKind(value_type);
    LLVMTypeKind target_kind = LLVMGetTypeKind(target_type);

    // Only handle integer to integer conversions
    if (value_kind == LLVMIntegerTypeKind && target_kind == LLVMIntegerTypeKind)
    {
        unsigned value_bits = LLVMGetIntTypeWidth(value_type);
        unsigned target_bits = LLVMGetIntTypeWidth(target_type);

        if (target_bits < value_bits)
            return LLVMBuildTrunc(ctx->builder, value, target_type, "trunc_val");
        else if (target_bits > value_bits)
        {
            if (zero_extend)
            {
                return LLVMBuildZExt(ctx->builder, value, target_type, "zext_val");
            }
            return LLVMBuildSExt(ctx->builder, value, target_type, "sext_val");
        }
    }

    return value;
}

static type_info_t const *
add_tagged_struct_or_union_type(
    ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind, struct_field_t * fields, size_t num_fields
)
{
    if (ctx == NULL || tag == NULL || fields == NULL || num_fields == 0)
    {
        debug_error(
            "Invalid arguments to add_tagged_struct_or_union_type. ctx=%p, tag=%s, fields=%p, num_fields=%zu",
            (void *)ctx,
            tag ? tag : "NULL",
            (void *)fields,
            num_fields
        );
        return NULL;
    }

    if (scope_find_tagged_struct(ctx->current_scope, tag))
    {
        return NULL;
    }

    type_info_t new_struct = {0};

    new_struct.tag = strdup(tag);
    new_struct.kind = kind;
    new_struct.field_count = num_fields;
    new_struct.fields = fields;
    new_struct.type = LLVMStructCreateNamed(ctx->context, new_struct.tag);

    struct_field_t * last_field = &new_struct.fields[new_struct.field_count - 1];
    unsigned num_storage_units = last_field->storage_index + 1;
    LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
    int current_storage_unit = -1;
    for (size_t i = 0; i < new_struct.field_count; i++)
    {
        struct_field_t * field = &fields[i];
        if (field->storage_index != (unsigned)current_storage_unit)
        {
            current_storage_unit = field->storage_index;
            field_types[current_storage_unit] = field->type;
        }
    }
    LLVMStructSetBody(new_struct.type, field_types, num_storage_units, false);
    free(field_types);

    return scope_add_tagged_type(ctx->current_scope, new_struct);
}

static char *
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
find_direct_declarator(c_grammar_node_t const * declarator)
{
    if (!declarator || declarator->type != AST_NODE_DECLARATOR)
        return NULL;

    for (size_t i = 0; i < declarator->list.count; ++i)
    {
        if (declarator->list.children[i]->type == AST_NODE_DIRECT_DECLARATOR)
        {
            return declarator->list.children[i];
        }
    }
    return NULL;
}

static LLVMTypeRef
get_type_from_name(ir_generator_ctx_t * ctx, char const * type_name)
{
    LLVMTypeRef type_ref = NULL;

    LLVMTypeRef struct_type = find_type_by_tag(ctx, type_name);
    if (struct_type)
    {
        type_ref = struct_type;
    }
    // Then check for basic types
    else if (strncmp(type_name, "int", 3) == 0)
        type_ref = LLVMInt32TypeInContext(ctx->context);
    else if (strncmp(type_name, "char", 4) == 0)
        type_ref = LLVMInt8TypeInContext(ctx->context);
    else if (strncmp(type_name, "void", 4) == 0)
        type_ref = LLVMVoidTypeInContext(ctx->context);
    else if (strncmp(type_name, "float", 5) == 0)
        type_ref = LLVMFloatTypeInContext(ctx->context);
    else if (strstr(type_name, "long") != NULL && strstr(type_name, "double") != NULL)
        type_ref = LLVMX86FP80TypeInContext(ctx->context);
    else if (strncmp(type_name, "double", 6) == 0)
        type_ref = LLVMDoubleTypeInContext(ctx->context);
    else if (strncmp(type_name, "long", 4) == 0)
        type_ref = LLVMInt64TypeInContext(ctx->context);
    else if (strncmp(type_name, "short", 5) == 0)
        type_ref = LLVMInt16TypeInContext(ctx->context);
    else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
        type_ref = LLVMInt1TypeInContext(ctx->context);

    return type_ref;
}

/*
 * map_type()
 *
 * Converts C-style type information from AST nodes (specifiers and declarators)
 * into a corresponding LLVMTypeRef. This handles base types, pointers,
 * and arrays by recursively processing the AST nodes to build the full
 * LLVM type representation.
 *
 * Parameters:
 *   ctx: The IR generator context.
 *   specifiers: The AST node for declaration specifiers (e.g., int, const).
 *   declarator: The AST node for the declarator (e.g., *ptr[10]).
 *
 * Returns:
 *   An LLVMTypeRef representing the equivalent C type.
 */
static LLVMTypeRef
map_type(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator)
{
    LLVMTypeRef base_type = NULL;
    int pointer_level = 0;
    size_t array_depth = 0;
    size_t array_capacity = 4;
    size_t * array_sizes = malloc(array_capacity * sizeof(*array_sizes));

    if (array_sizes == NULL)
    {
        return LLVMInt32TypeInContext(ctx->context);
    }

    // 1. Process Specifiers (extract base type and any pointers in specifiers)
    if (specifiers)
    {
        // Handle terminal TypeSpecifier (e.g., typedef name "IntFloat", or basic type "int", "float")
        if (specifiers->type == AST_NODE_TYPE_SPECIFIER)
        {
            if (specifiers->text != NULL)
            {
                char const * type_name = specifiers->text;
                base_type = get_type_from_name(ctx, type_name);
            }
            else
            {
                c_grammar_node_t const * type_specifier_node = specifiers;

                // Handle terminal type specifiers (e.g., typedef names like "IntFloat")
                if (type_specifier_node->type == AST_NODE_IDENTIFIER && type_specifier_node->text != NULL)
                {
                    char const * type_name = type_specifier_node->text;
                    LLVMTypeRef struct_type = find_type_by_tag(ctx, type_name);
                    if (struct_type)
                    {
                        base_type = struct_type;
                    }
                }
                else
                {
                    for (size_t i = 0; i < type_specifier_node->list.count; ++i)
                    {
                        c_grammar_node_t * child = type_specifier_node->list.children[i];

                        if (child == NULL)
                        {
                            continue;
                        }
                        if (child->text != NULL
                            && (child->type == AST_NODE_IDENTIFIER || child->type == AST_NODE_INTEGER_BASE
                                || child->type == AST_NODE_FLOAT_BASE))
                        {
                            char const * type_name = child->text;
                            base_type = get_type_from_name(ctx, type_name);
                        }
                        else if (child->type == AST_NODE_STRUCT_DEFINITION || child->type == AST_NODE_UNION_DEFINITION)
                        {
                            type_info_t const * info = register_struct_definition(ctx, child);
                            if (info != NULL)
                            {
                                base_type = info->type;
                            }
                        }
                        else if (
                            child->type == AST_NODE_STRUCT_TYPE_REF || child->type == AST_NODE_UNION_TYPE_REF
                            || child->type == AST_NODE_ENUM_TYPE_REF
                        )
                        {
                            // Handle struct/union type reference: should have a child Identifier with the tag name
                            char const * tag = extract_struct_or_union_or_enum_tag(type_specifier_node);
                            if (tag != NULL)
                            {
                                LLVMTypeRef tagged_type = find_type_by_tag(ctx, tag);
                                if (tagged_type != NULL)
                                {
                                    base_type = tagged_type;
                                }
                            }
                        }
                    }
                }
            }
        }
        // Handle DeclarationSpecifiers - extract TypeSpecifier from inside
        else if (specifiers->type == AST_NODE_DECL_SPECIFIERS)
        {
            for (size_t i = 0; i < specifiers->list.count; ++i)
            {
                c_grammar_node_t * child = specifiers->list.children[i];
                if (child && child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    // Found TypeSpecifier, process it
                    if (child->text != NULL)
                    {
                        char const * type_name = child->text;

                        // First check for typedef
                        LLVMTypeRef typedef_type = find_typedef_type(ctx, type_name);
                        if (typedef_type)
                        {
                            base_type = typedef_type;
                        }
                        else
                        {
                            base_type = get_type_from_name(ctx, type_name);
                        }
                    }
                    else
                    {
                        // Use helper to determine if this is a struct/union reference or a typedef
                        // Check struct/union keyword first
                        char const * struct_name = extract_struct_or_union_or_enum_tag(child);
                        if (struct_name != NULL)
                        {
                            LLVMTypeRef struct_type = find_type_by_tag(ctx, struct_name);
                            if (struct_type != NULL)
                            {
                                base_type = struct_type;
                            }
                        }
                        else
                        {
                            // No struct/union keyword - check for typedef
                            char const * typedef_name = extract_typedef_name(child);
                            if (typedef_name != NULL)
                            {
                                LLVMTypeRef typedef_type = find_typedef_type(ctx, typedef_name);
                                if (typedef_type != NULL)
                                {
                                    base_type = typedef_type;
                                }
                            }
                        }
                    }
                    if (base_type != NULL)
                    {
                        break;
                    }
                }
            }
        }
    }

    // 2. Process Declarator (extract pointers and arrays)
    bool is_function_pointer = false;
    LLVMTypeRef func_ptr_param_types[16];
    size_t func_ptr_num_params = 0;

    if (declarator && declarator->type == AST_NODE_DECLARATOR)
    {

        for (size_t i = 0; i < declarator->list.count; ++i)
        {
            c_grammar_node_t * child = declarator->list.children[i];
            if (child->type == AST_NODE_POINTER)
            {
                pointer_level++;
            }
            else if (child->type == AST_NODE_DIRECT_DECLARATOR)
            {
                // Check inside DirectDeclarator for pointers and arrays
                // The structure can be: DirectDeclarator -> Declarator -> {Pointer, ..., DeclaratorSuffix}
                for (size_t j = 0; j < child->list.count; ++j)
                {
                    c_grammar_node_t * direct_child = child->list.children[j];
                    if (direct_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                    {
                        // Function pointer parameter: int (*func)(int, int)
                        is_function_pointer = true;
                        for (size_t k = 0; k < direct_child->list.count; ++k)
                        {
                            c_grammar_node_t * fp_child = direct_child->list.children[k];
                            if (fp_child->type == AST_NODE_POINTER)
                            {
                                pointer_level++;
                            }
                            else if (fp_child->type == AST_NODE_DECLARATOR_SUFFIX)
                            {
                                // Check for array size inside FunctionPointerDeclarator (e.g., (*ops[2]))
                                for (size_t m = 0; m < fp_child->list.count; ++m)
                                {
                                    c_grammar_node_t * suffix_child = fp_child->list.children[m];
                                    if (suffix_child->type == AST_NODE_INTEGER_LITERAL)
                                    {
                                        unsigned long long size_val = suffix_child->integer_lit.integer_literal.value;
                                        if (array_depth < array_capacity)
                                        {
                                            array_sizes[array_depth] = (size_t)size_val;
                                            array_depth++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else if (child->type == AST_NODE_DECLARATOR_SUFFIX)
            {
                // Check if this is a function suffix (contains DeclarationSpecifiers for params)
                // vs array suffix (contains IntegerLiteral for size)
                bool has_function_params = false;
                bool has_array_size = false;

                for (size_t j = 0; j < child->list.count; ++j)
                {
                    c_grammar_node_t * suffix_child = child->list.children[j];
                    if (suffix_child->type == AST_NODE_DECL_SPECIFIERS)
                    {
                        // This is a function parameter type
                        has_function_params = true;
                        if (func_ptr_num_params < 16)
                        {
                            func_ptr_param_types[func_ptr_num_params++] = map_type(ctx, suffix_child, NULL);
                        }
                    }
                    else if (suffix_child->type == AST_NODE_INTEGER_LITERAL)
                    {
                        // This is an array size
                        unsigned long long size_val = suffix_child->integer_lit.integer_literal.value;
                        if (array_depth < array_capacity)
                        {
                            array_sizes[array_depth] = (size_t)size_val;
                            array_depth++;
                            has_array_size = true;
                        }
                    }
                    else if (suffix_child->type == AST_NODE_DECLARATOR)
                    {
                        // Function parameter with declarator (e.g., int (*func)(int))
                        // For now, just extract the type specifier
                        has_function_params = true;
                        if (func_ptr_num_params < 16)
                        {
                            func_ptr_param_types[func_ptr_num_params++] = map_type(ctx, child, NULL);
                        }
                    }
                }

                // Empty brackets [] - mark as unsized (for arrays)
                if (!has_array_size && !has_function_params)
                {
                    if (array_depth < array_capacity)
                    {
                        array_sizes[array_depth] = 0;
                        array_depth++;
                    }
                }
            }
        }
    }

    if (!base_type)
    {
        base_type = LLVMInt32TypeInContext(ctx->context);
    }

    // Handle function pointer - return pointer type (possibly wrapped in array)
    if (is_function_pointer)
    {
        LLVMTypeRef func_ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);

        // If there are array sizes, this is an array of function pointers
        if (array_depth > 0)
        {
            for (int i = (int)array_depth - 1; i >= 0; --i)
            {
                func_ptr_type = LLVMArrayType(func_ptr_type, (unsigned)array_sizes[i]);
            }
        }

        free(array_sizes);
        return func_ptr_type;
    }

    // Build array types from innermost to outermost
    LLVMTypeRef final_type = base_type;
    for (int i = (int)array_depth - 1; i >= 0; --i)
    {
        final_type = LLVMArrayType(final_type, (unsigned)array_sizes[i]);
    }

    // Add pointer types
    for (int i = 0; i < pointer_level; ++i)
    {
        final_type = LLVMPointerType(final_type, 0);
    }

    free(array_sizes);

    return final_type;
}

/**
 * @brief Initializes the IR generator context.
 * Creates LLVM context, module, and builder.
 */
ir_generator_ctx_t *
ir_generator_init(void)
{
    ir_generator_ctx_t * ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        debug_error("Failed to allocate memory for context.");
        return NULL;
    }

    ctx->context = LLVMContextCreate();
    if (!ctx->context)
    {
        debug_error("Failed to create LLVM context.");
        free(ctx);
        return NULL;
    }

    ctx->module = LLVMModuleCreateWithName("c_compiler_module");
    if (!ctx->module)
    {
        debug_error("Failed to create LLVM module.");
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->builder = LLVMCreateBuilder();
    if (!ctx->builder)
    {
        debug_error("Failed to create LLVM builder.");
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    // Initialize with global scope
    ctx->current_scope = scope_create(NULL); // NULL parent = global scope
    if (!ctx->current_scope)
    {
        debug_error("Failed to create global scope.");
        LLVMDisposeBuilder(ctx->builder);
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    // Initialize label management
    ctx->label_capacity = 16;
    ctx->labels = calloc(ctx->label_capacity, sizeof(label_t));
    ctx->label_count = 0;

    // Initialize error collection (any error will be fatal since max_errors=1)
    ir_gen_error_collection_init(&ctx->errors, 10);

    // Initialize function declarations tracking
    ctx->function_declarations.entries = NULL;
    ctx->function_declarations.count = 0;
    ctx->function_declarations.capacity = 0;

    return ctx;
}

/**
 * @brief Frees the symbol table memory (all scopes in the chain).
 */
static void
free_symbol_table(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;

    // Free all scopes in the chain
    while (ctx->current_scope)
    {
        scope_pop(ctx);
    }
}

static void
free_labels(ir_generator_ctx_t * ctx)
{
    if (!ctx || !ctx->labels)
        return;

    for (size_t i = 0; i < ctx->label_count; i++)
    {
        free(ctx->labels[i].name);
    }
    free(ctx->labels);
    ctx->labels = NULL;
    ctx->label_count = 0;
    ctx->label_capacity = 0;
}

/**
 * @brief Disposes of the IR generator context and associated LLVM resources.
 */
void
ir_generator_dispose(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;

    free_symbol_table(ctx); // Free symbol table first (includes local types)
    free_labels(ctx);

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

    // fprintf(stderr, "%s node type: %s (%u)\n", __func__, get_node_type_name_from_node(node), node->type);

    switch (node->type)
    {
    case AST_NODE_TRANSLATION_UNIT:
    {
        if (node->translation_unit.external_declarations == NULL)
        {
            debug_error("Translation unit is missing external declarations.");
            return;
        }
        scope_push(ctx);
        process_ast_node(ctx, node->translation_unit.external_declarations);
        scope_pop(ctx);
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
    case AST_NODE_PREPROCESSOR_DIRECTIVE:
    {
        // For now, we can ignore preprocessor directives in IR generation
        break;
    }
    case AST_NODE_FUNCTION_DEFINITION:
    {
        // Check if we've already encountered a fatal error
        if (ctx->errors.fatal)
        {
            return;
        }

        clear_labels(ctx);

        // Create function scope for parameters and body
        scope_push(ctx);

        // --- Handle Function Definition ---
        c_grammar_node_t const * decl_specifiers_node = node->function_definition.declaration_specifiers;
        c_grammar_node_t const * declarator_node = node->function_definition.declarator;
        c_grammar_node_t const * compound_stmt_node = node->function_definition.body;

        if (decl_specifiers_node == NULL || declarator_node == NULL || compound_stmt_node == NULL)
        {
            ir_gen_error(&ctx->errors, "Function definition is missing declaration specifiers, declarator, or body.");
            scope_pop(ctx);
            return;
        }

        // --- Extract Function Name ---
        char * func_name = "unknown_function";
        c_grammar_node_t const * suffix_node = NULL;

        c_grammar_node_t const * direct_decl = find_direct_declarator(declarator_node);
        if (direct_decl && direct_decl->list.count > 0 && direct_decl->list.children[0]->type == AST_NODE_IDENTIFIER)
        {
            func_name = direct_decl->list.children[0]->text;
        }

        // Find parameter suffix
        for (size_t i = 0; i < declarator_node->list.count; ++i)
        {
            if (declarator_node->list.children[i]->type == AST_NODE_DECLARATOR_SUFFIX)
            {
                suffix_node = declarator_node->list.children[i];
                break;
            }
        }

        // --- Extract Parameters ---
        size_t num_params = 0;
        LLVMTypeRef * param_types = NULL;
        char ** param_names = NULL;
        LLVMTypeRef empty_params[1];

        if (suffix_node && suffix_node->list.count > 0)
        {
            // Each parameter typically has [KwExtension, TypeSpecifier, Declarator]
            num_params = suffix_node->list.count / 3;
            param_types = calloc(num_params, sizeof(LLVMTypeRef));
            param_names = calloc(num_params, sizeof(char *));

            for (size_t i = 0; i < num_params; ++i)
            {
                c_grammar_node_t const * p_spec = suffix_node->list.children[i * 3 + 1];
                c_grammar_node_t const * p_decl = suffix_node->list.children[i * 3 + 2];

                param_types[i] = map_type(ctx, p_spec, p_decl);

                c_grammar_node_t const * p_direct = find_direct_declarator(p_decl);
                if (p_direct && p_direct->list.count > 0)
                {
                    c_grammar_node_t const * first_child = p_direct->list.children[0];
                    if (first_child->type == AST_NODE_IDENTIFIER)
                    {
                        param_names[i] = first_child->text;
                    }
                    else if (first_child->type == AST_NODE_DECLARATOR)
                    {
                        // Nested declarator (e.g., for function pointers like *name)
                        // Find the DirectDeclarator inside and get the Identifier
                        c_grammar_node_t const * nested_direct = find_direct_declarator(first_child);
                        if (nested_direct && nested_direct->list.count > 0
                            && nested_direct->list.children[0]->type == AST_NODE_IDENTIFIER)
                        {
                            param_names[i] = nested_direct->list.children[0]->text;
                        }
                    }
                    else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                    {
                        // FunctionPointerDeclarator: contains Pointer, Identifier, DeclaratorSuffix*
                        char const * id = search_for_identifier_in_ast_node(first_child);
                        if (id != NULL)
                        {
                            param_names[i] = (char *)id;
                        }
                    }
                }
            }
        }

        LLVMTypeRef return_type = map_type(ctx, decl_specifiers_node, NULL);
        LLVMTypeRef func_type
            = LLVMFunctionType(return_type, num_params > 0 ? param_types : empty_params, (unsigned)num_params, false);

        // Check for function redeclaration or signature mismatch
        LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, func_name);
        if (existing != NULL)
        {
            LLVMTypeRef existing_type = LLVMGlobalGetValueType(existing);
            if (!function_signatures_match(existing_type, func_type))
            {
                ir_gen_error(&ctx->errors, "Function '%s' redeclared with different signature.", func_name);
                free(param_types);
                free(param_names);
                scope_pop(ctx);
                return;
            }

            struct function_decl_entry * decl = find_function_declaration(ctx, func_name);
            if (decl != NULL && decl->has_definition)
            {
                ir_gen_error(&ctx->errors, "Function '%s' already has a body.", func_name);
                free(param_types);
                free(param_names);
                scope_pop(ctx);
                return;
            }

            // Update existing declaration to mark it as having a definition
            if (decl != NULL)
            {
                decl->has_definition = true;
            }
        }
        else
        {
            // New function - add to tracking
            add_function_declaration(ctx, func_name, func_type, true);
        }

        LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);

        // Create a basic block for the function's entry point.
        LLVMBasicBlockRef entry_block = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry_block);

        // --- Handle function parameters: allocate space and store arguments ---
        for (size_t i = 0; i < num_params; ++i)
        {
            LLVMValueRef param_val = LLVMGetParam(func, (unsigned)i);
            LLVMValueRef alloca_inst
                = LLVMBuildAlloca(ctx->builder, param_types[i], param_names[i] ? param_names[i] : "");
            aligned_store(ctx->builder, param_val, alloca_inst);
            if (param_names[i])
            {
                // Extract struct/union name from parameter specifiers for pointer-to-compound types
                char const * param_compound_name = NULL;
                c_grammar_node_t * p_spec = suffix_node->list.children[i * 3 + 1];

                // p_spec is either TypeSpecifier directly or DeclarationSpecifiers containing TypeSpecifier
                c_grammar_node_t * type_spec = NULL;
                if (p_spec && p_spec->list.count > 0)
                {
                    if (p_spec->type == AST_NODE_TYPE_SPECIFIER)
                    {
                        type_spec = p_spec;
                    }
                    else if (p_spec->type == AST_NODE_DECL_SPECIFIERS)
                    {
                        // DeclarationSpecifiers has TypeSpecifier as child
                        type_spec = p_spec->list.children[0];
                    }
                }

                // Use helper to extract type name - check struct/union keyword first, then typedef
                if (type_spec)
                {
                    char const * tag = extract_struct_or_union_or_enum_tag(type_spec);
                    if (tag != NULL)
                    {
                        param_compound_name = (char *)tag;
                    }
                    else
                    {
                        // Try typedef name
                        char const * typedef_name = extract_typedef_name(type_spec);
                        if (typedef_name != NULL)
                        {
                            // Look up typedef to get the struct type for member access
                            LLVMTypeRef typedef_type = find_typedef_type(ctx, typedef_name);
                            if (typedef_type != NULL)
                            {
                                param_types[i] = typedef_type;
                                // Get underlying struct name for member access
                                type_info_t * info = scope_find_type_by_llvm_type(ctx->current_scope, typedef_type);
                                if (info != NULL)
                                {
                                    param_compound_name = (char *)info->tag;
                                }
                            }
                        }
                    }
                }

                add_symbol_with_struct(ctx, param_names[i], alloca_inst, param_types[i], NULL, param_compound_name);
            }
        }

        free(param_types);
        free(param_names);

        // Process the compound statement (function body).
        process_ast_node(ctx, compound_stmt_node);
        if (ctx->errors.fatal)
        {
            return;
        }

        // --- Add a default return if the function doesn't end with one ---
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            if (LLVMGetTypeKind(return_type) == LLVMVoidTypeKind)
            {
                LLVMBuildRetVoid(ctx->builder);
            }
            else
            {
                LLVMBuildRet(ctx->builder, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false));
            }
        }

        // Pop function scope
        scope_pop(ctx);

        break;
    }
    case AST_NODE_COMPOUND_STATEMENT:
    {
        // Create new scope for this block
        scope_push(ctx);

        for (size_t i = 0; i < node->list.count; ++i)
        {
            process_ast_node(ctx, node->list.children[i]);
        }

        // Pop block scope when exiting
        scope_pop(ctx);
        break;
    }
    case AST_NODE_EXPRESSION_STATEMENT:
    {
        c_grammar_node_t const * expr_node = node->expression_statement.expression;
        if (expr_node != NULL)
        {
            process_expression(ctx, expr_node);
        }
        break;
    }
    case AST_NODE_DECLARATION:
    {
        /* [ OptionalKwExtension DeclarationSpecifiers OptionalInitDeclaratorList ] */
        // --- Handle Variable Declarations ---

        // Register any struct/enum definitions in the declaration specifiers (in current scope)
        c_grammar_node_t const * decl_specifiers = node->declaration.declaration_specifiers;
        for (size_t i = 0; i < decl_specifiers->list.count; ++i)
        {
            c_grammar_node_t * spec_child = decl_specifiers->list.children[i];
            if (spec_child->type == AST_NODE_TYPE_SPECIFIER)
            {
                for (size_t j = 0; j < spec_child->list.count; ++j)
                {
                    c_grammar_node_t * type_child = spec_child->list.children[j];

                    if (type_child != NULL)
                    {
                        if ((type_child->type == AST_NODE_STRUCT_DEFINITION)
                            || (type_child->type == AST_NODE_UNION_DEFINITION))
                        {
                            register_struct_definition(ctx, type_child);
                        }
                        else if (type_child->type == AST_NODE_ENUM_DEFINITION)
                        {
                            char const * enum_tag = search_ast_for_type_tag(type_child);

                            register_tagged_enum_definition(ctx, type_child, enum_tag);
                        }
                    }
                }
            }
        }

        c_grammar_node_t const * init_decl_nodes = node->declaration.init_declarator_list;

        // Process InitDeclarators to create variables and initialize them.
        for (size_t i = 0; i < init_decl_nodes->list.count; ++i)
        {
            c_grammar_node_t const * init_decl_node = init_decl_nodes->list.children[i];

            char const * var_name = NULL;
            c_grammar_node_t const * initializer_expr_node = NULL; // Node representing the initializer expression.
            c_grammar_node_t const * declarator_node = init_decl_node->init_declarator.declarator;
            c_grammar_node_t const * direct_decl_node = find_direct_declarator(declarator_node);

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
                    c_grammar_node_t const * nested_direct = find_direct_declarator(first_child);
                    if (nested_direct != NULL)
                    {
                        char const * id = search_for_identifier_in_ast_node(nested_direct);
                        if (id != NULL)
                        {
                            var_name = id;
                        }
                    }
                }
                else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                {
                    // FunctionPointerDeclarator contains Pointer, Identifier, DeclaratorSuffix*
                    char const * id = search_for_identifier_in_ast_node(first_child);
                    if (id != NULL)
                    {
                        var_name = (char *)id;
                    }
                }
            }

            LLVMTypeRef var_type = map_type(ctx, decl_specifiers, declarator_node);

            c_grammar_node_t const * init_decl_initializer = init_decl_node->init_declarator.initializer;
            if (init_decl_initializer != NULL && init_decl_initializer->list.count > 0)
            {
                initializer_expr_node = init_decl_initializer->list.children[0];
            }

            if (var_name)
            {
                LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);

                if (current_block && var_type)
                {
                    // Inside a function - use stack allocation
                    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, var_type, var_name);

                    // Find struct name for pointer-to-struct types
                    char const * struct_name = NULL;
                    if (decl_specifiers)
                    {
                        /* Iterate through DeclarationSpecifiers children */
                        for (size_t si = 0; si < decl_specifiers->list.count && !struct_name; si++)
                        {
                            c_grammar_node_t * child = decl_specifiers->list.children[si];

                            /* Handle terminal TypeSpecifier (typedef name like "FloatMember") */
                            if (child && child->type == AST_NODE_TYPE_SPECIFIER && child->text != NULL)
                            {
                                /* First try struct list */
                                if (find_type_by_tag(ctx, child->text))
                                {
                                    struct_name = child->text;
                                }
                                else
                                {
                                    /* Try typedef - get underlying struct name */
                                    LLVMTypeRef typedef_type = find_typedef_type(ctx, child->text);
                                    if (typedef_type != NULL)
                                    {
                                        /* Need to find the struct name from the typedef entry */
                                        /* For now, set struct_name to the typedef name itself - */
                                        /* we'll need to find the actual underlying struct */
                                        /* This is a limitation - we'll fix by looking up the typedef entry directly */
                                        /* Actually, let's look up the struct by the type */
                                        type_info_t const * info
                                            = scope_find_type_by_llvm_type(ctx->current_scope, typedef_type);
                                        if (info != NULL)
                                        {
                                            struct_name = info->tag;
                                        }
                                    }
                                }
                            }
                            /* Handle non-terminal TypeSpecifier */
                            else if (child && child->type == AST_NODE_TYPE_SPECIFIER)
                            {
                                /* First try to extract struct/union/enum tag directly */
                                char const * name_from_struct = extract_struct_or_union_or_enum_tag(child);
                                if (name_from_struct != NULL)
                                {
                                    if (find_type_by_tag(ctx, name_from_struct))
                                    {
                                        struct_name = name_from_struct;
                                    }
                                }

                                /* If no struct tag found, check for typedef names */
                                if (struct_name == NULL)
                                {
                                    char const * typedef_name = extract_typedef_name(child);
                                    if (typedef_name != NULL)
                                    {
                                        LLVMTypeRef typedef_type = find_typedef_type(ctx, typedef_name);
                                        if (typedef_type != NULL)
                                        {
                                            type_info_t * info
                                                = scope_find_type_by_llvm_type(ctx->current_scope, typedef_type);
                                            if (info != NULL)
                                            {
                                                struct_name = info->tag;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Compute pointee_type for pointer variables
                    // We need to compute this BEFORE map_type adds pointer types, because
                    // LLVMGetElementType returns NULL/invalid for opaque pointers
                    LLVMTypeRef pointee_type = NULL;
                    if (decl_specifiers)
                    {
                        // Get the base type from specifiers
                        pointee_type = map_type(ctx, decl_specifiers, NULL);
                        // If there's a declarator with pointers, this is the pointee type
                        if (pointee_type && declarator_node)
                        {
                            // Check if there are pointers in the declarator
                            bool has_pointer = false;
                            for (size_t di = 0; di < declarator_node->list.count; di++)
                            {
                                c_grammar_node_t * dc = declarator_node->list.children[di];
                                if (dc && dc->type == AST_NODE_POINTER)
                                {
                                    has_pointer = true;
                                    break;
                                }
                            }
                            if (!has_pointer)
                            {
                                pointee_type = NULL;
                            }
                        }
                    }

                    add_symbol_with_struct(ctx, var_name, alloca_inst, var_type, pointee_type, struct_name);

                    // Process initializer if present
                    if (initializer_expr_node)
                    {
                        if ((LLVMGetTypeKind(var_type) == LLVMArrayTypeKind
                             || LLVMGetTypeKind(var_type) == LLVMStructTypeKind)
                            && initializer_expr_node->type == AST_NODE_INITIALIZER_LIST)
                        {
                            // Check if this is an array of pointers (like function pointers)
                            if (LLVMGetTypeKind(var_type) == LLVMArrayTypeKind)
                            {
                                LLVMTypeRef elem_type = LLVMGetElementType(var_type);
                                if (elem_type && LLVMGetTypeKind(elem_type) == LLVMPointerTypeKind)
                                {
                                    // Array of pointers - process each element individually
                                    int current_index = 0;
                                    for (size_t i = 0; i < initializer_expr_node->list.count; ++i)
                                    {
                                        c_grammar_node_t * child = initializer_expr_node->list.children[i];
                                        LLVMValueRef value = process_expression(ctx, child);
                                        if (value)
                                        {
                                            // Create GEP to element
                                            LLVMValueRef indices[2];
                                            indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                            indices[1] = LLVMConstInt(
                                                LLVMInt32TypeInContext(ctx->context), current_index, false
                                            );
                                            LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
                                                ctx->builder, var_type, alloca_inst, indices, 2, "init_elem_ptr"
                                            );
                                            aligned_store(ctx->builder, value, elem_ptr);
                                        }
                                        current_index++;
                                    }
                                }
                                else
                                {
                                    // Regular array or struct initializer
                                    int current_index = 0;
                                    process_initializer_list(
                                        ctx, alloca_inst, var_type, initializer_expr_node, &current_index
                                    );
                                }
                            }
                            else
                            {
                                int current_index = 0;
                                process_initializer_list(
                                    ctx, alloca_inst, var_type, initializer_expr_node, &current_index
                                );
                            }
                        }
                        else
                        {
                            LLVMValueRef initializer_value = process_expression(ctx, initializer_expr_node);
                            if (initializer_value)
                            {
                                LLVMTypeRef init_type = LLVMTypeOf(initializer_value);
                                if (LLVMGetTypeKind(var_type) == LLVMFloatTypeKind
                                    || LLVMGetTypeKind(var_type) == LLVMDoubleTypeKind)
                                {
                                    if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind)
                                    {
                                        initializer_value
                                            = LLVMBuildSIToFP(ctx->builder, initializer_value, var_type, "casttmp");
                                    }
                                    else if (
                                        LLVMGetTypeKind(init_type) == LLVMFloatTypeKind
                                        && LLVMGetTypeKind(var_type) == LLVMDoubleTypeKind
                                    )
                                    {
                                        initializer_value
                                            = LLVMBuildFPExt(ctx->builder, initializer_value, var_type, "casttmp");
                                    }
                                    else if (
                                        LLVMGetTypeKind(init_type) == LLVMDoubleTypeKind
                                        && LLVMGetTypeKind(var_type) == LLVMFloatTypeKind
                                    )
                                    {
                                        initializer_value
                                            = LLVMBuildFPTrunc(ctx->builder, initializer_value, var_type, "casttmp");
                                    }
                                }

                                // Handle integer type conversion (e.g., i32 literal to i8 char)
                                initializer_value = cast_value_to_type(ctx, initializer_value, var_type, false);

                                aligned_store(ctx->builder, initializer_value, alloca_inst);
                            }
                        }
                    }
                }
                else if (var_type)
                {
                    // Skip void types (e.g., extern void setbuf(...))
                    if (LLVMGetTypeKind(var_type) == LLVMVoidTypeKind)
                    {
                        continue;
                    }

                    // Check if this is a function declaration (declarator has params)
                    if (declarator_node)
                    {
                        bool is_function = false;
                        for (size_t si = 0; si < declarator_node->list.count; si++)
                        {
                            c_grammar_node_t * suf = declarator_node->list.children[si];
                            if (suf && suf->type == AST_NODE_DECLARATOR_SUFFIX && suf->list.count > 0)
                            {
                                is_function = true;
                                break;
                            }
                        }
                        if (is_function)
                        {
                            // Function declarations are auto-declared when called
                            continue;
                        }
                    }

                    // File-scope (global) variable
                    // Check if this is an unsized array with a string initializer
                    bool is_unsized_array
                        = (LLVMGetTypeKind(var_type) == LLVMArrayTypeKind && LLVMGetArrayLength(var_type) == 0);

                    if (is_unsized_array && initializer_expr_node
                        && initializer_expr_node->type == AST_NODE_STRING_LITERAL)
                    {
                        // Infer array size from string literal
                        char * raw_text = initializer_expr_node->text;
                        char * decoded = decode_string(raw_text);
                        char * str = decoded ? decoded : raw_text;

                        size_t str_len = strlen(str);
                        LLVMTypeRef elem_type = LLVMGetElementType(var_type);
                        var_type = LLVMArrayType(elem_type, (unsigned)(str_len + 1)); // +1 for null terminator

                        // Create the global variable with the correct type
                        LLVMValueRef global_var = LLVMAddGlobal(ctx->module, var_type, var_name);
                        LLVMSetLinkage(global_var, LLVMInternalLinkage);
                        LLVMSetGlobalConstant(global_var, true);
                        // Use str_len bytes + auto null terminator (false = DO add null)
                        LLVMSetInitializer(
                            global_var, LLVMConstStringInContext(ctx->context, str, (unsigned)str_len, false)
                        );
                        add_symbol(ctx, var_name, global_var, var_type, NULL);

                        free(decoded);
                    }
                    else
                    {
                        LLVMValueRef global_var = LLVMAddGlobal(ctx->module, var_type, var_name);
                        add_symbol(ctx, var_name, global_var, var_type, NULL);

                        // Process initializer for global variable
                        if (initializer_expr_node)
                        {
                            if (LLVMGetTypeKind(var_type) == LLVMArrayTypeKind
                                && initializer_expr_node->type == AST_NODE_INITIALIZER_LIST)
                            {
                                // For array initializers at file scope, we'd need to create a constant
                                // For now, just set the global with undef and process it differently
                                LLVMSetInitializer(global_var, LLVMGetUndef(var_type));
                            }
                            else
                            {
                                LLVMValueRef initializer_value = process_expression(ctx, initializer_expr_node);
                                if (initializer_value)
                                {
                                    LLVMSetInitializer(global_var, initializer_value);
                                }
                                else
                                {
                                    LLVMSetInitializer(global_var, LLVMGetUndef(var_type));
                                }
                            }
                        }
                        else
                        {
                            // No initializer - treat as external declaration
                            // Don't set an initializer, just mark as externally initialized
                            LLVMSetExternallyInitialized(global_var, true);
                        }
                    }
                }
            }
        }
        break;
    }
    case AST_NODE_TYPEDEF_DECLARATION:
    {
        /* Handle TypedefDeclaration node: [KwExtension DeclarationSpecifiers, Identifier] */
        /* DeclarationSpecifiers contains the struct/union/enum definition */
        /* Identifier is the typedef name */
        c_grammar_node_t const * decl_specs = node->typedef_declaration.declaration_specifiers;
        c_grammar_node_t const * init_declarator_list = node->typedef_declaration.init_declarator_list;
        c_grammar_node_t const * typedef_name_node = NULL;
        if (init_declarator_list->list.count > 0)
        {
            typedef_name_node = init_declarator_list->list.children[0];
        }

        if (decl_specs->type == AST_NODE_DECL_SPECIFIERS && typedef_name_node != NULL
            && typedef_name_node->type == AST_NODE_IDENTIFIER && typedef_name_node->text != NULL)
        {
            char * typedef_name = typedef_name_node->text;
            c_grammar_node_t const * struct_def_node = NULL;
            c_grammar_node_t const * enum_def_node = NULL;

            /* Look for struct/union/enum definition inside DeclarationSpecifiers */
            /* Could be:
                - struct Point { ... } (has StructDefinition)
                - struct Point; (just keyword + identifier, no body)
            */

            for (size_t i = 0; i < decl_specs->list.count; ++i)
            {
                c_grammar_node_t * spec_child = decl_specs->list.children[i];

                if (spec_child && spec_child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    char const * struct_tag = NULL;
                    type_kind_t kind = TYPE_KIND_UNKNOWN;
                    bool handled = false;

                    for (size_t j = 0; j < spec_child->list.count; ++j)
                    {
                        c_grammar_node_t const * type_child = spec_child->list.children[j];
                        kind = TYPE_KIND_UNKNOWN;
                        if (type_child->type == AST_NODE_STRUCT_TYPE_REF)
                        {
                            kind = TYPE_KIND_STRUCT;
                        }
                        else if (type_child->type == AST_NODE_UNION_TYPE_REF)
                        {
                            kind = TYPE_KIND_UNION;
                        }
                        if (kind != TYPE_KIND_UNKNOWN)
                        {
                            /* Check if there's an identifier (forward declaration) */
                            c_grammar_node_t const * name_node = NULL;
                            if (type_child->type == AST_NODE_STRUCT_TYPE_REF
                                || type_child->type == AST_NODE_UNION_TYPE_REF)
                            {
                                name_node = type_child->type_ref.identifier;
                            }

                            if (name_node && name_node->type == AST_NODE_IDENTIFIER)
                            {
                                /* This is a forward declaration: e.g. struct Point; */
                                struct_tag = name_node->text;
                                scope_add_typedef_forward_decl(ctx->current_scope, typedef_name, struct_tag, kind);
                                handled = true;
                                break;
                            }
                        }
                    }

                    if (handled)
                    {
                        continue;
                    }

                    /* Also check for StructDefinition (full definition) */
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

            if (struct_def_node)
            {
                /* We have a full definition */
                char const * struct_tag = search_ast_for_type_tag(struct_def_node);
                type_kind_t kind;

                /* Register the struct definition if we have a tag and definition */
                scope_typedef_entry_t typedef_entry = {0};
                typedef_entry.name = strdup(typedef_name);

                if (struct_tag != NULL)
                {
                    /* Tagged struct typedef - reference by tag name */
                    kind = struct_def_node->type == AST_NODE_STRUCT_DEFINITION ? TYPE_KIND_STRUCT : TYPE_KIND_UNION;
                    register_tagged_struct_or_union_definition(ctx, struct_def_node, struct_tag, kind);
                    typedef_entry.tag = strdup(struct_tag);
                }
                else
                {
                    kind = struct_def_node->type == AST_NODE_STRUCT_DEFINITION ? TYPE_KIND_UNTAGGED_STRUCT
                                                                               : TYPE_KIND_UNTAGGED_UNION;
                    register_untagged_struct_or_union_definition(ctx, struct_def_node, kind);
                    // Index of the newly added untagged type
                    typedef_entry.untagged_index = ctx->current_scope->untagged_types.count - 1;
                }
                typedef_entry.kind = kind;
                scope_add_typedef_entry(ctx->current_scope, typedef_entry);
            }
            else if (enum_def_node)
            {
                /* Register the enum values as constants */
                char const * enum_tag = search_ast_for_type_tag(enum_def_node);

                /* Also register the typedef */
                scope_typedef_entry_t typedef_entry = {0};
                typedef_entry.name = strdup(typedef_name);

                if (enum_tag != NULL)
                {
                    /* Tagged enum typedef */
                    typedef_entry.kind = TYPE_KIND_ENUM;
                    register_tagged_enum_definition(ctx, enum_def_node, enum_tag);
                    typedef_entry.tag = strdup(enum_tag);
                }
                else
                {
                    /* Untagged enum typedef - store the integer type directly */
                    typedef_entry.kind = TYPE_KIND_UNTAGGED_ENUM;
                    register_untagged_enum_definition(ctx, enum_def_node);
                    typedef_entry.type = LLVMInt32TypeInContext(ctx->context);
                    // Index of the newly added untagged type
                    typedef_entry.untagged_index = ctx->current_scope->untagged_types.count - 1;
                }

                scope_add_typedef_entry(ctx->current_scope, typedef_entry);
            }
        }
        break;
    }
    case AST_NODE_ASSIGNMENT:
    {
        // Handle assignment like 'variable = expression', 'arr[i] = expression', or 's.member = expression'
        c_grammar_node_t const * lhs_node = node->lhs;
        c_grammar_node_t const * rhs_node = node->rhs;

        LLVMValueRef lhs_ptr = NULL;
        LLVMTypeRef lhs_type = NULL;

        // Check if LHS is a PostfixExpression with suffixes (array subscript, member access)
        if (lhs_node->type == AST_NODE_POSTFIX_EXPRESSION)
        {
            c_grammar_node_t const * base_node = lhs_node->lhs;

            if (base_node->type == AST_NODE_IDENTIFIER)
            {
                char const * base_name = base_node->text;
                LLVMValueRef base_ptr;
                LLVMTypeRef base_type;
                if (find_symbol(ctx, base_name, &base_ptr, &base_type, NULL))
                {
                    c_grammar_node_t const * postfix_node = lhs_node->rhs;

                    // Use helper to process all suffixes (array subscript, member access)
                    process_postfix_suffixes(
                        ctx, postfix_node, base_ptr, base_type, NULL, base_node, &lhs_ptr, &lhs_type
                    );
                }
            }
        }
        else
        {
            // Simple variable assignment
            lhs_ptr = get_variable_pointer(ctx, lhs_node, &lhs_type, NULL);
        }

        if (!lhs_ptr)
        {
            debug_error("Could not get pointer for LHS in assignment.");
            return;
        }

        // Process the RHS expression to get its LLVM ValueRef.
        LLVMValueRef rhs_value = process_expression(ctx, rhs_node);
        if (!rhs_value)
        {
            debug_error("Failed to process RHS expression in assignment.");
            return;
        }

        // Convert RHS to LHS type if needed (e.g., i32 literal to i8 char)
        rhs_value = cast_value_to_type(ctx, rhs_value, lhs_type, false);

        // Generate the store instruction.
        aligned_store(ctx->builder, rhs_value, lhs_ptr);
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
        process_ast_node(ctx, init_node);
        if (ctx->errors.fatal)
        {
            return;
        }

        LLVMBuildBr(ctx->builder, cond_block);

        // 2. Emit Cond block
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        LLVMValueRef cond_val = process_expression(ctx, cond_node);
        if (ctx->errors.fatal)
        {
            return;
        }

        if (cond_val)
        {
            // Convert condition to bool (i1) if it's not already.
            LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
            if (cond_type != LLVMInt1TypeInContext(ctx->context))
            {
                LLVMValueRef zero = LLVMConstNull(cond_type);
                cond_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "for_cond_bool");
            }
            LLVMBuildCondBr(ctx->builder, cond_val, body_block, after_block);
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
        LLVMValueRef condition_val = process_expression(ctx, condition_node);
        if (!condition_val)
        {
            debug_error("Failed to process condition for WhileStatement.");
            return;
        }

        // Convert condition to bool (i1) if it's not already.
        LLVMTypeRef cond_type = LLVMTypeOf(condition_val);
        if (cond_type != LLVMInt1TypeInContext(ctx->context))
        {
            LLVMValueRef zero = LLVMConstNull(cond_type);
            condition_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, condition_val, zero, "cond_bool");
        }

        LLVMBuildCondBr(ctx->builder, condition_val, body_block, after_block);

        // --- Emit body block ---
        LLVMPositionBuilderAtEnd(ctx->builder, body_block);
        process_ast_node(ctx, body_node);
        if (ctx->errors.fatal)
        {
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
            return;
        }

        // Jump to condition
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, cond_block);
        }

        // --- Emit condition block ---
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        LLVMValueRef condition_val = process_expression(ctx, condition_node);
        if (!condition_val)
        {
            debug_error("Failed to process condition for DoWhileStatement.");
            return;
        }

        // Convert condition to bool (i1) if it's not already.
        LLVMTypeRef cond_type = LLVMTypeOf(condition_val);
        if (cond_type != LLVMInt1TypeInContext(ctx->context))
        {
            LLVMValueRef zero = LLVMConstNull(cond_type);
            condition_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, condition_val, zero, "do_cond_bool");
        }

        LLVMBuildCondBr(ctx->builder, condition_val, body_block, after_block);

        // Restore old break/continue targets
        ctx->break_target = old_break_target;
        ctx->continue_target = old_continue_target;

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

        LLVMValueRef switch_val = process_expression(ctx, switch_expr);
        if (switch_val == NULL)
        {
            debug_error("Failed to process switch expression.");
            return;
        }

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

        LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, switch_val, default_target, (unsigned)num_case_values);

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
                    LLVMValueRef case_val = process_expression(ctx, child->list.children[0]);
                    LLVMAddCase(switch_inst, case_val, items[i].body_block ? items[i].body_block : fallthrough_target);
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
        // AST structure for IfStatement: [ConditionExpression, ThenStatement, (Optional) ElseStatement]
        c_grammar_node_t const * condition_node = node->if_statement.condition;
        c_grammar_node_t const * then_node = node->if_statement.then_statement;
        c_grammar_node_t const * else_node = node->if_statement.else_statement;

        LLVMValueRef condition_val = process_expression(ctx, condition_node);
        if (condition_val == NULL)
        {
            debug_error("Failed to process condition for IfStatement.");
            return;
        }

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "then");
        LLVMBasicBlockRef else_block
            = else_node != NULL ? LLVMAppendBasicBlockInContext(ctx->context, current_func, "else") : NULL;
        LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "if_merge");

        if (else_node)
        {
            LLVMBuildCondBr(ctx->builder, condition_val, then_block, else_block);
        }
        else
        {
            LLVMBuildCondBr(ctx->builder, condition_val, then_block, merge_block);
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
        c_grammar_node_t const * expr_node = node->return_statement.expression;
        if (expr_node != NULL)
        {
            LLVMValueRef return_value = process_expression(ctx, expr_node);

            if (return_value)
            {
                LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
                LLVMTypeRef func_ret_type = LLVMGetReturnType(LLVMGlobalGetValueType(parent_func));

                return_value = cast_value_to_type(ctx, return_value, func_ret_type, true);

                LLVMBuildRet(ctx->builder, return_value);
            }
            else
            {
                debug_error("Failed to process return expression.");
            }
        }
        else
        {
            // Handle 'return;' (e.g., void function or default return).
            // Assuming 'int' return type, so return 0.
            LLVMBuildRet(ctx->builder, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false));
        }
        break;
    }
    case AST_NODE_GOTO_STATEMENT:
    {
        char const * label_name = node->goto_statement.label->text;
        LLVMBasicBlockRef target = get_or_create_label(ctx, label_name);
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
        LLVMBasicBlockRef label_block = get_or_create_label(ctx, label_name);

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

    case AST_NODE_FLOAT_BASE:
    case AST_NODE_INTEGER_LITERAL:
    case AST_NODE_FLOAT_LITERAL:
    case AST_NODE_STRING_LITERAL:
    case AST_NODE_LITERAL_SUFFIX:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_DECL_SPECIFIERS:
    case AST_NODE_TYPE_SPECIFIER:
    case AST_NODE_UNARY_OPERATOR:
    case AST_NODE_UNARY_EXPRESSION:
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
    case AST_NODE_FUNCTION_CALL:
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
    case AST_NODE_OPTIONAL_INIT_DECLARATOR_LIST:
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
    default:
        // Fallback: Recursively process children for unhandled node types.
        if (node->text != NULL && node->list.count == 0)
        {
            /*
                Do nothing for terminal nodes unless handled above.
                Shouldn't happen.
             */
            debug_warning("Unhandled terminal node type: %d (%s)", node->type, node->text);
        }
        else
        {
            for (size_t i = 0; i < node->list.count; ++i)
            {
                process_ast_node(ctx, node->list.children[i]);
                if (ctx->errors.fatal)
                {
                    return;
                }
            }
        }
        break;
    }
}

static void
process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (ctx->errors.fatal)
    {
        return; // Stop processing if a fatal error has occurred
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
    printf("IRGen: Successfully wrote LLVM IR to %s\n", file_path);
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

    printf(
        "IRGen: Successfully emitted %s to %s\n", (file_type == LLVMObjectFile) ? "object code" : "assembly", file_path
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
static LLVMValueRef
get_variable_pointer(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * identifier_node,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
)
{
    if (!identifier_node || identifier_node->type != AST_NODE_IDENTIFIER || !identifier_node->text)
    {
        debug_error("Invalid identifier node for get_variable_pointer.");
        return NULL;
    }
    char const * name = identifier_node->text;

    LLVMValueRef var_ptr;
    LLVMTypeRef retrieved_type;
    LLVMTypeRef pointee_type;

    if (find_symbol(ctx, name, &var_ptr, &retrieved_type, &pointee_type))
    {
        if (out_type)
            *out_type = retrieved_type;
        if (out_pointee_type)
            *out_pointee_type = pointee_type;
        return var_ptr;
    }
    else
    {
        return NULL;
    }
}

static LLVMValueRef
process_integer_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * _node)
{
    ast_node_integer_literal_t const * int_node = &_node->integer_lit;
    LLVMTypeRef int_type;
    if (int_node->integer_literal.is_long)
    {
        int_type = LLVMInt64TypeInContext(ctx->context);
    }
    else
    {
        int_type = LLVMInt32TypeInContext(ctx->context);
    }

    return LLVMConstInt(int_type, int_node->integer_literal.value, !int_node->integer_literal.is_unsigned);
}

static LLVMValueRef
process_float_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * _node)
{
    ast_node_float_literal_t const * float_node = &_node->float_lit;

    LLVMTypeRef float_type = NULL;
    long double value = float_node->float_literal.value;
    if (float_node->float_literal.type == FLOAT_LITERAL_TYPE_LONG_DOUBLE)
    {
        float_type = LLVMX86FP80TypeInContext(ctx->context);
    }
    else if (float_node->float_literal.type == FLOAT_LITERAL_TYPE_DOUBLE)
    {
        float_type = LLVMDoubleTypeInContext(ctx->context);
    }
    else if (float_node->float_literal.type == FLOAT_LITERAL_TYPE_FLOAT)
    {
        float_type = LLVMFloatTypeInContext(ctx->context);
    }

    return LLVMConstReal(float_type, value);
}

static LLVMValueRef
process_string_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Handle string literals like "Hello".
    if (node->text == NULL)
    {
        return NULL;
    }

    char * raw_text = node->text;
    char * decoded = decode_string(raw_text);
    LLVMValueRef global_str = LLVMBuildGlobalStringPtr(ctx->builder, decoded, "str_tmp");
    free(decoded);

    return global_str;
}

static LLVMValueRef
process_character_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node->text == NULL)
    {
        return NULL;
    }

    char * raw_text = node->text;
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
    return LLVMConstInt(LLVMInt8TypeInContext(ctx->context), value, false);
}

static LLVMValueRef
process_postfix_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // AST structure for PostfixExpression: [BaseExpression, SuffixPart1, SuffixPart2, ...]
    c_grammar_node_t const * base_node = node->lhs;
    c_grammar_node_t const * postfix_node = node->rhs;
    LLVMValueRef base_val = NULL;
    LLVMValueRef current_ptr = NULL;
    LLVMTypeRef current_type = NULL;
    bool have_ptr = false;
    bool base_is_array = false;

    // Check if base is a symbol (for array access)
    // Do this before process_expression to avoid double GEP for arrays
    if (base_node->type == AST_NODE_IDENTIFIER)
    {
        char const * var_name = base_node->text;
        LLVMValueRef var_ptr;
        LLVMTypeRef var_type;
        if (find_symbol(ctx, var_name, &var_ptr, &var_type, NULL))
        {
            current_ptr = var_ptr;
            current_type = var_type;
            have_ptr = true;

            // If base is an array type, we'll handle subscript in the loop
            // Don't call process_expression for the base to avoid double GEP
            if (LLVMGetTypeKind(var_type) == LLVMArrayTypeKind)
            {
                base_is_array = true;
            }
        }
    }

    // Only process base if it's not an array (arrays need suffix handling for subscript)
    if (!base_val && !base_is_array)
    {
        // For function calls, don't call process_expression on the identifier
        // The function call suffix handling will get the function pointer directly
        bool has_func_call_suffix = false;

        for (size_t i = 0; i < postfix_node->list.count; ++i)
        {
            if (postfix_node->list.children[i]->type == AST_NODE_FUNCTION_CALL)
            {
                has_func_call_suffix = true;
                break;
            }
        }
        if (!has_func_call_suffix)
        {
            base_val = process_expression(ctx, base_node);
        }
    }

    for (size_t i = 0; i < postfix_node->list.count; ++i)
    {
        c_grammar_node_t * suffix = postfix_node->list.children[i];
        if (suffix->type == AST_NODE_FUNCTION_CALL)
        {
            // Handle function call. Arguments might be children directly or in an ArgumentList
            size_t num_args = 0;
            LLVMValueRef * args = NULL;

            if (suffix->list.count > 0)
            {
                num_args = suffix->list.count;
                args = malloc(num_args * sizeof(*args));
                for (size_t j = 0; j < num_args; ++j)
                {
                    args[j] = process_expression(ctx, suffix->list.children[j]);
                }
            }

            if (!base_val)
            {
                // Check if we have a current_ptr from array subscript or other suffix
                // This handles cases like ops[0](...) where current_ptr points to the function pointer element
                if (have_ptr && current_ptr && current_type && LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                {
                    // Load the function pointer from the element pointer
                    base_val = aligned_load(ctx->builder, current_type, current_ptr, "func_ptr");
                }
                else if (base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * func_name = base_node->text;

                    // First check if it's a variable (function pointer) in the symbol table
                    LLVMValueRef var_ptr;
                    LLVMTypeRef var_type;
                    bool found = find_symbol(ctx, func_name, &var_ptr, &var_type, NULL);
                    if (found && var_ptr)
                    {
                        // It's a function pointer variable - load the pointer value
                        base_val = aligned_load(ctx->builder, var_type, var_ptr, "func_ptr");
                    }
                    else
                    {
                        // Not a variable, try to get as a named function
                        base_val = LLVMGetNamedFunction(ctx->module, func_name);
                        if (!base_val)
                        {
                            // For undeclared functions like printf, auto-declare as variadic returning i32
                            // with no required arguments to support different call patterns
                            LLVMTypeRef ret_type = LLVMInt32TypeInContext(ctx->context);
                            LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, true);
                            base_val = LLVMAddFunction(ctx->module, func_name, func_type);
                        }
                    }
                }
                else
                {
                    debug_error("Could not resolve function for call.");
                    free(args);
                    return NULL;
                }

                LLVMTypeRef func_type;

                // Check if this is a global function or an indirect call (function pointer)
                if (LLVMIsAGlobalValue(base_val))
                {
                    func_type = LLVMGlobalGetValueType(base_val);
                }
                else
                {
                    // Indirect call through function pointer - create function type from arguments
                    // Default to returning i32 and infer parameter types from arguments
                    LLVMTypeRef * param_types = NULL;
                    if (num_args > 0)
                    {
                        param_types = malloc(num_args * sizeof(LLVMTypeRef));
                        for (size_t j = 0; j < num_args; ++j)
                        {
                            param_types[j] = LLVMTypeOf(args[j]);
                        }
                    }
                    LLVMTypeRef ret_type = LLVMInt32TypeInContext(ctx->context);
                    func_type = LLVMFunctionType(ret_type, param_types, (unsigned)num_args, true);
                    if (param_types)
                        free(param_types);
                }

                char const * call_name = "";
                if (LLVMGetReturnType(func_type) != LLVMVoidTypeInContext(ctx->context))
                {
                    call_name = "call_tmp";
                }

                // For zero-argument calls, pass NULL for args (per LLVM C API docs)
                LLVMValueRef * call_args = (num_args > 0) ? args : NULL;
                base_val = LLVMBuildCall2(ctx->builder, func_type, base_val, call_args, (unsigned)num_args, call_name);

                // For void functions, set base_val to NULL (void calls don't produce values)
                if (LLVMGetReturnType(func_type) == LLVMVoidTypeInContext(ctx->context))
                {
                    base_val = NULL;
                }

                free(args);
            }
        }
        else if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
        {
            // Array subscript: use helper function
            if (have_ptr && current_type)
            {
                LLVMValueRef new_ptr = process_array_subscript(ctx, suffix, current_ptr, current_type);
                if (new_ptr)
                {
                    // Update current_ptr and current_type for next iteration
                    current_ptr = new_ptr;
                    // Update type for next subscript
                    if (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                        current_type = get_pointer_element_type(ctx, current_type);
                    else if (LLVMGetTypeKind(current_type) == LLVMArrayTypeKind)
                        current_type = LLVMGetElementType(current_type);

                    // Clear base_val so final load uses the correct element type
                    base_val = NULL;
                }
                else
                {
                    debug_error("Could not process array subscript.");
                    return NULL;
                }
            }
        }
        else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
        {
            // Struct member access: s.x or p->x
            // AST_MEMBER_ACCESS_DOT/ARROW children: [Dot/Arrow, Identifier]
            char const * member_name = search_for_identifier_in_ast_node(suffix);
            if (member_name == NULL)
            {
                ir_gen_error(&ctx->errors, "Could not find member name in member access AST node.");
                return NULL;
            }

            LLVMValueRef struct_val = base_val;
            LLVMTypeRef struct_type = NULL;
            bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);

            // Handle case where base_val is NULL but we have current_ptr from array subscript
            if (!struct_val && have_ptr && current_ptr && current_type)
            {
                // current_ptr points to the element, current_type is its type
                LLVMTypeKind type_kind = LLVMGetTypeKind(current_type);
                if (type_kind == LLVMStructTypeKind)
                {
                    struct_val = current_ptr;
                    struct_type = current_type;
                }
                else if (type_kind == LLVMPointerTypeKind)
                {
                    LLVMTypeRef elem_type = LLVMGetElementType(current_type);
                    if (elem_type && LLVMGetTypeKind(elem_type) == LLVMStructTypeKind)
                    {
                        struct_val = current_ptr;
                        struct_type = elem_type;
                    }
                }
                else
                {
                    debug_error("Member access on unsupported type kind %d.", type_kind);
                }
            }

            if (struct_val)
            {
                // Get the struct type
                LLVMTypeRef base_type;
                if (struct_type)
                {
                    base_type = struct_type;
                }
                else
                {
                    base_type = LLVMTypeOf(struct_val);
                }
                if (!base_type)
                {
                    debug_error("NULL type for member access base.");
                    continue;
                }
                if (!struct_type)
                {
                    struct_type = base_type;
                }

                // For LLVM 18+ opaque pointers, use struct name from symbol table
                if (is_arrow && base_node && base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * tag = find_symbol_tag_name(ctx, base_node->text);
                    debug_info("Looking up struct type by tag '%s' for opaque pointer.", tag ? tag : "NULL");
                    if (tag != NULL)
                    {
                        struct_type = find_type_by_tag(ctx, tag);
                    }
                }
                // Fallback for older LLVM / dot access
                if (!struct_type)
                {
                    if (LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
                        struct_type = LLVMGetElementType(base_type);
                    else
                        struct_type = base_type;
                }

                if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind)
                {
                    debug_error("Could not find struct type for member access.");
                    continue;
                }

                // Find the member index by name
                unsigned num_elements = LLVMCountStructElementTypes(struct_type);
                unsigned member_index = 0;
                unsigned storage_index = 0;
                bool found = false;

                // Look up the struct info to find the member by name
                type_info_t const * struct_info = scope_find_type_by_llvm_type(ctx->current_scope, struct_type);
                debug_info(
                    "Looking up struct info for member access. Struct type: %p, found info: %s",
                    (void *)struct_type,
                    struct_info ? "yes" : "no"
                );
                if (struct_info && struct_info->fields)
                {
                    for (unsigned j = 0; j < struct_info->field_count; ++j)
                    {
                        if (struct_info->fields[j].name && strcmp(struct_info->fields[j].name, member_name) == 0
                            && struct_info->fields[j].storage_index < num_elements)
                        {
                            member_index = j;
                            storage_index = struct_info->fields[j].storage_index;
                            found = true;
                            break;
                        }
                    }
                }
                else
                {
                    /* Just use index 0 for member index and storage index. */
                    found = true;
                }
                debug_info(
                    "Member '%s' access - found: %s, member_index: %u, storage_index: %u, num_elements: %u",
                    member_name,
                    found ? "yes" : "no",
                    member_index,
                    storage_index,
                    num_elements
                );
                if (found || num_elements > 0)
                {
                    // Create GEP to access member
                    LLVMValueRef indices[2];
                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), storage_index, false);

                    LLVMValueRef member_ptr;
                    // Check if struct_val (which could be base_val or current_ptr) is a pointer
                    LLVMTypeRef struct_val_type = NULL;
                    if (struct_val)
                    {
                        struct_val_type = LLVMTypeOf(struct_val);
                    }
                    bool struct_val_is_ptr = struct_val_type && LLVMGetTypeKind(struct_val_type) == LLVMPointerTypeKind;

                    if (is_arrow || struct_val_is_ptr)
                    {
                        // struct_val is a pointer to the struct
                        // Use struct_type for the GEP, not the pointer type
                        member_ptr
                            = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, struct_val, indices, 2, "memberptr");
                    }
                    else
                    {
                        // For value types (struct passed by value), we need to get the pointer
                        LLVMValueRef struct_ptr = LLVMBuildAlloca(ctx->builder, struct_type, "struct_tmp");
                        aligned_store(ctx->builder, struct_val, struct_ptr);
                        member_ptr
                            = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr");
                    }
                    LLVMTypeRef member_type = LLVMStructGetTypeAtIndex(struct_type, storage_index);
                    base_val = aligned_load(ctx->builder, member_type, member_ptr, "member");
                    base_val = handle_bitfield_extraction(ctx, base_val, struct_info, member_index);
                }
            }
        }
        else if (suffix->type == AST_NODE_POSTFIX_OPERATOR)
        {
            // Handle postfix increment/decrement: i++ or i--
            if (have_ptr && current_ptr && current_type)
            {
                // Load current value
                LLVMValueRef current_val = aligned_load(ctx->builder, current_type, current_ptr, "postfix_val");

                // Create increment/decrement value
                LLVMTypeKind kind = LLVMGetTypeKind(current_type);
                LLVMValueRef one;
                LLVMValueRef new_val;

                if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                {
                    one = LLVMConstReal(current_type, 1.0);
                    if (suffix->op.postfix.op == POSTFIX_OP_INC)
                        new_val = LLVMBuildFAdd(ctx->builder, current_val, one, "postfix_inc");
                    else
                        new_val = LLVMBuildFSub(ctx->builder, current_val, one, "postfix_dec");
                }
                else
                {
                    one = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
                    if (suffix->op.postfix.op == POSTFIX_OP_INC)
                        new_val = LLVMBuildAdd(ctx->builder, current_val, one, "postfix_inc");
                    else
                        new_val = LLVMBuildSub(ctx->builder, current_val, one, "postfix_dec");
                }

                // Store the new value
                aligned_store(ctx->builder, new_val, current_ptr);

                // Postfix returns the original value (current_val)
                base_val = current_val;
            }
        }
        else
        {
            debug_warning("Unhandled postfix suffix type %u", suffix->type);
        }
    }

    // If base_val is NULL but we have a pointer (from array subscript or member access),
    // load the value from the pointer
    if (!base_val && have_ptr && current_ptr && current_type)
    {
        base_val = aligned_load(ctx->builder, current_type, current_ptr, "load_tmp");
    }

    return base_val;
}

static LLVMValueRef
process_cast_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // AST structure for CastExpression: [TypeName, CastExpression]
    if (node->list.count < 2)
    {
        return NULL;
    }

    c_grammar_node_t const * type_name_node = node->list.children[0];
    c_grammar_node_t const * inner_expr_node = node->list.children[1];

    if (type_name_node->type != AST_NODE_TYPE_NAME)
    {
        debug_error("Expected TypeName in cast expression, got %u", type_name_node->type);
        return NULL;
    }

    // TypeName children are [SpecifierQualifierList, AbstractDeclarator?]
    c_grammar_node_t * spec_qual = type_name_node->list.children[0];
    c_grammar_node_t * abstract_decl = (type_name_node->list.count > 1) ? type_name_node->list.children[1] : NULL;

    LLVMTypeRef target_type = map_type(ctx, spec_qual, abstract_decl);
    LLVMValueRef val_to_cast = process_expression(ctx, inner_expr_node);

    if (val_to_cast && target_type)
    {
        LLVMTypeRef src_type = LLVMTypeOf(val_to_cast);
        if (LLVMGetTypeKind(target_type) == LLVMIntegerTypeKind
            && (LLVMGetTypeKind(src_type) == LLVMFloatTypeKind || LLVMGetTypeKind(src_type) == LLVMDoubleTypeKind))
        {
            return LLVMBuildFPToSI(ctx->builder, val_to_cast, target_type, "casttmp");
        }
        else if (
            (LLVMGetTypeKind(target_type) == LLVMFloatTypeKind || LLVMGetTypeKind(target_type) == LLVMDoubleTypeKind)
            && LLVMGetTypeKind(src_type) == LLVMIntegerTypeKind
        )
        {
            return LLVMBuildSIToFP(ctx->builder, val_to_cast, target_type, "casttmp");
        }
        // Add more cast types as needed (bitcast, pointer casts, etc.)
        return val_to_cast; // Fallback
    }
    return NULL;
}

static LLVMValueRef
process_assignment(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    c_grammar_node_t const * lhs_node = node->lhs;
    c_grammar_node_t const * rhs_node = node->rhs;

    LLVMValueRef lhs_ptr = NULL;
    LLVMTypeRef lhs_type = NULL;

    // Track bitfield assignment info
    bool is_bitfield_assign = false;
    unsigned bitfield_storage_idx = 0;
    unsigned bitfield_bit_offset = 0;
    unsigned bitfield_bit_width = 0;
    LLVMValueRef bitfield_struct_ptr = NULL;
    LLVMTypeRef bitfield_struct_type = NULL;

    // Check if LHS is a PostfixExpression with array subscript or member access
    if (lhs_node->type == AST_NODE_POSTFIX_EXPRESSION)
    {
        c_grammar_node_t const * base_node = lhs_node->lhs;
        if (base_node->type == AST_NODE_IDENTIFIER)
        {
            char const * base_name = base_node->text;
            LLVMValueRef base_ptr;
            LLVMTypeRef base_type;
            if (find_symbol(ctx, base_name, &base_ptr, &base_type, NULL))
            {
                LLVMValueRef current_ptr = base_ptr;
                LLVMTypeRef current_type = base_type;
                c_grammar_node_t const * postfix_node = lhs_node->rhs;

                // Process suffixes to handle array subscripts and member access
                for (size_t i = 0; i < postfix_node->list.count; ++i)
                {
                    c_grammar_node_t * suffix = postfix_node->list.children[i];

                    if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
                    {
                        LLVMValueRef new_ptr = process_array_subscript(ctx, suffix, current_ptr, current_type);
                        if (new_ptr)
                        {
                            current_ptr = new_ptr;
                            // Update type for next subscript
                            if (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                                current_type = get_pointer_element_type(ctx, current_type);
                            else if (LLVMGetTypeKind(current_type) == LLVMArrayTypeKind)
                                current_type = LLVMGetElementType(current_type);
                        }
                    }
                    else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
                    {
                        /* The one and only child is an IDENTIFIER node. */
                        c_grammar_node_t * member_node = suffix->list.children[0];
                        char * member_name = member_node->text;

                        if (current_ptr && current_type)
                        {
                            LLVMTypeRef struct_type = NULL;

                            // For nested member access, if current_type is already a struct type, use it directly
                            // This handles cases like o.inner.x where after accessing 'inner', current_type is %Inner
                            if (LLVMGetTypeKind(current_type) == LLVMStructTypeKind)
                            {
                                struct_type = current_type;
                            }
                            // For LLVM 18+ opaque pointers, use struct name from symbol table
                            else if (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                            {
                                char const * tag = find_symbol_tag_name(ctx, base_name);
                                if (tag != NULL)
                                {
                                    struct_type = find_type_by_tag(ctx, tag);
                                }
                                // Fallback: use pointer element type
                                if (struct_type == NULL)
                                {
                                    struct_type = get_pointer_element_type(ctx, current_type);
                                }
                            }
                            // Fallback: try to find struct info by LLVM type directly (for untagged struct typedefs)
                            if (struct_type == NULL)
                            {
                                debug_info("No struct type found from pointer element, trying direct type lookup.");
                                LLVMTypeRef type_to_search = (LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                                                                 ? get_pointer_element_type(ctx, current_type)
                                                                 : current_type;
                                if (type_to_search && LLVMGetTypeKind(type_to_search) == LLVMStructTypeKind)
                                {
                                    type_info_t * info
                                        = scope_find_type_by_llvm_type(ctx->current_scope, type_to_search);
                                    if (info != NULL)
                                    {
                                        struct_type = type_to_search;
                                    }
                                }
                            }

                            if (struct_type && LLVMGetTypeKind(struct_type) == LLVMStructTypeKind)
                            {
                                // For arrow access, load the pointer first
                                LLVMValueRef struct_ptr = current_ptr;
                                bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);
                                if (is_arrow)
                                {
                                    struct_ptr = aligned_load(ctx->builder, current_type, current_ptr, "arrow_ptr");
                                }

                                unsigned num_elements = LLVMCountStructElementTypes(struct_type);
                                unsigned member_index = 0;
                                unsigned storage_index = 0;
                                type_info_t * info = NULL;

                                info = scope_find_type_by_llvm_type(ctx->current_scope, struct_type);

                                if (info != NULL)
                                {
                                    for (unsigned j = 0; j < info->field_count; j++)
                                    {
                                        if (info->fields[j].name != NULL
                                            && strcmp(info->fields[j].name, member_name) == 0)
                                        {
                                            if (info->fields[j].storage_index >= num_elements)
                                            {
                                                debug_warning(
                                                    "Storage index for member '%s' exceeds struct element "
                                                    "count.",
                                                    member_name
                                                );
                                                return NULL;
                                            }
                                            member_index = j;
                                            storage_index = info->fields[j].storage_index;
                                            break;
                                        }
                                    }
                                }

                                LLVMValueRef indices[2];
                                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), storage_index, false);
                                current_ptr = LLVMBuildInBoundsGEP2(
                                    ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr"
                                );
                                current_type = LLVMStructGetTypeAtIndex(struct_type, storage_index);

                                // Check if this is a bitfield and save metadata for later
                                if (info != NULL && storage_index < info->field_count
                                    && member_index < info->field_count)
                                {
                                    struct_field_t const * field = &info->fields[member_index];
                                    if (field->bit_width > 0)
                                    {
                                        // For bitfield assignment, track the metadata
                                        is_bitfield_assign = true;
                                        bitfield_storage_idx = storage_index;
                                        bitfield_bit_offset = field->bit_offset;
                                        bitfield_bit_width = field->bit_width;
                                        bitfield_struct_ptr = struct_ptr;
                                        bitfield_struct_type = struct_type;

                                        // Point to struct for load/modify/store
                                        current_ptr = struct_ptr;
                                        current_type = struct_type;
                                    }
                                }
                            }
                        }
                    }
                }

                lhs_ptr = current_ptr;
                lhs_type = current_type;
            }
        }
    }
    else
    {
        // Simple variable assignment
        lhs_ptr = get_variable_pointer(ctx, lhs_node, &lhs_type, NULL);
    }

    if (!lhs_ptr)
    {
        debug_error("Could not get pointer for LHS in assignment.");
        return NULL;
    }

    // Check for compound assignment operators (+=, -=, *=, /=, %=, etc.)
    assignment_operator_type_t assign_op_type = node->op.assign.op;
    bool is_compound = (assign_op_type != ASSIGN_OP_SIMPLE);

    LLVMValueRef rhs_value;

    if (is_compound)
    {
        // For compound assignment, load current LHS value
        LLVMValueRef lhs_value = aligned_load(ctx->builder, lhs_type, lhs_ptr, "lhs_load");
        rhs_value = process_expression(ctx, rhs_node);
        if (!rhs_value)
        {
            debug_error("Failed to process RHS expression in compound assignment.");
            return NULL;
        }

        // Determine if this is a floating point operation
        LLVMTypeKind lhs_kind = LLVMGetTypeKind(lhs_type);
        bool is_float = (lhs_kind == LLVMFloatTypeKind || lhs_kind == LLVMDoubleTypeKind);

        // Perform the operation
        switch (assign_op_type)
        {
        case ASSIGN_OP_SIMPLE:
            /* Do nothing. Shouldn't happen. Avoids compiler warning. */
            break;
        case ASSIGN_OP_ADD:
            rhs_value = is_float ? LLVMBuildFAdd(ctx->builder, lhs_value, rhs_value, "fadd_tmp")
                                 : LLVMBuildAdd(ctx->builder, lhs_value, rhs_value, "add_tmp");
            break;
        case ASSIGN_OP_SUB:
            rhs_value = is_float ? LLVMBuildFSub(ctx->builder, lhs_value, rhs_value, "fsub_tmp")
                                 : LLVMBuildSub(ctx->builder, lhs_value, rhs_value, "sub_tmp");
            break;
        case ASSIGN_OP_MUL:
            rhs_value = is_float ? LLVMBuildFMul(ctx->builder, lhs_value, rhs_value, "fmul_tmp")
                                 : LLVMBuildMul(ctx->builder, lhs_value, rhs_value, "mul_tmp");
            break;
        case ASSIGN_OP_DIV:
            rhs_value = is_float ? LLVMBuildFDiv(ctx->builder, lhs_value, rhs_value, "fdiv_tmp")
                                 : LLVMBuildSDiv(ctx->builder, lhs_value, rhs_value, "div_tmp");
            break;
        case ASSIGN_OP_MOD:
            rhs_value = LLVMBuildSRem(ctx->builder, lhs_value, rhs_value, "rem_tmp");
            break;
        case ASSIGN_OP_AND:
            rhs_value = LLVMBuildAnd(ctx->builder, lhs_value, rhs_value, "and_tmp");
            break;
        case ASSIGN_OP_OR:
            rhs_value = LLVMBuildOr(ctx->builder, lhs_value, rhs_value, "or_tmp");
            break;
        case ASSIGN_OP_XOR:
            rhs_value = LLVMBuildXor(ctx->builder, lhs_value, rhs_value, "xor_tmp");
            break;
        case ASSIGN_OP_SHL:
            rhs_value = LLVMBuildShl(ctx->builder, lhs_value, rhs_value, "shl_tmp");
            break;
        case ASSIGN_OP_SHR:
            rhs_value = LLVMBuildLShr(ctx->builder, lhs_value, rhs_value, "lshr_tmp");
            break;
        default:
            debug_error("Unknown compound assignment operator.");
            return NULL;
        }
    }
    else
    {
        // Process the RHS expression to get its LLVM ValueRef.
        rhs_value = process_expression(ctx, rhs_node);
        if (!rhs_value)
        {
            debug_error("Failed to process RHS expression in assignment.");
            return NULL;
        }
    }

    // Generate the store instruction.
    if (is_bitfield_assign && bitfield_struct_ptr && bitfield_struct_type)
    {
        // For bitfield assignment, we need to:
        // 1. Load the current struct value
        // 2. Clear the bits at bitfield position
        // 3. Shift new value to correct position
        // 4. OR to combine
        // 5. Store back

        // Load current struct value
        LLVMValueRef current_struct
            = aligned_load(ctx->builder, bitfield_struct_type, bitfield_struct_ptr, "bf_struct_load");

        // Get the field type
        LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(bitfield_struct_type, bitfield_storage_idx);

        // Extract current field value using LLVM 20 API (index as unsigned)
        LLVMValueRef current_field
            = LLVMBuildExtractValue(ctx->builder, current_struct, bitfield_storage_idx, "bf_extract");

        // Clear the bits at bitfield position
        // Create mask: ~(((1 << width) - 1) << offset)
        unsigned long long mask = ((1ULL << bitfield_bit_width) - 1) << bitfield_bit_offset;
        unsigned long long saved_bits_mask = ~mask;

        LLVMValueRef saved_bits_mask_val = LLVMConstInt(LLVMTypeOf(current_field), saved_bits_mask, false);
        LLVMValueRef cleared = LLVMBuildAnd(ctx->builder, current_field, saved_bits_mask_val, "bf_clear");

        // Extend rhs_value to field type if needed and shift to position
        LLVMValueRef shifted;
        if (LLVMGetTypeKind(LLVMTypeOf(rhs_value)) == LLVMIntegerTypeKind)
        {
            // Zero-extend to field type if needed
            rhs_value = cast_value_to_type(ctx, rhs_value, field_type, true);
            // Shift to bitfield position
            LLVMValueRef shift_amt = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), bitfield_bit_offset, false);
            shifted = LLVMBuildShl(ctx->builder, rhs_value, shift_amt, "bf_shift");
        }
        else
        {
            shifted = rhs_value;
        }

        // OR to combine
        LLVMValueRef new_field = LLVMBuildOr(ctx->builder, cleared, shifted, "bf_insert");

        // Insert back into struct using LLVM 20 API
        LLVMValueRef new_struct
            = LLVMBuildInsertValue(ctx->builder, current_struct, new_field, bitfield_storage_idx, "bf_insert_struct");

        // Store back
        aligned_store(ctx->builder, new_struct, bitfield_struct_ptr);
    }
    else
    {
        aligned_store(ctx->builder, rhs_value, lhs_ptr);
    }
    return rhs_value;
}

static LLVMValueRef
process_identifier(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Handle built-in boolean constants
    if (node && node->text != NULL)
    {
        if (strcmp(node->text, "true") == 0)
        {
            return LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, false);
        }
        if (strcmp(node->text, "false") == 0)
        {
            return LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 0, false);
        }
    }

    LLVMValueRef var_ptr;
    LLVMTypeRef element_type;

    // Get the variable's pointer and its element type from the symbol table.
    var_ptr = get_variable_pointer(ctx, node, &element_type, NULL);

    if (var_ptr && element_type) // Ensure both are valid
    {
        // Check if the type is an array (for file-scope or local arrays)
        if (LLVMGetTypeKind(element_type) == LLVMArrayTypeKind)
        {
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            return LLVMBuildInBoundsGEP2(ctx->builder, element_type, var_ptr, indices, 2, "array_ptr");
        }
        // Load the value from the memory address using LLVMBuildLoad2.
        return aligned_load(ctx->builder, element_type, var_ptr, "load_tmp"); // "load_tmp" is a debug name.
    }
    else if (var_ptr == NULL)
    {
        // Check if it's a function name - return the function pointer
        LLVMValueRef func_val = LLVMGetNamedFunction(ctx->module, node->text);
        if (func_val != NULL)
        {
            return func_val;
        }
        debug_error("Undefined variable '%s' used.", node->text);
        return NULL;
    }

    debug_error("NULL element type for variable '%s'.", node->text);
    return NULL;
}

static LLVMValueRef
process_bitwise_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Bitwise ops from chainl1: [LHS, RHS], operator is implied by node type
    LLVMValueRef lhs_val = process_expression(ctx, node->lhs);
    LLVMValueRef rhs_val = process_expression(ctx, node->rhs);
    LLVMTypeRef lhs_type = LLVMTypeOf(lhs_val);
    LLVMTypeRef rhs_type = LLVMTypeOf(rhs_val);
    LLVMTypeKind lhs_type_kind = LLVMGetTypeKind(lhs_type);
    LLVMTypeKind rhs_type_kind = LLVMGetTypeKind(rhs_type);

    // Handle type promotion for integer operands - both sides must match
    if (!(lhs_type_kind == LLVMFloatTypeKind || lhs_type_kind == LLVMDoubleTypeKind)
        && lhs_type_kind == LLVMIntegerTypeKind && rhs_type_kind == LLVMIntegerTypeKind)
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_val = LLVMBuildZExt(ctx->builder, rhs_val, lhs_type, "promote_rhs");
            rhs_type = lhs_type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_val = LLVMBuildZExt(ctx->builder, lhs_val, rhs_type, "promote_lhs");
            lhs_type = rhs_type;
        }
    }

    switch (node->op.bitwise.op)
    {
    case BITWISE_OP_AND:
        return LLVMBuildAnd(ctx->builder, lhs_val, rhs_val, "and_tmp");
    case BITWISE_OP_OR:
        return LLVMBuildOr(ctx->builder, lhs_val, rhs_val, "or_tmp");
    case BITWISE_OP_XOR:
        return LLVMBuildXor(ctx->builder, lhs_val, rhs_val, "xor_tmp");
    }
    return NULL; /* Shouldn't happen. */
}

static LLVMValueRef
process_shift_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    LLVMValueRef lhs_val = process_expression(ctx, node->lhs);
    LLVMValueRef rhs_val = process_expression(ctx, node->rhs);

    switch (node->op.shift.op)
    {
    case SHIFT_OP_LL:
        return LLVMBuildShl(ctx->builder, lhs_val, rhs_val, "shl_tmp");
    case SHIFT_OP_AR:
        return LLVMBuildAShr(ctx->builder, lhs_val, rhs_val, "ashr_tmp");
    }
    return NULL;
}

static LLVMValueRef
process_arithmetic_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    LLVMValueRef lhs_val = process_expression(ctx, node->lhs);
    LLVMValueRef rhs_val = process_expression(ctx, node->rhs);
    LLVMTypeRef lhs_type = LLVMTypeOf(lhs_val);
    LLVMTypeRef rhs_type = LLVMTypeOf(rhs_val);
    LLVMTypeKind lhs_type_kind = LLVMGetTypeKind(lhs_type);
    LLVMTypeKind rhs_type_kind = LLVMGetTypeKind(rhs_type);
    bool is_float_op = (lhs_type_kind == LLVMFloatTypeKind || lhs_type_kind == LLVMDoubleTypeKind);

    // Handle type promotion for integer operands
    // If lhs is wider than rhs (e.g., long vs int), promote rhs to match
    if (!is_float_op && lhs_type_kind == LLVMIntegerTypeKind && rhs_type_kind == LLVMIntegerTypeKind)
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_val = LLVMBuildSExt(ctx->builder, rhs_val, lhs_type, "promote_rhs");
            rhs_type = lhs_type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_val = LLVMBuildSExt(ctx->builder, lhs_val, rhs_type, "promote_lhs");
            lhs_type = rhs_type;
        }
    }

    switch (node->op.arith.op)
    {
    case ARITH_OP_ADD:
        return is_float_op ? LLVMBuildFAdd(ctx->builder, lhs_val, rhs_val, "fadd_tmp")
                           : LLVMBuildAdd(ctx->builder, lhs_val, rhs_val, "add_tmp");
    case ARITH_OP_SUB:
        return is_float_op ? LLVMBuildFSub(ctx->builder, lhs_val, rhs_val, "fsub_tmp")
                           : LLVMBuildSub(ctx->builder, lhs_val, rhs_val, "sub_tmp");
    case ARITH_OP_MUL:
        return is_float_op ? LLVMBuildFMul(ctx->builder, lhs_val, rhs_val, "fmul_tmp")
                           : LLVMBuildMul(ctx->builder, lhs_val, rhs_val, "mul_tmp");
    case ARITH_OP_DIV:
        return is_float_op ? LLVMBuildFDiv(ctx->builder, lhs_val, rhs_val, "fdiv_tmp")
                           : LLVMBuildSDiv(ctx->builder, lhs_val, rhs_val, "div_tmp");
    case ARITH_OP_MOD:
        return LLVMBuildSRem(ctx->builder, lhs_val, rhs_val, "rem_tmp");
    }
    return NULL; /* Shouldn't happen. */
}

static LLVMValueRef
process_relational_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    LLVMValueRef lhs_val = process_expression(ctx, node->lhs);
    LLVMValueRef rhs_val = process_expression(ctx, node->rhs);
    LLVMTypeRef lhs_type = LLVMTypeOf(lhs_val);
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float_op = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

    switch (node->op.rel.op)
    {
    case REL_OP_LT:
        return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs_val, rhs_val, "flt_tmp")
                           : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs_val, rhs_val, "lt_tmp");
    case REL_OP_GT:
        return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs_val, rhs_val, "fgt_tmp")
                           : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs_val, rhs_val, "gt_tmp");
    case REL_OP_LE:
        return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs_val, rhs_val, "fle_tmp")
                           : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs_val, rhs_val, "le_tmp");
    case REL_OP_GE:
        return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs_val, rhs_val, "fge_tmp")
                           : LLVMBuildICmp(ctx->builder, LLVMIntSGE, lhs_val, rhs_val, "ge_tmp");
    }
    return NULL; /* Shouldn't happen. */
}

static LLVMValueRef
process_equality_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    LLVMValueRef lhs_val = process_expression(ctx, node->lhs);
    LLVMValueRef rhs_val = process_expression(ctx, node->rhs);
    LLVMTypeRef lhs_type = LLVMTypeOf(lhs_val);
    LLVMTypeRef rhs_type = LLVMTypeOf(rhs_val);
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float_op = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

    // Handle type promotion for integer operands - both sides must match
    if (!is_float_op && type_kind == LLVMIntegerTypeKind && LLVMGetTypeKind(rhs_type) == LLVMIntegerTypeKind)
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_val = LLVMBuildSExt(ctx->builder, rhs_val, lhs_type, "promote_rhs");
            rhs_type = lhs_type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_val = LLVMBuildSExt(ctx->builder, lhs_val, rhs_type, "promote_lhs");
            lhs_type = rhs_type;
        }
    }

    switch (node->op.eq.op)
    {
    case EQ_OP_EQ:
        return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs_val, rhs_val, "feq_tmp")
                           : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs_val, rhs_val, "eq_tmp");
    case EQ_OP_NE:
        return is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs_val, rhs_val, "fne_tmp")
                           : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_val, rhs_val, "ne_tmp");
    }
    return NULL; /* Shouldn't happen. */
}

static LLVMValueRef
process_logical_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    bool is_or = (node->op.logical.op == LOGICAL_OP_OR);
    c_grammar_node_t const * lhs_node = node->lhs;
    c_grammar_node_t const * rhs_node = node->rhs;

    LLVMValueRef res_alloca = LLVMBuildAlloca(ctx->builder, LLVMInt1TypeInContext(ctx->context), "logical_res");

    LLVMBasicBlockRef rhs_block = LLVMAppendBasicBlockInContext(
        ctx->context, LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder)), "logical_rhs"
    );
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(
        ctx->context, LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder)), "logical_merge"
    );

    LLVMValueRef lhs_val = process_expression(ctx, lhs_node);
    // Convert to i1
    if (LLVMGetTypeKind(LLVMTypeOf(lhs_val)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(lhs_val)) != 1)
    {
        lhs_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_val, LLVMConstNull(LLVMTypeOf(lhs_val)), "bool_tmp");
    }

    aligned_store(ctx->builder, lhs_val, res_alloca);
    if (is_or)
    {
        LLVMBuildCondBr(ctx->builder, lhs_val, merge_block, rhs_block);
    }
    else
    {
        LLVMBuildCondBr(ctx->builder, lhs_val, rhs_block, merge_block);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, rhs_block);
    LLVMValueRef rhs_val = process_expression(ctx, rhs_node);
    if (LLVMGetTypeKind(LLVMTypeOf(rhs_val)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(rhs_val)) != 1)
    {
        rhs_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs_val, LLVMConstNull(LLVMTypeOf(rhs_val)), "bool_tmp");
    }
    aligned_store(ctx->builder, rhs_val, res_alloca);
    LLVMBuildBr(ctx->builder, merge_block);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
    return aligned_load(ctx->builder, LLVMInt1TypeInContext(ctx->context), res_alloca, "logical_final");
}

static LLVMValueRef
process_conditional_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Conditional expression: condition ? true_expr : false_expr
    // Stored in node->lhs (condition), node->rhs (true_expr), node->false_expr (false_expr)
    c_grammar_node_t const * condition_node = node->lhs;
    c_grammar_node_t const * true_expr_node = node->rhs;
    c_grammar_node_t const * false_expr_node = node->false_expr;

    if (!condition_node || !true_expr_node || !false_expr_node)
    {
        debug_error("Invalid conditional expression");
        return NULL;
    }

    // Get current function and create blocks
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

    LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "cond_then");
    LLVMBasicBlockRef else_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "cond_else");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "cond_merge");

    // Evaluate condition
    LLVMValueRef cond_val = process_expression(ctx, condition_node);
    if (!cond_val)
    {
        return NULL;
    }

    // Convert condition to i1 if needed
    LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
    if (LLVMGetTypeKind(cond_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(cond_type) != 1)
    {
        cond_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, LLVMConstNull(cond_type), "cond_bool");
    }

    // Branch to then or else
    LLVMBuildCondBr(ctx->builder, cond_val, then_block, else_block);

    // Generate then block
    LLVMPositionBuilderAtEnd(ctx->builder, then_block);
    LLVMValueRef true_val = process_expression(ctx, true_expr_node);
    if (!true_val)
    {
        return NULL;
    }
    // After processing true_expr (which might be a nested ternary), the builder
    // is positioned at the nested ternary's merge block. Save this block
    // before branching to our merge block.
    LLVMBasicBlockRef true_block = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_block);

    // Generate else block
    LLVMPositionBuilderAtEnd(ctx->builder, else_block);
    LLVMValueRef false_val = process_expression(ctx, false_expr_node);
    if (!false_val)
    {
        return NULL;
    }
    // After processing false_expr (which might be a nested ternary), the builder
    // is positioned at the nested ternary's merge block. Save this block
    // before branching to our merge block.
    LLVMBasicBlockRef false_block = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_block);

    // Merge and create phi node
    LLVMPositionBuilderAtEnd(ctx->builder, merge_block);

    LLVMTypeRef result_type = LLVMTypeOf(true_val);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, result_type, "cond_result");

    // Add phi operands using the actual blocks where the expressions ended
    LLVMAddIncoming(phi, &true_val, &true_block, 1);
    LLVMAddIncoming(phi, &false_val, &false_block, 1);

    return phi;
}

static LLVMValueRef
process_unary_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Unary structure: [Operator, Operand]
    c_grammar_node_t const * operand_node = node->lhs;

    switch (node->op.unary.op)
    {
    case UNARY_OP_ADDR:
    {
        // For &identifier, return the pointer directly (don't load)
        if (operand_node->type == AST_NODE_IDENTIFIER)
        {
            LLVMValueRef var_ptr;
            LLVMTypeRef var_type;
            if (find_symbol(ctx, operand_node->text, &var_ptr, &var_type, NULL))
            {
                return var_ptr;
            }
        }

        // For &compound_literal, we need to create a pointer to the temp
        // The compound literal code returns a loaded value, but we need the pointer
        if (operand_node->type == AST_NODE_COMPOUND_LITERAL)
        {
            // Extract the type and initializer from the compound literal
            if (operand_node->list.count < 2)
            {
                break;
            }

            c_grammar_node_t const * type_name_node = operand_node->list.children[0];
            c_grammar_node_t const * init_list_node = operand_node->list.children[1];

            /* Extract type name - check struct/union keyword first, then typedef */
            char const * type_name = NULL;
            bool is_typedef = false;
            if (type_name_node->type == AST_NODE_TYPE_NAME)
            {
                for (size_t i = 0; i < type_name_node->list.count && !type_name; ++i)
                {
                    c_grammar_node_t const * child = type_name_node->list.children[i];
                    /* Try struct/union keyword first */
                    type_name = extract_struct_or_union_or_enum_tag(child);
                    if (type_name == NULL)
                    {
                        /* Try typedef */
                        type_name = extract_typedef_name(child);
                        if (type_name != NULL)
                        {
                            is_typedef = true;
                        }
                    }
                }
            }

            if (type_name == NULL)
            {
                debug_error("Could not extract type name from compound literal in unary &");
                break;
            }

            LLVMTypeRef compound_type
                = is_typedef ? find_typedef_type(ctx, type_name) : find_type_by_tag(ctx, type_name);
            if (compound_type == NULL)
            {
                debug_error("Unknown type '%s' in compound literal in unary &", type_name);
                break;
            }

            // Create a temporary local variable (alloca) for the compound literal
            LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, compound_type, "compound_literal_tmp");
            if (alloca_inst == NULL)
            {
                debug_error("Failed to allocate compound literal for unary &");
                break;
            }

            // Initialize using the initializer list
            if (init_list_node->type == AST_NODE_INITIALIZER_LIST)
            {
                int current_index = 0;
                process_initializer_list(ctx, alloca_inst, compound_type, init_list_node, &current_index);
            }

            // Return the pointer to the alloca (not the loaded value)
            return alloca_inst;
        }

        // For &member or &array[i], process the expression which returns a pointer
        LLVMValueRef ptr_val = process_expression(ctx, operand_node);
        return ptr_val;
    }

    case UNARY_OP_DEREF:
    {
        LLVMValueRef operand_val = process_expression(ctx, operand_node);
        if (!operand_val)
            return NULL;

        // Try to get the pointee_type from the symbol table if operand is an identifier
        LLVMTypeRef elem_type = NULL;

        if (operand_node && operand_node->type == AST_NODE_IDENTIFIER)
        {
            LLVMValueRef var_ptr = NULL;
            LLVMTypeRef var_type = NULL;
            LLVMTypeRef pointee_type = NULL;
            if (find_symbol(ctx, operand_node->text, &var_ptr, &var_type, &pointee_type))
            {
                elem_type = pointee_type;
            }
        }

        // If we couldn't get pointee_type from symbol, try the generic approach
        if (!elem_type)
        {
            LLVMTypeRef ptr_type = LLVMTypeOf(operand_val);
            elem_type = get_pointer_element_type(ctx, ptr_type);
        }

        if (elem_type)
        {
            return aligned_load(ctx->builder, elem_type, operand_val, "deref_tmp");
        }
        return operand_val;
    }

    case UNARY_OP_MINUS:
    {
        LLVMValueRef operand_val = process_expression(ctx, operand_node);
        if (!operand_val)
            return NULL;
        LLVMTypeRef op_type = LLVMTypeOf(operand_val);
        if (op_type
            && (LLVMGetTypeKind(op_type) == LLVMFloatTypeKind || LLVMGetTypeKind(op_type) == LLVMDoubleTypeKind))
            return LLVMBuildFNeg(ctx->builder, operand_val, "fneg_tmp");
        return LLVMBuildNeg(ctx->builder, operand_val, "neg_tmp");
    }

    case UNARY_OP_NOT:
    {
        LLVMValueRef operand_val = process_expression(ctx, operand_node);
        if (!operand_val)
            return NULL;
        LLVMTypeRef op_type = LLVMTypeOf(operand_val);
        if (!op_type)
            return NULL;
        LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand_val, LLVMConstNull(op_type), "not_tmp");
        return is_zero;
    }

    case UNARY_OP_BITNOT:
    {
        LLVMValueRef operand_val = process_expression(ctx, operand_node);
        if (!operand_val)
            return NULL;
        return LLVMBuildNot(ctx->builder, operand_val, "bitnot_tmp");
    }

    case UNARY_OP_INC:
    case UNARY_OP_DEC:
    {
        LLVMValueRef var_ptr = NULL;
        LLVMTypeRef var_type = NULL;

        var_ptr = get_variable_pointer(ctx, operand_node, &var_type, NULL);

        if (var_ptr && var_type)
        {
            LLVMValueRef original_val = aligned_load(ctx->builder, var_type, var_ptr, "orig_val");
            LLVMValueRef one = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);

            LLVMValueRef new_val;
            if (node->op.unary.op == UNARY_OP_INC)
            {
                LLVMTypeKind kind = LLVMGetTypeKind(var_type);
                if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                    new_val = LLVMBuildFAdd(ctx->builder, original_val, LLVMConstReal(var_type, 1.0), "inc_tmp");
                else
                    new_val = LLVMBuildAdd(ctx->builder, original_val, one, "inc_tmp");
            }
            else
            {
                LLVMTypeKind kind = LLVMGetTypeKind(var_type);
                if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                    new_val = LLVMBuildFSub(ctx->builder, original_val, LLVMConstReal(var_type, 1.0), "dec_tmp");
                else
                    new_val = LLVMBuildSub(ctx->builder, original_val, one, "dec_tmp");
            }

            aligned_store(ctx->builder, new_val, var_ptr);
            return original_val;
        }
        return NULL;
    }

    case UNARY_OP_PLUS:
    {
        return process_expression(ctx, operand_node);
    }

    case UNARY_OP_SIZEOF:
    {
        LLVMTypeRef target_type = NULL;

        // Check if operand is a TypeName (e.g., sizeof(int) or sizeof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            // TypeName contains TypeSpecifier(s), possibly with struct/union keyword
            for (size_t i = 0; i < operand_node->list.count && target_type == NULL; i++)
            {
                c_grammar_node_t * child = operand_node->list.children[i];

                // Handle terminal type specifier (e.g., "int", "char")
                if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    if (child->text != NULL)
                    {
                        char const * type_name = child->text;

                        target_type = get_type_from_name(ctx, type_name);
                    }
                    else
                    {
                        target_type = map_type(ctx, child, NULL);
                    }
                }
                // Handle Identifier (struct name like "Point" in sizeof(struct Point))
                else if (child->type == AST_NODE_IDENTIFIER)
                {
                    char const * type_name = child->text;
                    LLVMTypeRef struct_type = find_type_by_tag(ctx, type_name);
                    if (struct_type)
                    {
                        target_type = struct_type;
                    }
                }
            }
        }
        // Check if operand is a type specifier (e.g., sizeof(int))
        else if (operand_node->type == AST_NODE_TYPE_SPECIFIER)
        {
            if (operand_node->text != NULL)
            {
                char const * type_name = operand_node->text;
                target_type = get_type_from_name(ctx, type_name);
            }
            else
            {
                // Non-terminal type specifier - use map_type
                target_type = map_type(ctx, operand_node, NULL);
            }
        }
        else if (operand_node->type == AST_NODE_DECL_SPECIFIERS)
        {
            target_type = map_type(ctx, operand_node, NULL);
        }
        // Check if operand is an identifier (e.g., sizeof(x) or sizeof(arr))
        else if (operand_node->type == AST_NODE_IDENTIFIER)
        {
            char const * var_name = operand_node->text;
            LLVMValueRef var_ptr;
            LLVMTypeRef var_type;
            if (find_symbol(ctx, var_name, &var_ptr, &var_type, NULL))
            {
                target_type = var_type;
            }
        }
        // Otherwise, try processing as expression (for things like sizeof(*ptr))
        else
        {
            // Handle dereference specially: sizeof(*ptr) should give sizeof of pointee type
            if (operand_node->type == AST_NODE_UNARY_EXPRESSION && operand_node->op.unary.op == UNARY_OP_DEREF)
            {
                c_grammar_node_t const * deref_operand = operand_node->lhs;
                if (deref_operand && deref_operand->type == AST_NODE_IDENTIFIER)
                {
                    char const * var_name = deref_operand->text;
                    LLVMValueRef var_ptr;
                    LLVMTypeRef var_type;
                    LLVMTypeRef pointee_type;
                    if (find_symbol(ctx, var_name, &var_ptr, &var_type, &pointee_type))
                    {
                        // If pointee_type is NULL (due to opaque pointer), compute from var_type manually
                        if (pointee_type == NULL && var_type != NULL
                            && LLVMGetTypeKind(var_type) == LLVMPointerTypeKind)
                        {
                            // Try to get the type from the declaration specifiers - look up in struct registry
                            // This is a workaround for opaque pointers
                            pointee_type = LLVMInt32TypeInContext(ctx->context);
                        }
                        target_type = pointee_type;
                    }
                }
            }

            // Fall back to processing expression if we haven't found type yet
            if (target_type == NULL)
            {
                LLVMValueRef expr_val = process_expression(ctx, operand_node);
                if (expr_val != NULL)
                {
                    target_type = LLVMTypeOf(expr_val);
                }
            }
        }

        if (target_type != NULL)
        {
            return get_type_size(ctx, target_type);
        }
        return NULL;
    }
    case UNARY_OP_ALIGNOF:
    {
        // alignof is similar to sizeof but returns alignment
        LLVMTypeRef target_type = NULL;

        // Handle TypeName (e.g., alignof(int) or alignof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            for (size_t i = 0; i < operand_node->list.count && target_type == NULL; i++)
            {
                c_grammar_node_t * child = operand_node->list.children[i];

                if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    if (child->text != NULL)
                    {
                        char const * type_name = child->text;

                        target_type = get_type_from_name(ctx, type_name);
                    }
                    else
                    {
                        target_type = map_type(ctx, child, NULL);
                    }
                }
                else if (child->type == AST_NODE_IDENTIFIER)
                {
                    char const * type_name = child->text;
                    LLVMTypeRef struct_type = find_type_by_tag(ctx, type_name);
                    if (struct_type)
                    {
                        target_type = struct_type;
                    }
                }
            }
        }
        else if (operand_node->type == AST_NODE_TYPE_SPECIFIER || operand_node->type == AST_NODE_DECL_SPECIFIERS)
        {
            target_type = map_type(ctx, operand_node, NULL);
        }
        else if (operand_node->type == AST_NODE_IDENTIFIER)
        {
            char const * var_name = operand_node->text;
            LLVMValueRef var_ptr;
            LLVMTypeRef var_type;
            if (find_symbol(ctx, var_name, &var_ptr, &var_type, NULL))
            {
                target_type = var_type;
            }
        }
        else
        {
            LLVMValueRef expr_val = process_expression(ctx, operand_node);
            if (expr_val != NULL)
            {
                target_type = LLVMTypeOf(expr_val);
            }
        }

        if (target_type != NULL)
        {
            unsigned alignment = get_type_alignment(target_type);
            return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), alignment, false);
        }
        return NULL;
    }
    default:
    {
        debug_error("Unknown unary operator %u.", node->op.unary.op);
        return NULL;
    }
    }

    return NULL; /* Shouldn't happen. */
}

/**
 * @brief Processes an expression AST node and returns its LLVM ValueRef.
 * This function recursively handles various expression types.
 */
static LLVMValueRef
_process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NULL;
    }

    // fprintf(stderr, "%s node type: %s (%u)\n", __func__, get_node_type_name_from_node(node), node->type);

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
        /* Comma expression: evaluate all expressions, return the last value. */
        LLVMValueRef result = NULL;
        for (size_t i = 0; i < node->list.count; ++i)
        {
            result = process_expression(ctx, node->list.children[i]);
            if (!result)
            {
                return NULL;
            }
        }
        return result;
    }
    case AST_NODE_UNARY_EXPRESSION:
    {
        return process_unary_expression(ctx, node);
    }
    case AST_NODE_FUNCTION_CALL:
    case AST_NODE_POSTFIX_PARTS:
    {
        debug_error("got %s in %s", get_node_type_name_from_type(node->type), __func__);
        /* Shouldn't happen. */
        break;
    }
    case AST_NODE_COMPOUND_LITERAL:
    {
        // CompoundLiteral: (type){initializer-list}
        // e.g., (struct Pos){.x = 1, .y = 2} or (union Data){.x = 1}
        // Structure: TypeName + InitializerList
        if (node->list.count < 2)
        {
            break;
        }

        // First child is TypeName, second is InitializerList
        c_grammar_node_t const * type_name_node = node->list.children[0];
        c_grammar_node_t const * init_list_node = node->list.children[1];

        /* Extract type name - check struct/union keyword first, then typedef */
        char const * type_name = NULL;
        bool is_typedef = false;

        if (type_name_node->type == AST_NODE_TYPE_NAME)
        {
            for (size_t i = 0; i < type_name_node->list.count && !type_name; ++i)
            {
                c_grammar_node_t const * child = type_name_node->list.children[i];
                /* Try struct/union keyword first */
                type_name = extract_struct_or_union_or_enum_tag(child);
                if (type_name == NULL)
                {
                    /* Try typedef */
                    type_name = extract_typedef_name(child);
                    if (type_name != NULL)
                    {
                        is_typedef = true;
                    }
                }
            }
        }

        if (type_name == NULL)
        {
            debug_error("Could not extract type name from compound literal");
            break;
        }

        /* Look up the type - struct list or typedef list */
        LLVMTypeRef compound_type = is_typedef ? find_typedef_type(ctx, type_name) : find_type_by_tag(ctx, type_name);
        if (compound_type == NULL)
        {
            debug_error("Unknown type '%s' in compound literal", type_name);
            break;
        }

        // Create a temporary local variable (alloca) for the compound literal
        LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, compound_type, "compound_literal_tmp");
        if (alloca_inst == NULL)
        {
            debug_error("Failed to allocate compound literal");
            break;
        }

        // Initialize using the initializer list
        if (init_list_node->type == AST_NODE_INITIALIZER_LIST)
        {
            int current_index = 0;
            process_initializer_list(ctx, alloca_inst, compound_type, init_list_node, &current_index);
        }

        // Load the value from the alloca and return it
        // This allows passing compound literals to functions expecting struct/union by value
        LLVMValueRef loaded = aligned_load(ctx->builder, compound_type, alloca_inst, "compound_literal_val");
        return loaded;
    }
    case AST_NODE_TRANSLATION_UNIT:
    case AST_NODE_FUNCTION_DEFINITION:
    case AST_NODE_COMPOUND_STATEMENT:
    case AST_NODE_DECLARATION:
    case AST_NODE_INTEGER_BASE:
    case AST_NODE_FLOAT_BASE:
    case AST_NODE_LITERAL_SUFFIX:
    case AST_NODE_DECL_SPECIFIERS:
    case AST_NODE_TYPE_SPECIFIER:
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
    case AST_NODE_EXPRESSION_STATEMENT:
    case AST_NODE_STRUCT_DEFINITION:
    case AST_NODE_UNION_DEFINITION:
    case AST_NODE_ENUM_DEFINITION:
    case AST_NODE_STRUCT_TYPE_REF:
    case AST_NODE_UNION_TYPE_REF:
    case AST_NODE_ENUM_TYPE_REF:
    case AST_NODE_TYPEDEF_DECLARATION:
    case AST_NODE_INITIALIZER_LIST:
    case AST_NODE_LABELED_STATEMENT:
    case AST_NODE_CASE_LABEL:
    case AST_NODE_SWITCH_CASE:
    case AST_NODE_DEFAULT_STATEMENT:
    case AST_NODE_ASSIGNMENT_OPERATOR:
    case AST_NODE_OPTIONAL_KW_EXTENSION:
    case AST_NODE_OPTIONAL_INIT_DECLARATOR_LIST:
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
    case AST_NODE_INITIALIZER:
    default:
        // Attempt to recursively process if it might yield a value.
        if (node->list.count > 0)
        {
            debug_info("Default processing for list node: %s %u", get_node_type_name_from_type(node->type), node->type);
            for (size_t i = 0; i < node->list.count; ++i)
            {
                LLVMValueRef res = process_expression(ctx, node->list.children[i]);
                if (res)
                    return res; // Return the first valid result found.
            }
        }
        else
        {
            debug_info("Ignoring terminal node %s (%u)", get_node_type_name_from_type(node->type), node->type);
            if (node->text != NULL)
            {
                debug_info("text: `%s`", node->text);
            }
        }
        break;
    }
    return NULL; // Return NULL if expression processing failed or not implemented.
}

static LLVMValueRef
process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (ctx->errors.fatal)
    {
        return NULL;
    }
    LLVMValueRef result = _process_expression(ctx, node);

    if (ctx->errors.fatal)
    {
        return NULL;
    }

    return result;
}
