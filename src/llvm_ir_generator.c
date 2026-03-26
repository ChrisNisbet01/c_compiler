#include "llvm_ir_generator.h"

#include "ast_node_name.h"
#include "c_grammar_ast.h" // Assumes this header defines c_grammar_node_t and its node types

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declare the context structure as it's used before its definition
// typedef struct ir_generator_ctx ir_generator_ctx_t; // Assuming this is declared in a header or elsewhere

// Symbol table management functions
static scope_t * scope_create(scope_t * parent);
static void scope_free(scope_t * scope);
static void scope_push(ir_generator_ctx_t * ctx);
static void scope_pop(ir_generator_ctx_t * ctx);
static void
add_symbol(ir_generator_ctx_t * ctx, char const * name, LLVMValueRef ptr, LLVMTypeRef type, LLVMTypeRef pointee_type);
static LLVMValueRef get_variable_pointer(
    ir_generator_ctx_t * ctx,
    c_grammar_node_t const * identifier_node,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
);
static void free_symbol_table(ir_generator_ctx_t * ctx);
static bool find_symbol(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef * out_ptr,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
); // Helper for get_variable_pointer

static LLVMValueRef process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);
static char const * find_symbol_struct_name(ir_generator_ctx_t * ctx, char const * name);
static LLVMTypeRef find_struct_type(ir_generator_ctx_t * ctx, char const * name);
static void register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child);

static void add_struct_type(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMTypeRef struct_type,
    char ** member_names,
    LLVMTypeRef * member_types,
    size_t num_members
);
static LLVMTypeRef find_struct_type(ir_generator_ctx_t * ctx, char const * name);
static struct_info_t * find_struct_info(ir_generator_ctx_t * ctx, char const * name);
static c_grammar_node_t * find_direct_declarator(c_grammar_node_t * declarator);

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
    if (subscript_node->data.list.count >= 1)
    {
        c_grammar_node_t * index_node = subscript_node->data.list.children[0];
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
        fprintf(stderr, "IRGen Error: Invalid type for array subscript.\n");
        return NULL;
    }

    return elem_ptr;
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

    for (size_t i = 0; i < postfix_node->data.list.count; ++i)
    {
        c_grammar_node_t * suffix = postfix_node->data.list.children[i];

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

            if (suffix->data.list.count > 0)
            {
                num_args = suffix->data.list.count;
                args = malloc(num_args * sizeof(*args));
                for (size_t j = 0; j < num_args; ++j)
                {
                    args[j] = process_expression(ctx, suffix->data.list.children[j]);
                }
            }

            if (!current_val)
            {
                if (base_node && base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * func_name = base_node->data.terminal.text;
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
            /* The one and only child is an IDENTIFIER node. */
            c_grammar_node_t * child = suffix->data.list.children[0];
            char * member_name = child->data.terminal.text;

            if (current_val || current_ptr)
            {
                LLVMTypeRef struct_type = NULL;
                bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);

                if (is_arrow && base_node && base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * sname = find_symbol_struct_name(ctx, base_node->data.terminal.text);
                    if (sname)
                        struct_type = find_struct_type(ctx, sname);
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
                    struct_info_t * info = NULL;

                    for (size_t si = 0; si < ctx->struct_count; si++)
                    {
                        if (ctx->structs[si].type == struct_type)
                        {
                            info = &ctx->structs[si];
                            break;
                        }
                    }

                    if (info)
                    {
                        for (unsigned j = 0; j < num_elements && j < info->field_count; j++)
                        {
                            if (info->fields[j].name && strcmp(info->fields[j].name, member_name) == 0)
                            {
                                member_index = j;
                                break;
                            }
                        }
                    }

                    LLVMValueRef indices[2];
                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), member_index, false);

                    if (is_arrow || (current_type && LLVMGetTypeKind(current_type) == LLVMPointerTypeKind))
                    {
                        if (is_arrow && current_ptr)
                            current_ptr = aligned_load(ctx->builder, current_type, current_ptr, "arrow_ptr");
                        else if (current_val)
                            current_ptr = LLVMBuildInBoundsGEP2(
                                ctx->builder, current_type, current_val, indices, 2, "memberptr"
                            );
                    }
                    else if (current_val)
                    {
                        LLVMValueRef struct_ptr = LLVMBuildAlloca(ctx->builder, struct_type, "struct_tmp");
                        aligned_store(ctx->builder, current_val, struct_ptr);
                        current_ptr
                            = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr");
                    }

                    if (current_ptr)
                    {
                        current_type = LLVMStructGetTypeAtIndex(struct_type, member_index);
                        current_val = aligned_load(ctx->builder, current_type, current_ptr, "member");
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

// Helper to map C types to LLVM types
static LLVMTypeRef
map_type(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator);

// --- Forward Declarations ---
static void process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);
static LLVMValueRef process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);

// --- Scope management functions ---

static scope_t *
scope_create(scope_t * parent)
{
    scope_t * scope = calloc(1, sizeof(*scope));
    if (!scope)
        return NULL;

    scope->capacity = 16;
    scope->symbols = calloc(scope->capacity, sizeof(*scope->symbols));
    if (!scope->symbols)
    {
        free(scope);
        return NULL;
    }
    scope->count = 0;
    scope->parent = parent;
    return scope;
}

static void
scope_free(scope_t * scope)
{
    if (!scope)
        return;

    // Free all symbol names and struct names in this scope
    for (size_t i = 0; i < scope->count; ++i)
    {
        free(scope->symbols[i].name);
        free(scope->symbols[i].struct_name);
    }
    free(scope->symbols);
    free(scope);
}

static void
scope_push(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;

    scope_t * new_scope = scope_create(ctx->current_scope);
    if (new_scope)
    {
        ctx->current_scope = new_scope;
    }
}

static void
scope_pop(ir_generator_ctx_t * ctx)
{
    if (!ctx || !ctx->current_scope)
        return;

    scope_t * old_scope = ctx->current_scope;
    ctx->current_scope = old_scope->parent;
    scope_free(old_scope);
}

static void
add_symbol_with_struct(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef ptr,
    LLVMTypeRef type,
    LLVMTypeRef pointee_type,
    char const * struct_name
)
{
    if (!ctx || !name || !ptr || !type || !ctx->current_scope)
        return;

    scope_t * scope = ctx->current_scope;

    if (scope->count >= scope->capacity)
    {
        size_t new_cap = scope->capacity == 0 ? 16 : scope->capacity * 2;
        symbol_t * new_symbols = realloc(scope->symbols, new_cap * sizeof(*new_symbols));
        if (!new_symbols)
            return;
        scope->symbols = new_symbols;
        scope->capacity = new_cap;
    }

    scope->symbols[scope->count].name = strdup(name);
    scope->symbols[scope->count].ptr = ptr;
    scope->symbols[scope->count].type = type;
    scope->symbols[scope->count].pointee_type = pointee_type;
    scope->symbols[scope->count].struct_name = struct_name ? strdup(struct_name) : NULL;
    scope->count++;
}

static void
add_symbol(ir_generator_ctx_t * ctx, char const * name, LLVMValueRef ptr, LLVMTypeRef type, LLVMTypeRef pointee_type)
{
    add_symbol_with_struct(ctx, name, ptr, type, pointee_type, NULL);
}

static char const *
find_symbol_struct_name(ir_generator_ctx_t * ctx, char const * name)
{
    if (!ctx || !ctx->current_scope)
        return NULL;

    // Search from current scope outward through parent scopes
    for (scope_t * scope = ctx->current_scope; scope; scope = scope->parent)
    {
        for (size_t i = 0; i < scope->count; ++i)
        {
            if (scope->symbols[i].name != NULL && strcmp(scope->symbols[i].name, name) == 0)
            {
                return scope->symbols[i].struct_name;
            }
        }
    }
    return NULL;
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

    if (!initializer_node->is_terminal_node)
    {
        // Use a local index for processing leaf elements at this level
        int local_index = 0;

        for (size_t i = 0; i < initializer_node->data.list.count; ++i)
        {
            c_grammar_node_t const * child = initializer_node->data.list.children[i];

            if (child->is_terminal_node && child->type != AST_NODE_INTEGER_LITERAL)
            {
                continue;
            }

            // If child is an INITIALIZER_LIST, create GEP to the row and recurse
            if (child->type == AST_NODE_INITIALIZER_LIST && kind == LLVMArrayTypeKind)
            {
                LLVMTypeRef nested_element = LLVMGetElementType(element_type);
                LLVMValueRef indices[2];
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), local_index, false);
                LLVMValueRef row_ptr
                    = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "row_ptr");
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
            if (kind == LLVMArrayTypeKind && child->type != AST_NODE_INTEGER_LITERAL && !child->is_terminal_node
                && child->data.list.count > 0)
            {
                LLVMTypeRef nested_element = LLVMGetElementType(element_type);
                LLVMValueRef indices[2];
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), local_index, false);
                LLVMValueRef elem_ptr
                    = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");
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
                    LLVMTypeRef value_type = LLVMTypeOf(value);
                    if (kind == LLVMStructTypeKind)
                    {
                        LLVMTypeRef member_type = LLVMStructGetTypeAtIndex(element_type, (unsigned)local_index);
                        if (member_type && member_type != value_type)
                        {
                            LLVMTypeKind member_kind = LLVMGetTypeKind(member_type);
                            LLVMTypeKind value_kind = LLVMGetTypeKind(value_type);
                            if (member_kind == LLVMIntegerTypeKind && value_kind == LLVMIntegerTypeKind)
                            {
                                unsigned member_bits = LLVMGetIntTypeWidth(member_type);
                                unsigned value_bits = LLVMGetIntTypeWidth(value_type);
                                if (member_bits < value_bits)
                                    value = LLVMBuildTrunc(ctx->builder, value, member_type, "trunc_val");
                                else if (member_bits > value_bits)
                                    value = LLVMBuildSExt(ctx->builder, value, member_type, "sext_val");
                            }
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
}

/**
 * @brief Finds a symbol in the symbol table and returns its pointer and type.
 * @param ctx The IR generator context.
 * @param name The name of the symbol to find.
 * @param out_ptr Pointer to store the found LLVMValueRef.
 * @param out_type Pointer to store the found LLVMTypeRef.
 * @return True if the symbol was found, false otherwise.
 */
static bool
find_symbol(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef * out_ptr,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
)
{
    if (!ctx || !name || !ctx->current_scope)
    {
        return false;
    }

    // Search from current scope outward through parent scopes
    for (scope_t * scope = ctx->current_scope; scope; scope = scope->parent)
    {
        // Search backwards within this scope (most recent first)
        for (size_t i = scope->count; i > 0; --i)
        {
            if (scope->symbols[i - 1].name != NULL && strcmp(scope->symbols[i - 1].name, name) == 0)
            {
                if (out_ptr)
                    *out_ptr = scope->symbols[i - 1].ptr;
                if (out_type)
                    *out_type = scope->symbols[i - 1].type;
                if (out_pointee_type)
                    *out_pointee_type = scope->symbols[i - 1].pointee_type;
                return true;
            }
        }
    }
    return false;
}

static void register_struct_definition_with_name(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, char const * struct_name
);

static void register_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);

static void
register_structs_in_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (ctx == NULL || node == NULL)
    {
        return;
    }

    bool is_struct_declaration = false;

    if (node->type == AST_NODE_DECLARATION)
    {
        /* [ OptionalKwExtension DeclarationSpecifiers OptionalInitDeclaratorList ] */
        c_grammar_node_t const * decl_specs_node = node->data.list.children[1];

        for (size_t i = 0; i < decl_specs_node->data.list.count; ++i)
        {
            c_grammar_node_t * spec_child = decl_specs_node->data.list.children[i];

            if (spec_child->type == AST_NODE_TYPE_SPECIFIER && !spec_child->is_terminal_node)
            {
                for (size_t j = 0; j < spec_child->data.list.count; ++j)
                {
                    c_grammar_node_t const * type_child = spec_child->data.list.children[j];

                    if (type_child->type == AST_NODE_STRUCT_DEFINITION)
                    {
                        register_struct_definition(ctx, type_child);
                        is_struct_declaration = true;
                    }
                    else if (type_child->type == AST_NODE_ENUM_SPECIFIER)
                    {
                        register_enum_definition(ctx, type_child);
                        is_struct_declaration = true;
                    }
                }
            }
        }
    }
    else if (node->type == AST_NODE_TYPEDEF_DECLARATION)
    {
        // Handle TypedefDeclaration node: [DeclarationSpecifiers, Identifier]
        // DeclarationSpecifiers contains the struct/union/enum definition
        // Identifier is the typedef name
        if (node->data.list.count >= 2)
        {
            c_grammar_node_t * decl_specs = node->data.list.children[0];
            c_grammar_node_t * typedef_name_node = node->data.list.children[1];

            if (decl_specs && typedef_name_node && decl_specs->type == AST_NODE_DECL_SPECIFIERS
                && typedef_name_node->type == AST_NODE_IDENTIFIER && typedef_name_node->is_terminal_node)
            {
                char * typedef_name = typedef_name_node->data.terminal.text;
                c_grammar_node_t const * struct_def_node = NULL;
                c_grammar_node_t const * enum_def_node = NULL;

                // Look for struct/union/enum definition inside DeclarationSpecifiers
                for (size_t i = 0; i < decl_specs->data.list.count; ++i)
                {
                    c_grammar_node_t * spec_child = decl_specs->data.list.children[i];

                    if (spec_child && spec_child->type == AST_NODE_TYPE_SPECIFIER && !spec_child->is_terminal_node)
                    {
                        for (size_t j = 0; j < spec_child->data.list.count; ++j)
                        {
                            c_grammar_node_t const * type_child = spec_child->data.list.children[j];
                            if (type_child && type_child->type == AST_NODE_STRUCT_DEFINITION)
                            {
                                struct_def_node = type_child;
                                break;
                            }
                            else if (type_child && type_child->type == AST_NODE_ENUM_SPECIFIER)
                            {
                                enum_def_node = type_child;
                                break;
                            }
                        }
                    }
                    if (struct_def_node || enum_def_node)
                        break;
                }

                if (struct_def_node && typedef_name)
                {
                    register_struct_definition_with_name(ctx, struct_def_node, typedef_name);
                    is_struct_declaration = true;
                }
                else if (enum_def_node)
                {
                    // Register the enum values as constants
                    register_enum_definition(ctx, enum_def_node);
                    is_struct_declaration = true;
                }
            }
        }
    }

    if (!is_struct_declaration)
    {
        if (node->is_terminal_node)
        {
            register_structs_in_node(ctx, node->lhs);
            register_structs_in_node(ctx, node->rhs);
        }
        else
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                register_structs_in_node(ctx, node->data.list.children[i]);
            }
        }
    }
}

static void
register_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node)
{
    if (ctx == NULL || enum_node == NULL || enum_node->type != AST_NODE_ENUM_SPECIFIER)
    {
        return;
    }

    // EnumDefinition structure: [KwEnum, Identifier?, Enumerator, Enumerator, ...]
    // The enumerators contain the enum constant names and values

    // Enumerate values and register them as global constants
    int current_value = 0;

    for (size_t i = 1; i < enum_node->data.list.count; ++i)
    {
        c_grammar_node_t * child = enum_node->data.list.children[i];

        if (child->type == AST_NODE_ENUMERATOR && child->data.list.count >= 1)
        {
            // Enumerator = [Identifier] or [Identifier, Assign, IntegerLiteral]
            c_grammar_node_t * name_node = child->data.list.children[0];

            if (name_node && name_node->type == AST_NODE_IDENTIFIER && name_node->is_terminal_node)
            {
                char const * enum_name = name_node->data.terminal.text;

                // Check if there's an explicit value assignment
                // The enumerator could be [Identifier, Value] or [Identifier, Assign, Value]
                c_grammar_node_t * value_node = NULL;

                if (child->data.list.count == 2)
                {
                    // [Identifier, Value]
                    value_node = child->data.list.children[1];
                }
                else if (child->data.list.count >= 3)
                {
                    // [Identifier, Assign, Value]
                    value_node = child->data.list.children[2];
                }

                if (value_node)
                {
                    // Walk down the expression tree to find the integer literal
                    if (value_node->type == AST_NODE_INTEGER_LITERAL)
                    {
                        current_value = (int)value_node->integer_literal.value;
                    }
                    else if (value_node->lhs)
                    {
                        // Try lhs recursively for wrapped expressions
                        c_grammar_node_t * node = (c_grammar_node_t *)value_node;
                        while (node && node->lhs)
                        {
                            if (node->type == AST_NODE_INTEGER_LITERAL)
                            {
                                current_value = (int)node->integer_literal.value;
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
        else if (child->type == AST_NODE_IDENTIFIER && child->is_terminal_node)
        {
            // This might be the enum name, skip it
            continue;
        }
    }
}

static void
register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    if (ctx == NULL || type_child == NULL || type_child->type != AST_NODE_STRUCT_DEFINITION)
    {
        return;
    }

    char * struct_name = NULL;
    LLVMTypeRef * member_types = NULL;
    char ** member_names = NULL;
    size_t num_members = 0;

    for (size_t m = 0; m < type_child->data.list.count; ++m)
    {
        c_grammar_node_t * struct_child = type_child->data.list.children[m];

        if (struct_child->type == AST_NODE_IDENTIFIER && struct_child->is_terminal_node)
        {
            struct_name = struct_child->data.terminal.text;
            break;
        }
    }

    if (struct_name == NULL)
    {
        return;
    }

    if (find_struct_type(ctx, struct_name))
    {
        return;
    }

    // StructDefinition now has: [Keyword, Identifier?, TypeSpec, Declarator, TypeSpec, Declarator, ...]
    // Skip Keyword and optional Identifier to get to member pairs
    size_t start_idx = 2; // Skip Keyword and struct name
    // Check if child[1] is Identifier (struct name) - if so, start at 2, else 1
    if (type_child->data.list.count > 1 && type_child->data.list.children[1]
        && type_child->data.list.children[1]->type == AST_NODE_IDENTIFIER)
    {
        start_idx = 2;
    }
    else
    {
        start_idx = 1;
    }
    for (size_t m = start_idx; m + 1 < type_child->data.list.count; m += 2)
    {
        c_grammar_node_t * type_spec = type_child->data.list.children[m];
        c_grammar_node_t * decl = type_child->data.list.children[m + 1];
        if (type_spec && decl && type_spec->type == AST_NODE_TYPE_SPECIFIER && decl->type == AST_NODE_DECLARATOR)
        {
            // Get member type
            LLVMTypeRef member_type = map_type(ctx, type_spec, decl);

            // Get member name from declarator
            char * member_name = NULL;
            c_grammar_node_t * direct_decl = find_direct_declarator(decl);
            if (direct_decl && direct_decl->data.list.count > 0)
            {
                c_grammar_node_t * ident = direct_decl->data.list.children[0];
                if (ident && ident->type == AST_NODE_IDENTIFIER && ident->is_terminal_node)
                {
                    member_name = ident->data.terminal.text;
                }
            }

            LLVMTypeRef * new_types = realloc(member_types, (num_members + 1) * sizeof(LLVMTypeRef));
            char ** new_names = realloc(member_names, (num_members + 1) * sizeof(char *));
            if (new_types && new_names)
            {
                member_types = new_types;
                member_names = new_names;
                member_types[num_members] = member_type;
                member_names[num_members] = member_name ? strdup(member_name) : NULL;
                num_members++;
            }
        }
    }

    if (struct_name && num_members > 0 && member_types)
    {
        LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, struct_name);
        LLVMStructSetBody(struct_type, member_types, (unsigned)num_members, false);
        add_struct_type(ctx, struct_name, struct_type, member_names, member_types, num_members);
    }

    free(member_types);
    if (member_names)
    {
        for (size_t i = 0; i < num_members; i++)
        {
            free(member_names[i]);
        }
        free(member_names);
    }
}

static void
register_struct_definition_with_name(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, char const * struct_name
)
{
    if (ctx == NULL || type_child == NULL || type_child->type != AST_NODE_STRUCT_DEFINITION || struct_name == NULL)
    {
        return;
    }

    if (find_struct_type(ctx, struct_name))
    {
        return;
    }

    LLVMTypeRef * member_types = NULL;
    char ** member_names = NULL;
    size_t num_members = 0;

    // StructDefinition now has: [Keyword, Identifier?, TypeSpec, Declarator, TypeSpec, Declarator, ...]
    // Skip Keyword and optional Identifier to get to member pairs
    size_t start_idx = 2; // Skip Keyword and struct name
    // Check if child[1] is Identifier (struct name) - if so, start at 2, else 1
    if (type_child->data.list.count > 1 && type_child->data.list.children[1]
        && type_child->data.list.children[1]->type == AST_NODE_IDENTIFIER)
    {
        start_idx = 2;
    }
    else
    {
        start_idx = 1;
    }
    for (size_t m = start_idx; m + 1 < type_child->data.list.count; m += 2)
    {
        c_grammar_node_t * child0 = type_child->data.list.children[m];
        c_grammar_node_t * child1 = type_child->data.list.children[m + 1];

        // Look for TypeSpecifier followed by Declarator (in order)
        c_grammar_node_t * type_spec = NULL;
        c_grammar_node_t * decl = NULL;
        if (child0 && child0->type == AST_NODE_TYPE_SPECIFIER && child1 && child1->type == AST_NODE_DECLARATOR)
        {
            type_spec = child0;
            decl = child1;
        }

        if (type_spec && decl)
        {
            LLVMTypeRef member_type = map_type(ctx, type_spec, decl);

            char * member_name = NULL;
            c_grammar_node_t * direct_decl = find_direct_declarator(decl);
            if (direct_decl && direct_decl->data.list.count > 0)
            {
                c_grammar_node_t * ident = direct_decl->data.list.children[0];
                if (ident && ident->type == AST_NODE_IDENTIFIER && ident->is_terminal_node)
                {
                    member_name = ident->data.terminal.text;
                }
            }

            LLVMTypeRef * new_types = realloc(member_types, (num_members + 1) * sizeof(LLVMTypeRef));
            char ** new_names = realloc(member_names, (num_members + 1) * sizeof(char *));
            if (new_types && new_names)
            {
                member_types = new_types;
                member_names = new_names;
                member_types[num_members] = member_type;
                member_names[num_members] = member_name ? strdup(member_name) : NULL;
                num_members++;
            }
        }
    }

    if (struct_name && num_members > 0 && member_types)
    {
        LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, struct_name);
        LLVMStructSetBody(struct_type, member_types, (unsigned)num_members, false);
        add_struct_type(ctx, struct_name, struct_type, member_names, member_types, num_members);
    }

    free(member_types);
    if (member_names)
    {
        for (size_t i = 0; i < num_members; i++)
        {
            free(member_names[i]);
        }
        free(member_names);
    }
}

static struct_info_t *
find_struct_info(ir_generator_ctx_t * ctx, char const * name)
{
    if (!ctx || !name)
        return NULL;

    for (size_t i = 0; i < ctx->struct_count; ++i)
    {
        if (ctx->structs[i].name && strcmp(ctx->structs[i].name, name) == 0)
        {
            return &ctx->structs[i];
        }
    }
    return NULL;
}

static LLVMTypeRef
find_struct_type(ir_generator_ctx_t * ctx, char const * name)
{
    struct_info_t * info = find_struct_info(ctx, name);
    return info ? info->type : NULL;
}

static void
add_struct_type(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMTypeRef struct_type,
    char ** member_names,
    LLVMTypeRef * member_types,
    size_t num_members
)
{
    if (!ctx || !name || !struct_type)
        return;

    // Check if already exists
    if (find_struct_type(ctx, name))
        return;

    // Resize if needed
    if (ctx->struct_count >= ctx->struct_capacity)
    {
        size_t new_capacity = ctx->struct_capacity * 2;
        struct_info_t * new_structs = realloc(ctx->structs, new_capacity * sizeof(struct_info_t));
        if (!new_structs)
            return;
        ctx->structs = new_structs;
        ctx->struct_capacity = new_capacity;
    }

    ctx->structs[ctx->struct_count].name = strdup(name);
    ctx->structs[ctx->struct_count].type = struct_type;
    ctx->structs[ctx->struct_count].field_count = num_members;
    ctx->structs[ctx->struct_count].fields = NULL;

    if (num_members > 0 && member_names && member_types)
    {
        ctx->structs[ctx->struct_count].fields = calloc(num_members, sizeof(struct_field_t));
        for (size_t i = 0; i < num_members; i++)
        {
            ctx->structs[ctx->struct_count].fields[i].name = strdup(member_names[i]);
            ctx->structs[ctx->struct_count].fields[i].type = member_types[i];
        }
    }

    ctx->struct_count++;
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
static c_grammar_node_t *
find_direct_declarator(c_grammar_node_t * declarator)
{
    if (!declarator || declarator->type != AST_NODE_DECLARATOR)
        return NULL;

    for (size_t i = 0; i < declarator->data.list.count; ++i)
    {
        if (declarator->data.list.children[i]->type == AST_NODE_DIRECT_DECLARATOR)
        {
            return declarator->data.list.children[i];
        }
    }
    return NULL;
}

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
        if (specifiers->type == AST_NODE_TYPE_SPECIFIER && specifiers->is_terminal_node
            && specifiers->data.terminal.text)
        {
            char const * type_name = specifiers->data.terminal.text;
            // First check if it's a struct/union type
            LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
            if (struct_type)
            {
                base_type = struct_type;
            }
            // Then check for basic types
            else if (strncmp(type_name, "int", 3) == 0)
                base_type = LLVMInt32TypeInContext(ctx->context);
            else if (strncmp(type_name, "char", 4) == 0)
                base_type = LLVMInt8TypeInContext(ctx->context);
            else if (strncmp(type_name, "void", 4) == 0)
                base_type = LLVMVoidTypeInContext(ctx->context);
            else if (strncmp(type_name, "float", 5) == 0)
                base_type = LLVMFloatTypeInContext(ctx->context);
            else if (strncmp(type_name, "double", 6) == 0)
                base_type = LLVMDoubleTypeInContext(ctx->context);
            else if (strncmp(type_name, "long", 4) == 0)
                base_type = LLVMInt64TypeInContext(ctx->context);
            else if (strncmp(type_name, "short", 5) == 0)
                base_type = LLVMInt16TypeInContext(ctx->context);
            else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
                base_type = LLVMInt1TypeInContext(ctx->context);
        }
        // Handle DeclarationSpecifiers - extract TypeSpecifier from inside
        else if (specifiers->type == AST_NODE_DECL_SPECIFIERS)
        {
            for (size_t i = 0; i < specifiers->data.list.count; ++i)
            {
                c_grammar_node_t * child = specifiers->data.list.children[i];
                if (child && child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    // Found TypeSpecifier, process it
                    if (child->is_terminal_node && child->data.terminal.text)
                    {
                        char const * type_name = child->data.terminal.text;
                        LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                        if (struct_type)
                        {
                            base_type = struct_type;
                            break;
                        }
                        // Fallback: check for basic types
                        if (strncmp(type_name, "int", 3) == 0)
                            base_type = LLVMInt32TypeInContext(ctx->context);
                        else if (strncmp(type_name, "char", 4) == 0)
                            base_type = LLVMInt8TypeInContext(ctx->context);
                        else if (strncmp(type_name, "void", 4) == 0)
                            base_type = LLVMVoidTypeInContext(ctx->context);
                        else if (strncmp(type_name, "float", 5) == 0)
                            base_type = LLVMFloatTypeInContext(ctx->context);
                        else if (strncmp(type_name, "double", 6) == 0)
                            base_type = LLVMDoubleTypeInContext(ctx->context);
                        else if (strncmp(type_name, "long", 4) == 0)
                            base_type = LLVMInt64TypeInContext(ctx->context);
                        else if (strncmp(type_name, "short", 5) == 0)
                            base_type = LLVMInt16TypeInContext(ctx->context);
                        else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
                            base_type = LLVMInt1TypeInContext(ctx->context);
                        if (base_type)
                            break;
                    }
                    else if (!child->is_terminal_node && child->data.list.count > 0)
                    {
                        // Check children for typedef names or struct definitions
                        for (size_t j = 0; j < child->data.list.count; ++j)
                        {
                            c_grammar_node_t * tchild = child->data.list.children[j];
                            if (tchild && tchild->is_terminal_node && tchild->type == AST_NODE_IDENTIFIER)
                            {
                                char const * type_name = tchild->data.terminal.text;
                                LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                                if (struct_type)
                                {
                                    base_type = struct_type;
                                    break;
                                }
                            }
                        }
                        if (base_type)
                            break;
                    }
                }
            }
        }
        else if (specifiers->type == AST_NODE_TYPE_SPECIFIER)
        {
            if (specifiers->is_terminal_node)
            {
                char const * type_name = specifiers->data.terminal.text;
                if (strncmp(type_name, "int", 3) == 0)
                    base_type = LLVMInt32TypeInContext(ctx->context);
                else if (strncmp(type_name, "char", 4) == 0)
                    base_type = LLVMInt8TypeInContext(ctx->context);
                else if (strncmp(type_name, "void", 4) == 0)
                    base_type = LLVMVoidTypeInContext(ctx->context);
                else if (strncmp(type_name, "float", 5) == 0)
                    base_type = LLVMFloatTypeInContext(ctx->context);
                else if (strncmp(type_name, "double", 6) == 0)
                    base_type = LLVMDoubleTypeInContext(ctx->context);
                else if (strncmp(type_name, "long", 4) == 0)
                    base_type = LLVMInt64TypeInContext(ctx->context);
                else if (strncmp(type_name, "short", 5) == 0)
                    base_type = LLVMInt16TypeInContext(ctx->context);
                else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
                    base_type = LLVMInt1TypeInContext(ctx->context);
                else if (type_name && (strncmp(type_name, "struct ", 7) == 0 || strncmp(type_name, "union ", 6) == 0))
                {
                    char const * name_start = strchr(type_name, ' ');
                    if (name_start)
                    {
                        name_start++;
                        LLVMTypeRef st = find_struct_type(ctx, name_start);
                        if (st)
                            base_type = st;
                    }
                }
            }
            else
            {
                c_grammar_node_t const * type_specifier_node = specifiers;
                while (type_specifier_node && type_specifier_node->type == AST_NODE_TYPE_SPECIFIER
                       && !type_specifier_node->is_terminal_node)
                {
                    if (type_specifier_node->data.list.count == 1
                        && type_specifier_node->data.list.children[0]->type == AST_NODE_TYPE_SPECIFIER)
                    {
                        type_specifier_node = type_specifier_node->data.list.children[0];
                    }
                    else
                    {
                        break;
                    }
                }
                // Handle terminal type specifiers (e.g., typedef names like "IntFloat")
                if (type_specifier_node && type_specifier_node->is_terminal_node
                    && type_specifier_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * type_name = type_specifier_node->data.terminal.text;
                    LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                    if (struct_type)
                    {
                        base_type = struct_type;
                    }
                }
                else if (
                    type_specifier_node && !type_specifier_node->is_terminal_node
                    && type_specifier_node->data.list.count > 0
                )
                {
                    for (size_t i = 0; i < type_specifier_node->data.list.count; ++i)
                    {
                        c_grammar_node_t * child = type_specifier_node->data.list.children[i];
                        if (child && child->is_terminal_node
                            && (child->type == AST_NODE_IDENTIFIER || child->type == AST_NODE_INTEGER_BASE
                                || child->type == AST_NODE_FLOAT_BASE))
                        {
                            char const * type_name = child->data.terminal.text;
                            // Check if it's a registered struct type first
                            LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                            if (struct_type)
                            {
                                base_type = struct_type;
                            }
                            else if (strncmp(type_name, "int", 3) == 0)
                                base_type = LLVMInt32TypeInContext(ctx->context);
                            else if (strncmp(type_name, "char", 4) == 0)
                                base_type = LLVMInt8TypeInContext(ctx->context);
                            else if (strncmp(type_name, "void", 4) == 0)
                                base_type = LLVMVoidTypeInContext(ctx->context);
                            else if (strncmp(type_name, "float", 5) == 0)
                                base_type = LLVMFloatTypeInContext(ctx->context);
                            else if (strncmp(type_name, "double", 6) == 0)
                                base_type = LLVMDoubleTypeInContext(ctx->context);
                            else if (strncmp(type_name, "long", 4) == 0)
                                base_type = LLVMInt64TypeInContext(ctx->context);
                            else if (strncmp(type_name, "short", 5) == 0)
                                base_type = LLVMInt16TypeInContext(ctx->context);
                            else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
                                base_type = LLVMInt1TypeInContext(ctx->context);
                        }
                        else if (child && child->type == AST_NODE_STRUCT_DEFINITION)
                        {
                            register_struct_definition(ctx, child);
                            char const * name = NULL;
                            for (size_t si = 0; si < child->data.list.count; si++)
                            {
                                c_grammar_node_t * sc = child->data.list.children[si];
                                if (sc && sc->type == AST_NODE_IDENTIFIER && sc->is_terminal_node)
                                {
                                    name = sc->data.terminal.text;
                                    break;
                                }
                            }
                            if (name)
                            {
                                LLVMTypeRef st = find_struct_type(ctx, name);
                                if (st)
                                    base_type = st;
                            }
                        }
                        else if (child && child->type == AST_NODE_TYPE_SPECIFIER && !child->is_terminal_node)
                        {
                            // Nested TYPE_SPECIFIER - recurse to find the type
                            LLVMTypeRef nested = map_type(ctx, child, NULL);
                            if (nested)
                                base_type = nested;
                        }
                        else if (child && !child->is_terminal_node && child->data.list.children)
                        {
                            // Check if this is a StructTypeRef: first child is KwStruct terminal, second is Identifier
                            // Note: count may be 0 but children still exist in the array
                            c_grammar_node_t * first = child->data.list.children[0];
                            c_grammar_node_t * second = child->data.list.children[1];
                            if (first && first->is_terminal_node && second && second->is_terminal_node
                                && second->type == AST_NODE_IDENTIFIER)
                            {
                                LLVMTypeRef st = find_struct_type(ctx, second->data.terminal.text);
                                if (st)
                                    base_type = st;
                            }
                        }
                    }
                }
            }
        }
        else if (specifiers->type == AST_NODE_DECL_SPECIFIERS)
        {
            for (size_t i = 0; i < specifiers->data.list.count; ++i)
            {
                c_grammar_node_t * child = specifiers->data.list.children[i];
                if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    base_type = map_type(ctx, child, NULL);
                }
                else if (child->type == AST_NODE_DECL_SPECIFIERS)
                {
                    // Nested decl specifiers - recurse to find the type
                    LLVMTypeRef nested_type = map_type(ctx, child, NULL);
                    if (nested_type)
                        base_type = nested_type;
                }
                else if (child->type == AST_NODE_POINTER)
                {
                    pointer_level++;
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
        for (size_t i = 0; i < declarator->data.list.count; ++i)
        {
            c_grammar_node_t * child = declarator->data.list.children[i];
            if (child->type == AST_NODE_POINTER)
            {
                pointer_level++;
            }
            else if (child->type == AST_NODE_DIRECT_DECLARATOR)
            {
                // Check inside DirectDeclarator for pointers and arrays
                // The structure can be: DirectDeclarator -> Declarator -> {Pointer, ..., DeclaratorSuffix}
                for (size_t j = 0; j < child->data.list.count; ++j)
                {
                    c_grammar_node_t * direct_child = child->data.list.children[j];
                    if (direct_child->type == AST_NODE_POINTER)
                    {
                        pointer_level++;
                    }
                    else if (direct_child->type == AST_NODE_DECLARATOR)
                    {
                        // Nested declarator - check for pointers AND array suffixes inside
                        for (size_t k = 0; k < direct_child->data.list.count; ++k)
                        {
                            c_grammar_node_t * nested_child = direct_child->data.list.children[k];
                            if (nested_child->type == AST_NODE_POINTER)
                            {
                                pointer_level++;
                            }
                            else if (nested_child->type == AST_NODE_DECLARATOR_SUFFIX)
                            {
                                // Look for array size in nested declarator suffix
                                for (size_t m = 0; m < nested_child->data.list.count; ++m)
                                {
                                    c_grammar_node_t * nested_suffix = nested_child->data.list.children[m];
                                    if (nested_suffix->type == AST_NODE_INTEGER_LITERAL)
                                    {
                                        unsigned long long size_val = nested_suffix->integer_literal.value;
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
                    else if (direct_child->type == AST_NODE_DECLARATOR_SUFFIX)
                    {
                        // Look for array size in DirectDeclarator's suffix (for array of function pointers)
                        for (size_t m = 0; m < direct_child->data.list.count; ++m)
                        {
                            c_grammar_node_t * suffix_child = direct_child->data.list.children[m];
                            if (suffix_child->type == AST_NODE_INTEGER_LITERAL)
                            {
                                unsigned long long size_val = suffix_child->integer_literal.value;
                                if (array_depth < array_capacity)
                                {
                                    array_sizes[array_depth] = (size_t)size_val;
                                    array_depth++;
                                }
                            }
                        }
                    }
                    else if (direct_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                    {
                        // Function pointer parameter: int (*func)(int, int)
                        is_function_pointer = true;
                        for (size_t k = 0; k < direct_child->data.list.count; ++k)
                        {
                            c_grammar_node_t * fp_child = direct_child->data.list.children[k];
                            if (fp_child->type == AST_NODE_POINTER)
                            {
                                pointer_level++;
                            }
                            else if (fp_child->type == AST_NODE_DECLARATOR_SUFFIX)
                            {
                                // Check for array size inside FunctionPointerDeclarator (e.g., (*ops[2]))
                                for (size_t m = 0; m < fp_child->data.list.count; ++m)
                                {
                                    c_grammar_node_t * suffix_child = fp_child->data.list.children[m];
                                    if (suffix_child->type == AST_NODE_INTEGER_LITERAL)
                                    {
                                        unsigned long long size_val = suffix_child->integer_literal.value;
                                        if (array_depth < array_capacity)
                                        {
                                            array_sizes[array_depth] = (size_t)size_val;
                                            array_depth++;
                                        }
                                    }
                                }
                            }
                        }
                        // Continue processing to check for additional DeclaratorSuffix after FunctionPointerDeclarator
                        // (which contains array size like (*ops[2]))
                    }
                }
            }
            else if (child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
            {
                // FunctionPointerDeclarator contains: Pointer, Identifier, DeclaratorSuffix*
                // This includes both the pointer AND any array suffix (e.g., (*ops[2]))
                is_function_pointer = true;
                for (size_t j = 0; j < child->data.list.count; ++j)
                {
                    c_grammar_node_t * fp_child = child->data.list.children[j];
                    if (fp_child->type == AST_NODE_POINTER)
                    {
                        pointer_level++;
                    }
                    else if (fp_child->type == AST_NODE_DECLARATOR_SUFFIX)
                    {
                        // Check for array size (e.g., (*ops[2])(int, int))
                        for (size_t k = 0; k < fp_child->data.list.count; ++k)
                        {
                            c_grammar_node_t * suffix_child = fp_child->data.list.children[k];
                            if (suffix_child->type == AST_NODE_INTEGER_LITERAL)
                            {
                                unsigned long long size_val = suffix_child->integer_literal.value;
                                if (array_depth < array_capacity)
                                {
                                    array_sizes[array_depth] = (size_t)size_val;
                                    array_depth++;
                                }
                            }
                        }
                    }
                }
                // We've handled the function pointer declarator, don't process outer DeclaratorSuffix
                // which contains the function parameters - it's already accounted for
                break;
            }
            else if (child->type == AST_NODE_DECLARATOR_SUFFIX)
            {
                // Check if this is a function suffix (contains DeclarationSpecifiers for params)
                // vs array suffix (contains IntegerLiteral for size)
                bool has_function_params = false;
                bool has_array_size = false;

                for (size_t j = 0; j < child->data.list.count; ++j)
                {
                    c_grammar_node_t * suffix_child = child->data.list.children[j];
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
                        unsigned long long size_val = suffix_child->integer_literal.value;
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
        fprintf(stderr, "IRGen: Failed to allocate memory for context.\n");
        return NULL;
    }

    ctx->context = LLVMContextCreate();
    if (!ctx->context)
    {
        fprintf(stderr, "IRGen: Failed to create LLVM context.\n");
        free(ctx);
        return NULL;
    }

    ctx->module = LLVMModuleCreateWithName("c_compiler_module");
    if (!ctx->module)
    {
        fprintf(stderr, "IRGen: Failed to create LLVM module.\n");
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->builder = LLVMCreateBuilder();
    if (!ctx->builder)
    {
        fprintf(stderr, "IRGen: Failed to create LLVM builder.\n");
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    // Initialize with global scope
    ctx->current_scope = scope_create(NULL); // NULL parent = global scope
    if (!ctx->current_scope)
    {
        fprintf(stderr, "IRGen: Failed to create global scope.\n");
        LLVMDisposeBuilder(ctx->builder);
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    // Initialize struct type registry
    ctx->struct_capacity = 16;
    ctx->structs = calloc(ctx->struct_capacity, sizeof(struct_info_t));
    ctx->struct_count = 0;

    // Initialize label management
    ctx->label_capacity = 16;
    ctx->labels = calloc(ctx->label_capacity, sizeof(label_t));
    ctx->label_count = 0;

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

    free_symbol_table(ctx); // Free symbol table first
    free_labels(ctx);

    // Free struct type registry
    if (ctx->structs)
    {
        for (size_t i = 0; i < ctx->struct_count; ++i)
        {
            free(ctx->structs[i].name);
            if (ctx->structs[i].fields)
            {
                for (size_t j = 0; j < ctx->structs[i].field_count; j++)
                {
                    free(ctx->structs[i].fields[j].name);
                }
                free(ctx->structs[i].fields);
            }
        }
        free(ctx->structs);
    }

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
        fprintf(stderr, "IRGen: Invalid context or AST root provided.\n");
        return NULL;
    }

    register_structs_in_node(ctx, ast_root);

    process_ast_node(ctx, ast_root);

    return ctx->module;
}

// --- AST Node Processing Logic ---

/**
 * @brief Recursively processes AST nodes to generate LLVM IR.
 * This function dispatches to specific handlers based on the node type.
 */
static void
process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return;
    }

    fprintf(stderr, "%s node type: %s (%u)\n", __func__, get_node_type_name_from_node(node), node->type);

    switch (node->type)
    {
    case AST_NODE_TRANSLATION_UNIT:
    {
        // Process top-level declarations and function definitions.
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_FUNCTION_DEFINITION:
    {
        clear_labels(ctx);

        // Create function scope for parameters and body
        scope_push(ctx);

        // --- Handle Function Definition ---
        if (node->data.list.count != 3)
        {
            fprintf(stderr, "IRGen Error: Invalid function definition.\n");
            return;
        }
        c_grammar_node_t * decl_specifiers_node = node->data.list.children[0];
        c_grammar_node_t * declarator_node = node->data.list.children[1];
        c_grammar_node_t * compound_stmt_node = node->data.list.children[2];

        // --- Extract Function Name ---
        char * func_name = "unknown_function";
        c_grammar_node_t * suffix_node = NULL;

        c_grammar_node_t * direct_decl = find_direct_declarator(declarator_node);
        if (direct_decl && !direct_decl->is_terminal_node && direct_decl->data.list.count > 0
            && direct_decl->data.list.children[0]->type == AST_NODE_IDENTIFIER)
        {
            func_name = direct_decl->data.list.children[0]->data.terminal.text;
        }

        // Find parameter suffix
        for (size_t i = 0; i < declarator_node->data.list.count; ++i)
        {
            if (declarator_node->data.list.children[i]->type == AST_NODE_DECLARATOR_SUFFIX)
            {
                suffix_node = declarator_node->data.list.children[i];
                break;
            }
        }

        // --- Extract Parameters ---
        size_t num_params = 0;
        LLVMTypeRef * param_types = NULL;
        char ** param_names = NULL;
        LLVMTypeRef empty_params[1];

        if (suffix_node && !suffix_node->is_terminal_node && suffix_node->data.list.count > 0)
        {
            // Each parameter typically has [TypeSpecifier, Declarator]
            num_params = suffix_node->data.list.count / 2;
            param_types = calloc(num_params, sizeof(LLVMTypeRef));
            param_names = calloc(num_params, sizeof(char *));

            for (size_t i = 0; i < num_params; ++i)
            {
                c_grammar_node_t * p_spec = suffix_node->data.list.children[i * 2];
                c_grammar_node_t * p_decl = suffix_node->data.list.children[i * 2 + 1];

                param_types[i] = map_type(ctx, p_spec, p_decl);

                c_grammar_node_t * p_direct = find_direct_declarator(p_decl);
                if (p_direct && !p_direct->is_terminal_node && p_direct->data.list.count > 0)
                {
                    c_grammar_node_t * first_child = p_direct->data.list.children[0];
                    if (first_child->type == AST_NODE_IDENTIFIER)
                    {
                        param_names[i] = first_child->data.terminal.text;
                    }
                    else if (first_child->type == AST_NODE_DECLARATOR)
                    {
                        // Nested declarator (e.g., for function pointers like *name)
                        // Find the DirectDeclarator inside and get the Identifier
                        c_grammar_node_t * nested_direct = find_direct_declarator(first_child);
                        if (nested_direct && nested_direct->data.list.count > 0
                            && nested_direct->data.list.children[0]->type == AST_NODE_IDENTIFIER)
                        {
                            param_names[i] = nested_direct->data.list.children[0]->data.terminal.text;
                        }
                    }
                    else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                    {
                        // FunctionPointerDeclarator: contains Pointer, Identifier, DeclaratorSuffix*
                        for (size_t k = 0; k < first_child->data.list.count; ++k)
                        {
                            c_grammar_node_t * fp_child = first_child->data.list.children[k];
                            if (fp_child->type == AST_NODE_IDENTIFIER)
                            {
                                param_names[i] = fp_child->data.terminal.text;
                                break;
                            }
                        }
                    }
                }
            }
        }

        LLVMTypeRef return_type = map_type(ctx, decl_specifiers_node, NULL);
        LLVMTypeRef func_type
            = LLVMFunctionType(return_type, num_params > 0 ? param_types : empty_params, (unsigned)num_params, false);
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
                add_symbol(ctx, param_names[i], alloca_inst, param_types[i], NULL);
            }
        }

        free(param_types);
        free(param_names);

        // Process the compound statement (function body).
        process_ast_node(ctx, compound_stmt_node);

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

        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }

        // Pop block scope when exiting
        scope_pop(ctx);
        break;
    }
    case AST_NODE_EXPRESSION_STATEMENT:
    {
        if (node->data.list.children)
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_expression(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
    case AST_NODE_DECLARATION:
    {
        /* [ OptionalKwExtension DeclarationSpecifiers OptionalInitDeclaratorList ] */
        // --- Handle Variable Declarations ---

        // First, check if this is a struct definition - this is now handled in register_structs_in_node

        c_grammar_node_t * decl_specifiers = node->data.list.children[1];
        c_grammar_node_t * init_decl_nodes = node->data.list.children[2];

        // Process InitDeclarators to create variables and initialize them.
        for (size_t i = 0; i < init_decl_nodes->data.list.count; ++i)
        {
            c_grammar_node_t * init_decl_node = init_decl_nodes->data.list.children[i];

            char * var_name = NULL;
            c_grammar_node_t * initializer_expr_node = NULL; // Node representing the initializer expression.
            c_grammar_node_t * declarator_node = init_decl_node->data.list.children[0];
            c_grammar_node_t * direct_decl_node = find_direct_declarator(declarator_node);

            // For regular variables: DirectDeclarator -> Identifier
            // For function pointers: DirectDeclarator -> FunctionPointerDeclarator -> {Pointer, Identifier,
            // DeclaratorSuffix*}
            if (direct_decl_node && direct_decl_node->data.list.count > 0)
            {
                c_grammar_node_t * first_child = direct_decl_node->data.list.children[0];
                if (first_child->type == AST_NODE_IDENTIFIER)
                {
                    var_name = first_child->data.terminal.text;
                }
                else if (first_child->type == AST_NODE_DECLARATOR)
                {
                    // Nested declarator (e.g., for function pointers like *name)
                    // Find the DirectDeclarator inside and get the Identifier
                    c_grammar_node_t * nested_direct = find_direct_declarator(first_child);
                    if (nested_direct && nested_direct->data.list.count > 0
                        && nested_direct->data.list.children[0]->type == AST_NODE_IDENTIFIER)
                    {
                        var_name = nested_direct->data.list.children[0]->data.terminal.text;
                    }
                }
                else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                {
                    // FunctionPointerDeclarator contains Pointer, Identifier, DeclaratorSuffix*
                    for (size_t k = 0; k < first_child->data.list.count; ++k)
                    {
                        c_grammar_node_t * fp_child = first_child->data.list.children[k];
                        if (fp_child->type == AST_NODE_IDENTIFIER)
                        {
                            var_name = fp_child->data.terminal.text;
                            break;
                        }
                    }
                }
            }

            LLVMTypeRef var_type = map_type(ctx, decl_specifiers, declarator_node);

            // Find initializer - child[0] is the declarator, rest are initializers
            for (size_t ci = 1; ci < init_decl_node->data.list.count; ci++)
            {
                c_grammar_node_t * child = init_decl_node->data.list.children[ci];

                // Skip declarator nodes - anything else is an initializer
                if (child && child->type != AST_NODE_DECLARATOR)
                {
                    initializer_expr_node = child;
                    break;
                }
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
                        // Iterate through DeclarationSpecifiers children
                        for (size_t si = 0; si < decl_specifiers->data.list.count && !struct_name; si++)
                        {
                            c_grammar_node_t * child = decl_specifiers->data.list.children[si];

                            // Handle terminal TypeSpecifier (typedef name like "FloatMember")
                            if (child && child->type == AST_NODE_TYPE_SPECIFIER && child->is_terminal_node
                                && child->data.terminal.text)
                            {
                                if (find_struct_type(ctx, child->data.terminal.text))
                                {
                                    struct_name = child->data.terminal.text;
                                }
                            }
                            // Handle non-terminal TypeSpecifier
                            else if (child && child->type == AST_NODE_TYPE_SPECIFIER && !child->is_terminal_node)
                            {
                                for (size_t ssi = 0; ssi < child->data.list.count && !struct_name; ssi++)
                                {
                                    c_grammar_node_t * ssc = child->data.list.children[ssi];
                                    if (ssc && ssc->is_terminal_node && ssc->type == AST_NODE_IDENTIFIER)
                                    {
                                        if (find_struct_type(ctx, ssc->data.terminal.text))
                                            struct_name = ssc->data.terminal.text;
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
                            for (size_t di = 0; di < declarator_node->data.list.count; di++)
                            {
                                c_grammar_node_t * dc = declarator_node->data.list.children[di];
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
                                    for (size_t i = 0; i < initializer_expr_node->data.list.count; ++i)
                                    {
                                        c_grammar_node_t * child = initializer_expr_node->data.list.children[i];
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
                                if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind
                                    && LLVMGetTypeKind(var_type) == LLVMIntegerTypeKind)
                                {
                                    unsigned init_bits = LLVMGetIntTypeWidth(init_type);
                                    unsigned var_bits = LLVMGetIntTypeWidth(var_type);
                                    if (init_bits > var_bits)
                                    {
                                        initializer_value
                                            = LLVMBuildTrunc(ctx->builder, initializer_value, var_type, "trunc_init");
                                    }
                                    else if (init_bits < var_bits)
                                    {
                                        initializer_value
                                            = LLVMBuildSExt(ctx->builder, initializer_value, var_type, "sext_init");
                                    }
                                }

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
                        for (size_t si = 0; si < declarator_node->data.list.count; si++)
                        {
                            c_grammar_node_t * suf = declarator_node->data.list.children[si];
                            if (suf && suf->type == AST_NODE_DECLARATOR_SUFFIX && suf->data.list.count > 0)
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
                        char * raw_text = initializer_expr_node->data.terminal.text;
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
                char const * base_name = base_node->data.terminal.text;
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
            fprintf(stderr, "IRGen Error: Could not get pointer for LHS in assignment.\n");
            return;
        }

        // Process the RHS expression to get its LLVM ValueRef.
        LLVMValueRef rhs_value = process_expression(ctx, rhs_node);
        if (!rhs_value)
        {
            fprintf(stderr, "IRGen Error: Failed to process RHS expression in assignment.\n");
            return;
        }

        // Convert RHS to LHS type if needed (e.g., i32 literal to i8 char)
        if (lhs_type != NULL && rhs_value != NULL)
        {
            LLVMTypeRef rhs_type = LLVMTypeOf(rhs_value);
            if (lhs_type != rhs_type && LLVMGetTypeKind(lhs_type) == LLVMIntegerTypeKind
                && LLVMGetTypeKind(rhs_type) == LLVMIntegerTypeKind)
            {
                unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
                unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
                if (lhs_bits < rhs_bits)
                {
                    rhs_value = LLVMBuildTrunc(ctx->builder, rhs_value, lhs_type, "trunc_rhs");
                }
                else if (lhs_bits > rhs_bits)
                {
                    rhs_value = LLVMBuildSExt(ctx->builder, rhs_value, lhs_type, "sext_rhs");
                }
            }
        }

        // Generate the store instruction.
        aligned_store(ctx->builder, rhs_value, lhs_ptr);
        break;
    }
    case AST_NODE_FOR_STATEMENT:
    {
        // AST structure for ForStatement: [InitExpr/Decl, CondExpr, PostExpr, BodyStatement]
        if (node->data.list.count < 4)
        {
            fprintf(stderr, "IRGen Error: Invalid ForStatement AST node.\n");
            return;
        }

        c_grammar_node_t * init_node = node->data.list.children[0];
        c_grammar_node_t * cond_node = node->data.list.children[1];
        c_grammar_node_t * post_node = node->data.list.children[2];
        c_grammar_node_t * body_node = node->data.list.children[3];

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
        LLVMBuildBr(ctx->builder, cond_block);

        // 2. Emit Cond block
        LLVMPositionBuilderAtEnd(ctx->builder, cond_block);
        LLVMValueRef cond_val = process_expression(ctx, cond_node);
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
        // AST structure for WhileStatement: [ConditionExpression, BodyStatement]
        if (node->data.list.count < 2)
        {
            fprintf(stderr, "IRGen Error: Invalid WhileStatement AST node.\n");
            return;
        }

        c_grammar_node_t * condition_node = node->data.list.children[0];
        c_grammar_node_t * body_node = node->data.list.children[1];

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
            fprintf(stderr, "IRGen Error: Failed to process condition for WhileStatement.\n");
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
        // AST structure for DoWhileStatement: [BodyStatement, ConditionExpression]
        if (node->data.list.count < 2)
        {
            fprintf(stderr, "IRGen Error: Invalid DoWhileStatement AST node.\n");
            return;
        }

        c_grammar_node_t * body_node = node->data.list.children[0];
        c_grammar_node_t * condition_node = node->data.list.children[1];

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
            fprintf(stderr, "IRGen Error: Failed to process condition for DoWhileStatement.\n");
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
        if (node->data.list.count == 2)
        {
            c_grammar_node_t * stmt = node->data.list.children[1];
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
            fprintf(stderr, "IRGen Error: break statement not within a loop or switch.\n");
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
            fprintf(stderr, "IRGen Error: continue statement not within a loop.\n");
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
        if (node->data.list.count < 2)
        {
            fprintf(stderr, "IRGen Error: Invalid SwitchStatement AST node.\n");
            return;
        }

        c_grammar_node_t * switch_expr = node->data.list.children[0];
        c_grammar_node_t * body_stmt = node->data.list.children[1];

        LLVMValueRef switch_val = process_expression(ctx, switch_expr);
        if (!switch_val)
        {
            fprintf(stderr, "IRGen Error: Failed to process switch expression.\n");
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
            c_grammar_node_t * node;
            LLVMBasicBlockRef body_block;
        } switch_item_t;

        size_t num_items = 0;
        size_t items_capacity = 16;
        switch_item_t * items = malloc(items_capacity * sizeof(*items));

        size_t default_idx = SIZE_MAX;

        if (body_stmt && body_stmt->type == AST_NODE_COMPOUND_STATEMENT)
        {
            for (size_t i = 0; i < body_stmt->data.list.count; ++i)
            {
                c_grammar_node_t * child = body_stmt->data.list.children[i];

                if (child->type == AST_NODE_SWITCH_CASE)
                {
                    if (num_items >= items_capacity)
                    {
                        items_capacity *= 2;
                        items = realloc(items, items_capacity * sizeof(switch_item_t));
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
                        items = realloc(items, items_capacity * sizeof(switch_item_t));
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
            c_grammar_node_t * item_node = items[i].node;
            bool has_statements = false;

            if (items[i].is_default)
            {
                has_statements = (item_node->data.list.count > 0);
            }
            else
            {
                // SwitchCase: children are [case_label*, statement*]
                // Statements start after all case labels
                has_statements = false;
                for (size_t j = 0; j < item_node->data.list.count; j++)
                {
                    if (item_node->data.list.children[j]->type != AST_NODE_CASE_LABEL)
                    {
                        has_statements = true;
                        break;
                    }
                }
            }

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
                for (size_t j = 0; j < items[i].node->data.list.count; j++)
                {
                    if (items[i].node->data.list.children[j]->type == AST_NODE_CASE_LABEL)
                    {
                        num_case_values++;
                    }
                }
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
            c_grammar_node_t * switch_case_node = items[i].node;
            for (size_t j = 0; j < switch_case_node->data.list.count; j++)
            {
                c_grammar_node_t * child = switch_case_node->data.list.children[j];
                if (child->type == AST_NODE_CASE_LABEL)
                {
                    // CaseLabel contains the case expression
                    if (child->data.list.count >= 1)
                    {
                        LLVMValueRef case_val = process_expression(ctx, child->data.list.children[0]);
                        LLVMAddCase(
                            switch_inst, case_val, items[i].body_block ? items[i].body_block : fallthrough_target
                        );
                    }
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
            c_grammar_node_t * item_node = items[i].node;
            for (size_t j = 0; j < item_node->data.list.count; j++)
            {
                c_grammar_node_t * child = item_node->data.list.children[j];
                if (child->type != AST_NODE_CASE_LABEL)
                {
                    process_ast_node(ctx, child);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                    {
                        break;
                    }
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
        if (node->data.list.count < 2)
        {
            fprintf(stderr, "IRGen Error: Invalid IfStatement AST node.\n");
            return;
        }

        c_grammar_node_t * condition_node = node->data.list.children[0];
        c_grammar_node_t * then_node = node->data.list.children[1];
        c_grammar_node_t * else_node = (node->data.list.count > 2) ? node->data.list.children[2] : NULL;

        LLVMValueRef condition_val = process_expression(ctx, condition_node);
        if (!condition_val)
        {
            fprintf(stderr, "IRGen Error: Failed to process condition for IfStatement.\n");
            return;
        }

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "then");
        LLVMBasicBlockRef else_block
            = else_node ? LLVMAppendBasicBlockInContext(ctx->context, current_func, "else") : NULL;
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
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        {
            LLVMBuildBr(ctx->builder, merge_block);
        }

        // --- Emit 'else' block if present ---
        if (else_node)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, else_block);
            process_ast_node(ctx, else_node);
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
        // Handle 'return expression;' or 'return;'.
        c_grammar_node_t const * expr_node = node->lhs;
        if (expr_node != NULL)
        {
            // Process the return expression.
            LLVMValueRef return_value = process_expression(ctx, expr_node);

            if (return_value)
            {
                LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
                LLVMTypeRef func_ret_type = LLVMGetReturnType(LLVMGlobalGetValueType(parent_func));

                // Get the type of the expression you just processed
                LLVMTypeRef expr_type = LLVMTypeOf(return_value);

                // Logic to adjust bit width (assuming Integer types)
                if (LLVMGetTypeKind(expr_type) == LLVMIntegerTypeKind
                    && LLVMGetTypeKind(func_ret_type) == LLVMIntegerTypeKind)
                {

                    unsigned expr_width = LLVMGetIntTypeWidth(expr_type);
                    unsigned func_width = LLVMGetIntTypeWidth(func_ret_type);

                    if (expr_width < func_width)
                    {
                        // e.g., i1 -> i32 (Bool to Int)
                        fprintf(stderr, "extending\n");
                        return_value = LLVMBuildZExt(ctx->builder, return_value, func_ret_type, "zext_tmp");
                    }
                    else if (expr_width > func_width)
                    {
                        // e.g., i32 -> i1 (Int to Bool)
                        fprintf(stderr, "truncating\n");
                        return_value = LLVMBuildTrunc(ctx->builder, return_value, func_ret_type, "trunc_tmp");
                    }
                }

                LLVMBuildRet(ctx->builder, return_value);
            }
            else
            {
                fprintf(stderr, "IRGen Error: Failed to process return expression.\n");
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
        char const * label_name = node->data.list.children[0]->data.terminal.text;
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
        // LabeledStatement children: [LabeledIdentifier] or [Identifier, Statement] (legacy)
        // LabeledIdentifier children: [Identifier, Statement]

        c_grammar_node_t const * label_id_node = NULL;
        if (node->data.list.count >= 1 && node->data.list.children[0]->type == AST_NODE_LABELED_IDENTIFIER)
        {
            // New format: children[0] is LabeledIdentifier
            label_id_node = node->data.list.children[0];
        }
        else if (node->data.list.count >= 2 && node->data.list.children[0]->type == AST_NODE_IDENTIFIER)
        {
            // Legacy format: children[0] is Identifier, children[1] is Statement
            label_id_node = node;
        }

        if (label_id_node != NULL)
        {
            c_grammar_node_t * identifier_node = label_id_node->data.list.children[0];
            c_grammar_node_t * statement_node
                = label_id_node->data.list.count >= 2 ? label_id_node->data.list.children[1] : NULL;

            if (identifier_node->type == AST_NODE_IDENTIFIER && statement_node != NULL)
            {
                char const * label_name = identifier_node->data.terminal.text;
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
            }
        }
        break;
    }
    case AST_NODE_POSTFIX_PARTS:
    case AST_NODE_STRUCT_DEFINITION:
    {
        /* Probably a bug to see these nodes at this level. */
        fprintf(stderr, "Ignoring AST node type %s\n", get_node_type_name_from_type(node->type));
        break;
    }
        // --- Add cases for other AST_NODE types ---
        // Examples: AST_NODE_BINARY_OP, AST_NODE_UNARY_OP, AST_NODE_FUNCTION_CALL,
        // AST_NODE_IF_STATEMENT, AST_NODE_WHILE_STATEMENT, AST_NODE_FOR_STATEMENT, etc.
        // Each requires specific LLVM IR generation logic.

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
    case AST_NODE_LABELED_IDENTIFIER:
    case AST_NODE_ASSIGNMENT_OPERATOR:
    case AST_NODE_INTEGER_BASE:
    case AST_NODE_INIT_DECLARATOR:
    case AST_NODE_OPTIONAL_KW_EXTENSION:
    case AST_NODE_OPTIONAL_INIT_DECLARATOR_LIST:
    case AST_NODE_TYPEDEF_DECLARATION:
    case AST_NODE_KEYWORD:
    case AST_NODE_TERNARY_OPERATION:
    case AST_NODE_ENUM_SPECIFIER:
    case AST_NODE_ENUMERATOR:
    case AST_NODE_COMMA_EXPRESSION:
    case AST_NODE_CONDITIONAL_EXPRESSION:
    case AST_NODE_FUNCTION_POINTER_DECLARATOR:
    default:
        // Fallback: Recursively process children for unhandled node types.
        if (node->is_terminal_node)
        {
            /*
                Do nothing for terminal nodes unless handled above.
                Shouldn't happen.
             */
            fprintf(stderr, "Unhandled terminal node type: %d (%s)\n", node->type, node->data.terminal.text);
        }
        else
        {
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                process_ast_node(ctx, node->data.list.children[i]);
            }
        }
        break;
    }
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
        fprintf(stderr, "IRGen Error: Invalid module or file path for writing IR.\n");
        return -1;
    }

    char * error_message = NULL;
    // LLVMPrintModuleToFile writes human-readable IR.
    if (LLVMPrintModuleToFile(module, file_path, &error_message))
    {
        fprintf(stderr, "IRGen Error: Failed to write LLVM IR to file '%s': %s\n", file_path, error_message);
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
        fprintf(stderr, "IRGen Error: Invalid module or file path for emission.\n");
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
        fprintf(stderr, "IRGen Error: Failed to get target from triple '%s': %s\n", triple, error);
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
        fprintf(stderr, "IRGen Error: Failed to create target machine.\n");
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
        fprintf(stderr, "IRGen Error: Failed to emit file '%s': %s\n", file_path, error);
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

// --- Placeholder for Symbol Table Management ---
// This is a crucial part of IR generation and needs to be robust.
// For this example, it's conceptual. A real implementation would use a hash map.

// Example of a conceptual symbol table entry
// typedef struct symbol_table_entry {
//     char* name;
//     LLVMValueRef ptr; // Pointer to the allocated variable (or function reference)
//     LLVMTypeRef type;
//     // Potentially scope information
// } symbol_table_entry_t;

// Placeholder function to add a symbol to the symbol table.
// static void add_symbol(ir_generator_ctx_t* ctx, const char* name, LLVMValueRef ptr, LLVMTypeRef type) {
//     // TODO: Implement symbol table addition logic.
//     fprintf(stderr, "IRGen: Conceptual add_symbol for '%s'\n", name);
// }

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
    if (!identifier_node || identifier_node->type != AST_NODE_IDENTIFIER || !identifier_node->data.terminal.text)
    {
        fprintf(stderr, "IRGen Error: Invalid identifier node for get_variable_pointer.\n");
        return NULL;
    }
    char const * name = identifier_node->data.terminal.text;

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
process_integer_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    LLVMTypeRef int_type;
    if (node->integer_literal.is_long)
    {
        int_type = LLVMInt64TypeInContext(ctx->context);
    }
    else
    {
        int_type = LLVMInt32TypeInContext(ctx->context);
    }

    return LLVMConstInt(int_type, node->integer_literal.value, !node->integer_literal.is_unsigned);
}

static LLVMValueRef
process_float_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    LLVMTypeRef float_type = NULL;
    long double value = node->float_literal.value;
    if (node->float_literal.type == FLOAT_LITERAL_TYPE_DOUBLE)
    {
        float_type = LLVMDoubleTypeInContext(ctx->context);
    }
    else if (node->float_literal.type == FLOAT_LITERAL_TYPE_FLOAT)
    {
        float_type = LLVMFloatTypeInContext(ctx->context);
        value = (double)value;
    }
    else if (node->float_literal.type == FLOAT_LITERAL_TYPE_LONG_DOUBLE)
    {
        float_type = LLVMX86FP80TypeInContext(ctx->context);
    }

    return LLVMConstReal(float_type, value);
}

static LLVMValueRef
process_string_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Handle string literals like "Hello".
    if (node->data.terminal.text == NULL)
    {
        return NULL;
    }

    char * raw_text = node->data.terminal.text;
    char * decoded = decode_string(raw_text);
    LLVMValueRef global_str = LLVMBuildGlobalStringPtr(ctx->builder, decoded, "str_tmp");
    free(decoded);

    return global_str;
}

static LLVMValueRef
process_character_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node->data.terminal.text == NULL)
    {
        return NULL;
    }

    char * raw_text = node->data.terminal.text;
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
        char const * var_name = base_node->data.terminal.text;
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

        for (size_t i = 0; i < postfix_node->data.list.count; ++i)
        {
            if (postfix_node->data.list.children[i]->type == AST_NODE_FUNCTION_CALL)
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

    for (size_t i = 0; i < postfix_node->data.list.count; ++i)
    {
        c_grammar_node_t * suffix = postfix_node->data.list.children[i];
        if (suffix->type == AST_NODE_FUNCTION_CALL)
        {
            // Handle function call. Arguments might be children directly or in an ArgumentList
            size_t num_args = 0;
            LLVMValueRef * args = NULL;

            if (suffix->data.list.count > 0)
            {
                num_args = suffix->data.list.count;
                args = malloc(num_args * sizeof(*args));
                for (size_t j = 0; j < num_args; ++j)
                {
                    args[j] = process_expression(ctx, suffix->data.list.children[j]);
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
                    char const * func_name = base_node->data.terminal.text;

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
                    fprintf(stderr, "IRGen Error: Could not resolve function for call.\n");
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
                    fprintf(stderr, "IRGen Error: Could not process array subscript.\n");
                    return NULL;
                }
            }
        }
        else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
        {
            // Struct member access: s.x or p->x
            // AST_MEMBER_ACCESS_DOT/ARROW children: [Dot/Arrow, Identifier]
            /* The one and only child is an IDENTIFIER node. */
            c_grammar_node_t * child = suffix->data.list.children[0];
            char * member_name = child->data.terminal.text;

            if (base_val)
            {
                // Get the struct type
                LLVMTypeRef base_type = LLVMTypeOf(base_val);
                if (!base_type)
                {
                    fprintf(stderr, "IRGen Error: NULL type for member access base.\n");
                    continue;
                }
                LLVMTypeRef struct_type;
                bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);

                // For LLVM 18+ opaque pointers, use struct name from symbol table
                struct_type = NULL;
                if (is_arrow && base_node && base_node->type == AST_NODE_IDENTIFIER)
                {
                    char const * sname = find_symbol_struct_name(ctx, base_node->data.terminal.text);
                    if (sname)
                        struct_type = find_struct_type(ctx, sname);
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
                    fprintf(stderr, "IRGen Error: Could not find struct type for member access.\n");
                    continue;
                }

                // Find the member index by name
                unsigned num_elements = LLVMCountStructElementTypes(struct_type);
                unsigned member_index = 0;
                bool found = false;

                // Look up the struct info to find the member by name
                struct_info_t * struct_info = NULL;
                for (size_t si = 0; si < ctx->struct_count; si++)
                {
                    if (ctx->structs[si].type == struct_type)
                    {
                        struct_info = &ctx->structs[si];
                        break;
                    }
                }

                if (struct_info && struct_info->fields)
                {
                    for (unsigned j = 0; j < num_elements && j < struct_info->field_count; ++j)
                    {
                        if (struct_info->fields[j].name && strcmp(struct_info->fields[j].name, member_name) == 0)
                        {
                            member_index = j;
                            found = true;
                            break;
                        }
                    }
                }
                else
                {
                    // Fallback: assume members are in order and match the field_count
                    for (unsigned j = 0; j < num_elements; ++j)
                    {
                        if (j == 0)
                        {
                            member_index = j;
                            found = true;
                            break;
                        }
                    }
                }

                if (found || num_elements > 0)
                {
                    // Create GEP to access member
                    LLVMValueRef indices[2];
                    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), member_index, false);

                    LLVMValueRef member_ptr;
                    if (is_arrow || LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
                    {
                        member_ptr = LLVMBuildInBoundsGEP2(ctx->builder, base_type, base_val, indices, 2, "memberptr");
                    }
                    else
                    {
                        // For value types, we need to get the pointer
                        LLVMValueRef struct_ptr = LLVMBuildAlloca(ctx->builder, struct_type, "struct_tmp");
                        aligned_store(ctx->builder, base_val, struct_ptr);
                        member_ptr
                            = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr");
                    }
                    LLVMTypeRef member_type = LLVMStructGetTypeAtIndex(struct_type, member_index);
                    base_val = aligned_load(ctx->builder, member_type, member_ptr, "member");
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
            fprintf(stderr, "IRGen Warning: Unhandled postfix suffix type %u\n", suffix->type);
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
    if (node->data.list.count < 2)
    {
        return NULL;
    }

    c_grammar_node_t const * type_name_node = node->data.list.children[0];
    c_grammar_node_t const * inner_expr_node = node->data.list.children[1];

    if (type_name_node->type != AST_NODE_TYPE_NAME)
    {
        fprintf(stderr, "IRGen Error: Expected TypeName in cast expression, got %u\n", type_name_node->type);
        return NULL;
    }

    // TypeName children are [SpecifierQualifierList, AbstractDeclarator?]
    c_grammar_node_t * spec_qual = type_name_node->data.list.children[0];
    c_grammar_node_t * abstract_decl
        = (type_name_node->data.list.count > 1) ? type_name_node->data.list.children[1] : NULL;

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

    // Check if LHS is a PostfixExpression with array subscript or member access
    if (lhs_node->type == AST_NODE_POSTFIX_EXPRESSION)
    {
        c_grammar_node_t const * base_node = lhs_node->lhs;
        if (base_node->type == AST_NODE_IDENTIFIER)
        {
            char const * base_name = base_node->data.terminal.text;
            LLVMValueRef base_ptr;
            LLVMTypeRef base_type;
            if (find_symbol(ctx, base_name, &base_ptr, &base_type, NULL))
            {
                LLVMValueRef current_ptr = base_ptr;
                LLVMTypeRef current_type = base_type;
                c_grammar_node_t const * postfix_node = lhs_node->rhs;

                // Process suffixes to handle array subscripts and member access
                for (size_t i = 0; i < postfix_node->data.list.count; ++i)
                {
                    c_grammar_node_t * suffix = postfix_node->data.list.children[i];

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
                        c_grammar_node_t * member_node = suffix->data.list.children[0];
                        char * member_name = member_node->data.terminal.text;

                        if (current_ptr && current_type)
                        {
                            LLVMTypeRef struct_type = NULL;

                            // For LLVM 18+ opaque pointers, use struct name from symbol table
                            char const * sname = find_symbol_struct_name(ctx, base_name);
                            if (sname != NULL)
                            {
                                struct_type = find_struct_type(ctx, sname);
                            }
                            // Fallback for opaque pointers
                            if (struct_type == NULL && LLVMGetTypeKind(current_type) == LLVMPointerTypeKind)
                            {
                                struct_type = get_pointer_element_type(ctx, current_type);
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
                                struct_info_t * info = NULL;

                                for (size_t si = 0; si < ctx->struct_count; si++)
                                {
                                    if (ctx->structs[si].type == struct_type)
                                    {
                                        info = &ctx->structs[si];
                                        break;
                                    }
                                }

                                if (info != NULL)
                                {
                                    for (unsigned j = 0; j < num_elements && j < info->field_count; j++)
                                    {
                                        if (info->fields[j].name != NULL
                                            && strcmp(info->fields[j].name, member_name) == 0)
                                        {
                                            member_index = j;
                                            break;
                                        }
                                    }
                                }

                                LLVMValueRef indices[2];
                                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                                indices[1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), member_index, false);
                                current_ptr = LLVMBuildInBoundsGEP2(
                                    ctx->builder, struct_type, struct_ptr, indices, 2, "memberptr"
                                );
                                current_type = LLVMStructGetTypeAtIndex(struct_type, member_index);
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
        fprintf(stderr, "IRGen Error: Could not get pointer for LHS in assignment.\n");
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
            fprintf(stderr, "IRGen Error: Failed to process RHS expression in compound assignment.\n");
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
            fprintf(stderr, "IRGen Error: Unknown compound assignment operator.\n");
            return NULL;
        }
    }
    else
    {
        // Process the RHS expression to get its LLVM ValueRef.
        rhs_value = process_expression(ctx, rhs_node);
        if (!rhs_value)
        {
            fprintf(stderr, "IRGen Error: Failed to process RHS expression in assignment.\n");
            return NULL;
        }
    }

    // Generate the store instruction.
    aligned_store(ctx->builder, rhs_value, lhs_ptr);
    return rhs_value;
}

static LLVMValueRef
process_identifier(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Handle built-in boolean constants
    if (node && node->data.terminal.text != NULL)
    {
        if (strcmp(node->data.terminal.text, "true") == 0)
        {
            return LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, false);
        }
        if (strcmp(node->data.terminal.text, "false") == 0)
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
        LLVMValueRef func_val = LLVMGetNamedFunction(ctx->module, node->data.terminal.text);
        if (func_val != NULL)
        {
            return func_val;
        }
        fprintf(stderr, "IRGen Error: Undefined variable '%s' used.\n", node->data.terminal.text);
        return NULL;
    }

    fprintf(stderr, "IRGen Error: NULL element type for variable '%s'.\n", node->data.terminal.text);
    return NULL;
}

static LLVMValueRef
process_bitwise_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Bitwise ops from chainl1: [LHS, RHS], operator is implied by node type
    LLVMValueRef lhs_val = process_expression(ctx, node->lhs);
    LLVMValueRef rhs_val = process_expression(ctx, node->rhs);

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
        fprintf(stderr, "IRGen Error: Invalid conditional expression\n");
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
            if (find_symbol(ctx, operand_node->data.terminal.text, &var_ptr, &var_type, NULL))
            {
                return var_ptr;
            }
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
            if (find_symbol(ctx, operand_node->data.terminal.text, &var_ptr, &var_type, &pointee_type))
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
            for (size_t i = 0; i < operand_node->data.list.count && target_type == NULL; i++)
            {
                c_grammar_node_t * child = operand_node->data.list.children[i];

                // Skip struct/union keywords
                if (child->type == AST_NODE_KEYWORD)
                {
                    continue;
                }

                // Handle terminal type specifier (e.g., "int", "char")
                if (child->type == AST_NODE_TYPE_SPECIFIER && child->is_terminal_node && child->data.terminal.text)
                {
                    char const * type_name = child->data.terminal.text;
                    LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                    if (struct_type)
                    {
                        target_type = struct_type;
                        break;
                    }
                    else if (strncmp(type_name, "int", 3) == 0)
                        target_type = LLVMInt32TypeInContext(ctx->context);
                    else if (strncmp(type_name, "char", 4) == 0)
                        target_type = LLVMInt8TypeInContext(ctx->context);
                    else if (strncmp(type_name, "void", 4) == 0)
                        target_type = LLVMVoidTypeInContext(ctx->context);
                    else if (strncmp(type_name, "float", 5) == 0)
                        target_type = LLVMFloatTypeInContext(ctx->context);
                    else if (strncmp(type_name, "double", 6) == 0)
                        target_type = LLVMDoubleTypeInContext(ctx->context);
                    else if (strncmp(type_name, "long", 4) == 0)
                        target_type = LLVMInt64TypeInContext(ctx->context);
                    else if (strncmp(type_name, "short", 5) == 0)
                        target_type = LLVMInt16TypeInContext(ctx->context);
                }
                // Handle Identifier (struct name like "Point" in sizeof(struct Point))
                else if (child->type == AST_NODE_IDENTIFIER)
                {
                    char const * type_name = child->data.terminal.text;
                    LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                    if (struct_type)
                    {
                        target_type = struct_type;
                    }
                }
                // Handle nested TypeSpecifier (non-terminal)
                else if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    target_type = map_type(ctx, child, NULL);
                }
            }
        }
        // Check if operand is a type specifier (e.g., sizeof(int))
        else if (operand_node->type == AST_NODE_TYPE_SPECIFIER)
        {
            if (operand_node->is_terminal_node && operand_node->data.terminal.text)
            {
                char const * type_name = operand_node->data.terminal.text;
                LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                if (struct_type)
                {
                    target_type = struct_type;
                }
                else if (strncmp(type_name, "int", 3) == 0)
                    target_type = LLVMInt32TypeInContext(ctx->context);
                else if (strncmp(type_name, "char", 4) == 0)
                    target_type = LLVMInt8TypeInContext(ctx->context);
                else if (strncmp(type_name, "void", 4) == 0)
                    target_type = LLVMVoidTypeInContext(ctx->context);
                else if (strncmp(type_name, "float", 5) == 0)
                    target_type = LLVMFloatTypeInContext(ctx->context);
                else if (strncmp(type_name, "double", 6) == 0)
                    target_type = LLVMDoubleTypeInContext(ctx->context);
                else if (strncmp(type_name, "long", 4) == 0)
                    target_type = LLVMInt64TypeInContext(ctx->context);
                else if (strncmp(type_name, "short", 5) == 0)
                    target_type = LLVMInt16TypeInContext(ctx->context);
                else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
                    target_type = LLVMInt1TypeInContext(ctx->context);
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
            char const * var_name = operand_node->data.terminal.text;
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
                    char const * var_name = deref_operand->data.terminal.text;
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
            for (size_t i = 0; i < operand_node->data.list.count && target_type == NULL; i++)
            {
                c_grammar_node_t * child = operand_node->data.list.children[i];

                if (child->type == AST_NODE_KEYWORD)
                {
                    continue;
                }

                if (child->type == AST_NODE_TYPE_SPECIFIER && child->is_terminal_node && child->data.terminal.text)
                {
                    char const * type_name = child->data.terminal.text;
                    LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                    if (struct_type)
                    {
                        target_type = struct_type;
                    }
                    else if (strncmp(type_name, "int", 3) == 0)
                        target_type = LLVMInt32TypeInContext(ctx->context);
                    else if (strncmp(type_name, "char", 4) == 0)
                        target_type = LLVMInt8TypeInContext(ctx->context);
                    else if (strncmp(type_name, "void", 4) == 0)
                        target_type = LLVMVoidTypeInContext(ctx->context);
                    else if (strncmp(type_name, "float", 5) == 0)
                        target_type = LLVMFloatTypeInContext(ctx->context);
                    else if (strncmp(type_name, "double", 6) == 0)
                        target_type = LLVMDoubleTypeInContext(ctx->context);
                    else if (strncmp(type_name, "long", 4) == 0)
                        target_type = LLVMInt64TypeInContext(ctx->context);
                    else if (strncmp(type_name, "short", 5) == 0)
                        target_type = LLVMInt16TypeInContext(ctx->context);
                    else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
                        target_type = LLVMInt1TypeInContext(ctx->context);
                }
                else if (child->type == AST_NODE_IDENTIFIER)
                {
                    char const * type_name = child->data.terminal.text;
                    LLVMTypeRef struct_type = find_struct_type(ctx, type_name);
                    if (struct_type)
                    {
                        target_type = struct_type;
                    }
                }
                else if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    target_type = map_type(ctx, child, NULL);
                }
            }
        }
        else if (operand_node->type == AST_NODE_TYPE_SPECIFIER || operand_node->type == AST_NODE_DECL_SPECIFIERS)
        {
            target_type = map_type(ctx, operand_node, NULL);
        }
        else if (operand_node->type == AST_NODE_IDENTIFIER)
        {
            char const * var_name = operand_node->data.terminal.text;
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
        fprintf(stderr, "IRGen Error: Unknown unary operator %u.\n", node->op.unary.op);
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
process_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL)
    {
        return NULL;
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
        /* Comma expression: evaluate all expressions, return the last value. */
        LLVMValueRef result = NULL;
        for (size_t i = 0; i < node->data.list.count; ++i)
        {
            result = process_expression(ctx, node->data.list.children[i]);
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
        fprintf(stderr, "BUG: got %s in %s", get_node_type_name_from_type(node->type), __func__);
        /* Shouldn't happen. */
        break;
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
    case AST_NODE_TYPEDEF_DECLARATION:
    case AST_NODE_INITIALIZER_LIST:
    case AST_NODE_LABELED_STATEMENT:
    case AST_NODE_CASE_LABEL:
    case AST_NODE_SWITCH_CASE:
    case AST_NODE_DEFAULT_STATEMENT:
    case AST_NODE_LABELED_IDENTIFIER:
    case AST_NODE_ASSIGNMENT_OPERATOR:
    case AST_NODE_OPTIONAL_KW_EXTENSION:
    case AST_NODE_OPTIONAL_INIT_DECLARATOR_LIST:
    case AST_NODE_KEYWORD:
    case AST_NODE_TERNARY_OPERATION:
    case AST_NODE_ENUM_SPECIFIER:
    case AST_NODE_ENUMERATOR:
    case AST_NODE_FUNCTION_POINTER_DECLARATOR:
    default:
        // Attempt to recursively process if it might yield a value.
        if (!node->is_terminal_node)
        {
            fprintf(
                stderr,
                "Default processing for list node: %s %u\n",
                get_node_type_name_from_type(node->type),
                node->type
            );
            for (size_t i = 0; i < node->data.list.count; ++i)
            {
                LLVMValueRef res = process_expression(ctx, node->data.list.children[i]);
                if (res)
                    return res; // Return the first valid result found.
            }
        }
        else
        {
            fprintf(stderr, "Ignoring terminal node %s (%u)\n", get_node_type_name_from_type(node->type), node->type);
        }
        break;
    }
    return NULL; // Return NULL if expression processing failed or not implemented.
}
