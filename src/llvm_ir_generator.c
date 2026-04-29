#include "llvm_ir_generator.h"

#include "ast_node_name.h"
#include "ast_print.h"
#include "c_grammar_ast.h" // Assumes this header defines c_grammar_node_t and its node types
#include "debug.h"
#include "declaration_handler.h"
#include "type_utils.h"

// Helper function to get natural alignment for a type
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TypedValue NullTypedValue;

typedef struct
{
    size_t count;
    bool is_variadic;
    char const ** names;
    LLVMTypeRef * types;
    LLVMTypeRef * pointee_types;
} parameter_definitions_t;

// Forward declarations for functions used before definition
// Helper to map C types to LLVM types
static LLVMTypeRef
map_type_to_llvm_t(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator);

static void process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node);

static TypedValue _process_expression_impl(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, int line);
#define process_expression(c, n) _process_expression_impl((c), (n), __LINE__);

char const * extract_typedef_name(c_grammar_node_t const * type_spec_node);
char const * extract_struct_or_union_or_enum_tag(c_grammar_node_t const * type_spec_node);
TypeDescriptor const * find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name);
LLVMTypeRef find_type_by_tag(ir_generator_ctx_t * ctx, char const * name);
TypeDescriptor const * find_type_descriptor_by_tag(ir_generator_ctx_t * ctx, char const * name);
char * generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix);
type_info_t const * register_struct_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child);
bool is_function_suffix(c_grammar_node_t const * suffix);
c_grammar_node_t const * extract_parameter_list(c_grammar_node_t const * suffix);
c_grammar_node_t const * search_parameters_list_in_declarator(c_grammar_node_t const * declarator_node);
int evaluate_enum_value_assignment_expression(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * value_node, int current_value
);
bool register_enum_constants(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);

static int find_struct_field_index(ir_generator_ctx_t * ctx, LLVMTypeRef struct_type, char const * field_name);
static TypedValue
cast_value_to_type(ir_generator_ctx_t * ctx, TypedValue src_value, LLVMTypeRef target_type, bool zero_extend);

static type_info_t const * register_tagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, char const * tag, type_kind_t kind
);

static type_info_t const * register_untagged_struct_or_union_definition(
    ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child, type_kind_t kind, int * new_type_id
);

static char const * search_ast_for_type_tag(c_grammar_node_t const * definition_node);

static type_info_t const *
register_tagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node, char const * tag);

static type_info_t const *
register_untagged_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node, int * new_enum_id);

static type_info_t const * register_enum_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * enum_node);

static TypedValue get_variable_pointer(ir_generator_ctx_t * ctx, c_grammar_node_t const * identifier_node);

static parameter_definitions_t
extract_function_parameters(ir_generator_ctx_t * ctx, c_grammar_node_t const * params_list);

// Helper function to extract struct/union tag from a TypeSpecifier node
// Returns the tag if found (struct or union definition with identifier child), or NULL
// Only returns non-NULL when struct/union definition node is present and it contains an Identifier.

// Parses const/volatile qualifiers at each level of indirection
// For 'int const * p': level 0 const comes from type specifiers
// For 'int * const p': level 0 const comes from pointer_list[0]
static void
extract_pointer_qualifiers(
    c_grammar_node_t const * pointer_list, c_grammar_node_t const * type_specifiers, pointer_qualifiers_t * pq
)
{
    if (pq == NULL)
    {
        return;
    }

    memset(pq, 0, sizeof(*pq));

    if (pointer_list != NULL && pointer_list->type == AST_NODE_POINTER_LIST)
    {
        pq->level = (unsigned int)pointer_list->list.count;
    }

    if (pq->level > MAX_POINTER_INDIRECTION_LEVELS)
    {
        pq->level = MAX_POINTER_INDIRECTION_LEVELS;
    }

    // Extract qualifiers from pointer_list (const/volatile after each *)
    for (unsigned int i = 0; i < pq->level; i++)
    {
        if (pointer_list == NULL)
        {
            break;
        }
        c_grammar_node_t const * ptr_node = pointer_list->list.children[i];
        if (ptr_node == NULL)
        {
            continue;
        }

        for (size_t j = 0; j < ptr_node->list.count; j++)
        {
            c_grammar_node_t const * qual = ptr_node->list.children[j];
            if (qual->type == AST_NODE_TYPE_QUALIFIER)
            {
                if (qual->type_qualifier.is_const)
                {
                    pq->is_const[i] = true;
                }
                if (qual->type_qualifier.is_volatile)
                {
                    pq->is_volatile[i] = true;
                }
            }
        }
    }

    // If there's a pointer level, check type specifiers for level 0 const (the pointee type)
    // e.g., 'int const * p' - the const is on 'int' (pointee), not on the pointer itself
    if (pq->level > 0 && type_specifiers != NULL && type_specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
    {
        if (type_specifiers->decl_specifiers.type.is_const)
        {
            pq->is_const_on_pointee = true; // const is on pointee, not on pointer
        }
        if (type_specifiers->decl_specifiers.type.is_volatile)
        {
            pq->is_volatile[0] = true;
        }
    }
}

static uint64_t
get_type_alignment(ir_generator_ctx_t * ctx, LLVMTypeRef type)
{
    if (type == NULL || LLVMGetTypeKind(type) == LLVMVoidTypeKind)
    {
        return 1;
    }

    // 1. Get the Data Layout from the module
    // This contains the rules for your specific target (x86, ARM, etc.)
    LLVMTargetDataRef data_layout = LLVMGetModuleDataLayout(ctx->module);

    // 2. Query the Preferred (or ABI) alignment directly as a number
    // LLVMABIAlignmentOfType returns the actual unsigned int you want.
    uint64_t alignment = LLVMABIAlignmentOfType(data_layout, type);

    debug_info("Type kind %u has alignment: %u", LLVMGetTypeKind(type), alignment);

    return alignment;
}

// Helper function to get size in bytes for a type
static uint64_t
get_type_size(ir_generator_ctx_t * ctx, LLVMTypeRef type)
{
    if (type == NULL || LLVMGetTypeKind(type) == LLVMVoidTypeKind)
    {
        return 0;
    }

    // Get the size in bytes as a standard C integer
    LLVMTargetDataRef data_layout = LLVMGetModuleDataLayout(ctx->module);
    uint64_t size_in_bytes = LLVMABISizeOfType(data_layout, type);
    debug_info("type size: %llu", size_in_bytes);
    return size_in_bytes;
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
    if (val.value == NULL)
    {
        return val;
    }
    if (!val.is_lvalue)
    {
        return val; // Already data
    }

    // Convert Lvalue to Rvalue by performing the load
    LLVMValueRef container = aligned_load(ctx, ctx->builder, val.type, val.value, label);

    // 2. If it's a bitfield, extract the bits
    if (val.bit_width > 0)
    {
        LLVMValueRef offset = LLVMConstInt(ctx->ref_type.i32_type, val.bit_offset, false);
        LLVMValueRef mask = LLVMConstInt(ctx->ref_type.i32_type, (1ULL << val.bit_width) - 1, false);
        LLVMValueRef shifted = LLVMBuildLShr(ctx->builder, container, offset, "bf_extract");
        container = LLVMBuildAnd(ctx->builder, shifted, mask, "bf_val");
    }

    return (TypedValue){
        .value = container,
        .type = val.type,
        .is_lvalue = false,
        .pointee_type = val.pointee_type,
        .bit_width = val.bit_width,
        .bit_offset = val.bit_offset,
    };
}

static TypedValue
process_array_subscript(ir_generator_ctx_t * ctx, c_grammar_node_t const * subscript_node, TypedValue base)
{
    LLVMValueRef base_ptr = base.value;
    LLVMTypeRef base_type = base.type;

    if (ctx == NULL || base_ptr == NULL || base_type == NULL || subscript_node == NULL)
    {
        return NullTypedValue;
    }

    // Extract index from first child of ArraySubscript node
    TypedValue index_res = NullTypedValue;
    if (subscript_node->list.count >= 1)
    {
        c_grammar_node_t * index_node = subscript_node->list.children[0];
        index_res = process_expression(ctx, index_node);
    }

    if (index_res.value == NULL)
    {
        return index_res;
    }

    // Determine element type and build GEP based on whether base is pointer or array
    LLVMTypeRef elem_type = NULL;
    LLVMValueRef elem_ptr = NULL;

    dump_typed_value("subscript base", base);

    if (LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
    {
        debug_info("%s: base_type is pointer (kind=%d)", __func__, LLVMGetTypeKind(base_type));
        LLVMValueRef ptr_val;

        /* Look at the instruction type of base_ptr.
         * If base_ptr is an Alloca, it's definitely a variable/memory location
         * that holds our pointer. We must load it.
         */
        if (LLVMIsAAllocaInst(base_ptr))
        {
            ptr_val = aligned_load(ctx, ctx->builder, base_type, base_ptr, "ptr_load");
            debug_info("%s: loaded pointer from alloca", __func__);
        }
        else
        {
            /* * If it's not an alloca (e.g., it's a LoadInst from o.inner),
             * then base_ptr IS the address we want. Do NOT load again.
             */
            ptr_val = base_ptr;
            debug_info("%s: using pointer value directly", __func__);
        }

        // Get the element type.
        elem_type = base.pointee_type;
        debug_info(
            "%s: get_pointer_element_type returned %p (i8=%p)",
            __func__,
            (void *)elem_type,
            (void *)ctx->ref_type.i8_type
        );

        if (elem_type == NULL || elem_type == ctx->ref_type.i8_type)
        {
            // Opaque pointer - try to find the struct type via scope
            // Look up the pointer type in scope to find what it points to
            type_info_t const * info = scope_find_type_by_llvm_type(ctx->current_scope, base_type);
            debug_info("%s: scope lookup for pointer type returned %p", __func__, (void *)info);

            // Try to find struct types that might be the pointee
            // We'll iterate through known struct types and check if any matches
            if (info == NULL)
            {
                // Try the opposite: look up struct types and check their pointer fields
                // For now, scan the current scope's untagged_types for struct with pointer field matching our type
                scope_t const * scope = ctx->current_scope;
                for (size_t i = 0; i < scope->untagged_types.count; i++)
                {
                    type_info_t const * ti = &scope->untagged_types.entries[i];
                    for (size_t j = 0; j < ti->field_count; ++j)
                    {
                        if (ti->fields[j].type == base_type && ti->fields[j].pointee_struct_type != NULL)
                        {
                            elem_type = ti->fields[j].pointee_struct_type;
                            debug_info("%s: found pointee from field %zu of struct %zu", __func__, j, i);
                            break;
                        }
                    }
                    if (elem_type != NULL && elem_type != ctx->ref_type.i8_type)
                    {
                        break;
                    }
                }
            }
        }

        // If still i8 or NULL, search through all scopes for a struct with a pointer field of our type
        if (elem_type == NULL || elem_type == ctx->ref_type.i8_type)
        {
            debug_info("%s: scanning scopes for pointer field", __func__);
            // Search through current and parent scopes
            scope_t const * scope = ctx->current_scope;
            while (scope != NULL && (elem_type == NULL || elem_type == ctx->ref_type.i8_type))
            {
                // Check tagged types
                for (size_t i = 0;
                     i < scope->tagged_types.count && (elem_type == NULL || elem_type == ctx->ref_type.i8_type);
                     ++i)
                {
                    type_info_t const * ti = &scope->tagged_types.entries[i];
                    for (size_t j = 0; j < ti->field_count; ++j)
                    {
                        if (ti->fields[j].type == base_type && ti->fields[j].pointee_struct_type != NULL)
                        {
                            elem_type = ti->fields[j].pointee_struct_type;
                            debug_info("%s: found pointee from tagged_types[%zu].field[%zu]", __func__, i, j);
                            break;
                        }
                    }
                }
                // Check untagged types
                for (size_t i = 0;
                     i < scope->untagged_types.count && (elem_type == NULL || elem_type == ctx->ref_type.i8_type);
                     ++i)
                {
                    type_info_t const * ti = &scope->untagged_types.entries[i];
                    for (size_t j = 0; j < ti->field_count; ++j)
                    {
                        if (ti->fields[j].type == base_type && ti->fields[j].pointee_struct_type != NULL)
                        {
                            elem_type = ti->fields[j].pointee_struct_type;
                            debug_info("%s: found pointee from untagged_types[%zu].field[%zu]", __func__, i, j);
                            break;
                        }
                    }
                }
                scope = scope->parent;
            }
            if (elem_type != NULL && elem_type != ctx->ref_type.i8_type)
            {
                debug_info("%s: found pointee through scope scan", __func__);
            }
        }

        // If elem_type is i8 (char*), check if the actual LLVM pointer's element is valid
        // This handles char* and other primitive pointer types
        if (elem_type == NULL || elem_type == ctx->ref_type.i8_type)
        {
            LLVMTypeRef direct_elem = LLVMGetElementType(base_type);
            if (direct_elem != NULL && direct_elem != ctx->ref_type.i8_type)
            {
                elem_type = direct_elem;
                debug_info("%s: using direct element type from LLVMGetElementType", __func__);
            }
            else if (direct_elem != NULL && LLVMGetTypeKind(direct_elem) == LLVMIntegerTypeKind)
            {
                // For char* and other integer pointers, use the element type directly
                elem_type = direct_elem;
                debug_info("%s: using integer element type", __func__);
            }
            else
            {
                // For char* specifically, i8 is correct - the issue was we were rejecting it
                // when the pointer was from a non-struct context. But char* should work.
                if (direct_elem == ctx->ref_type.i8_type)
                {
                    elem_type = direct_elem;
                    debug_info("%s: using i8 for char*", __func__);
                }
            }
        }

        // Final fallback - if we still can't find the element type, we can't proceed
        // But if the element type is i8 (char*), that's valid - allow it
        // The error case is when we have an opaque struct pointer
        if (elem_type == NULL)
        {
            debug_error(
                "Cannot determine element type for pointer subscript - pointer type %p is opaque.", (void *)base_type
            );
            return NullTypedValue;
        }

        // If element type is i8 (char*), that's valid for string handling
        // Only fail for i8 when it's truly an opaque struct pointer context
        if (elem_type == ctx->ref_type.i8_type)
        {
            // Check if this could be a struct pointer by looking at what we have
            // If we found this pointer in scope as a struct field, we'd have found it above
            // So if we get here with i8, it's likely a primitive pointer like char*
            // Allow it to proceed - this handles string subscripting
            debug_info("%s: allowing i8 element type for string/primitive pointer", __func__);
        }

        // Validate element type
        LLVMTypeKind elem_kind = LLVMGetTypeKind(elem_type);
        debug_info("%s: elem_type kind=%d", __func__, elem_kind);

        if (elem_kind != LLVMIntegerTypeKind && elem_kind != LLVMPointerTypeKind && elem_kind != LLVMStructTypeKind
            && elem_kind != LLVMArrayTypeKind && elem_kind != LLVMFloatTypeKind && elem_kind != LLVMDoubleTypeKind)
        {
            debug_error("Invalid element type kind %d for pointer subscript.", elem_kind);
            return NullTypedValue;
        }

        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, elem_type, ptr_val, &index_res.value, 1, "arrayidx");
    }
    else if (LLVMGetTypeKind(base_type) == LLVMArrayTypeKind)
    {
        debug_info("%s: base_type is array, using array path", __func__);

        // For array: use [0, index] GEP
        elem_type = LLVMGetElementType(base_type);
        if (elem_type == NULL)
        {
            return NullTypedValue;
        }
        index_res = ensure_rvalue(ctx, "array_index", index_res);

        LLVMValueRef indices[2];
        indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
        indices[1] = index_res.value;
        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, base_type, base_ptr, indices, 2, "arrayidx");

        TypedValue res = {
            .value = elem_ptr,
            .type = elem_type,
            .pointee_type = base.pointee_type,
            .is_lvalue = true,
        };

        return res;
    }
    else
    {
        debug_error("Invalid type for array subscript.");
        return NullTypedValue;
    }

    return (TypedValue){.value = elem_ptr, .type = elem_type, .is_lvalue = true};
}

static TypedValue
handle_bitfield_extraction(TypedValue tval, type_info_t const * info, size_t member_index)
{
    if (info == NULL || info->fields == NULL)
    {
        debug_info("%s no info to extract bit_width", __func__);
        return tval;
    }

    if (member_index >= info->field_count || info->fields[member_index].bit_width == 0)
    {
        return tval;
    }

    struct_field_t const * field = &info->fields[member_index];

    tval.bit_width = field->bit_width;
    tval.bit_offset = field->bit_offset;
    debug_info("%s: bitfield width: %u offset: %u", __func__, tval.bit_width, tval.bit_offset);
    return tval;
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
        LLVMValueRef size = LLVMSizeOf(element_type);
        LLVMValueRef zero = LLVMConstNull(ctx->ref_type.i8_type);
        LLVMBuildMemSet(ctx->builder, base_ptr, zero, size, get_type_alignment(ctx, element_type));
    }

    // Use a local index for processing leaf elements at this level
    int local_index = 0;

    for (size_t i = 0; i < initializer_node->list.count; ++i)
    {
        c_grammar_node_t const * list_entry = initializer_node->list.children[i];
        c_grammar_node_t const * value_node = list_entry->initializer_list_entry.initializer;
        c_grammar_node_t const * designation = list_entry->initializer_list_entry.designation;

        // Unwrap value from INITIALIZER wrapper if present
        if (value_node->list.count > 0)
        {
            value_node = value_node->list.children[0];
        }

        // Handle Designation nodes (designated initializers like .x = value or .pos.x = value)
        if (designation != NULL)
        {
            // Handle nested designations (e.g., .pos.x = value has 2 identifiers: pos, x)
            LLVMValueRef current_ptr = base_ptr;
            LLVMTypeRef current_type = element_type;
            LLVMTypeKind current_kind = kind;
            int field_indices[16]; // Max nesting depth
            (void)field_indices;
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
                                indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                                indices[1] = LLVMConstInt(ctx->ref_type.i32_type, field_idx, false);
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
                    indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                    indices[1] = LLVMConstInt(ctx->ref_type.i32_type, final_local_index, false);
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
                TypedValue tvalue = process_expression(ctx, value_node);
                if (tvalue.value && field_count > 0)
                {
                    LLVMValueRef elem_ptr;
                    if (field_count > 1)
                    {
                        // Nested field - current_ptr already points to the parent struct's field
                        // We need to get element 0 of that nested struct (since it's the final target)
                        LLVMValueRef indices[2];
                        indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                        indices[1] = LLVMConstInt(ctx->ref_type.i32_type, final_local_index, false);
                        elem_ptr = LLVMBuildInBoundsGEP2(
                            ctx->builder, current_type, current_ptr, indices, 2, "nested_init_ptr"
                        );
                    }
                    else
                    {
                        // Single field
                        LLVMValueRef indices[2];
                        indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                        indices[1] = LLVMConstInt(ctx->ref_type.i32_type, final_local_index, false);
                        elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");
                    }

                    TypedValue cast_value = cast_value_to_type(ctx, tvalue, final_type, false);
                    aligned_store(ctx, ctx->builder, cast_value.value, cast_value.type, elem_ptr);
                }
            }

            local_index++;
            if (outer_index != NULL)
            {
                (*outer_index)++;
            }
            continue;
        }

        // Non-designated: handle plain INITIALIZER_LIST for arrays
        if (value_node->type == AST_NODE_INITIALIZER_LIST && kind == LLVMArrayTypeKind)
        {
            LLVMTypeRef nested_element = LLVMGetElementType(element_type);
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
            indices[1] = LLVMConstInt(ctx->ref_type.i32_type, local_index, false);
            LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "elem_ptr");
            process_initializer_list(ctx, elem_ptr, nested_element, value_node, NULL);
            local_index++;
            if (outer_index != NULL)
            {
                (*outer_index)++;
            }
            continue;
        }

        // For array types, create a GEP to the element and recurse
        if (kind == LLVMArrayTypeKind && value_node->type != AST_NODE_INTEGER_LITERAL && value_node->list.count > 0)
        {
            LLVMTypeRef nested_element = LLVMGetElementType(element_type);
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
            indices[1] = LLVMConstInt(ctx->ref_type.i32_type, local_index, false);
            LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");
            process_initializer_list(ctx, elem_ptr, nested_element, value_node, &local_index);
        }
        // Process leaf values - store to array or struct member
        else
        {
            TypedValue tvalue = process_expression(ctx, value_node);
            LLVMValueRef value = tvalue.value;
            LLVMTypeRef final_type = tvalue.type;
            if (tvalue.value != NULL)
            {
                LLVMValueRef indices[2];
                indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                indices[1] = LLVMConstInt(ctx->ref_type.i32_type, local_index, false);

                LLVMValueRef elem_ptr
                    = LLVMBuildInBoundsGEP2(ctx->builder, element_type, base_ptr, indices, 2, "init_ptr");

                // For structs, cast the value to the member type
                if (kind == LLVMStructTypeKind)
                {
                    LLVMTypeRef member_type = LLVMStructGetTypeAtIndex(element_type, (unsigned)local_index);
                    if (member_type)
                    {
                        TypedValue cast_value = cast_value_to_type(ctx, tvalue, member_type, false);
                        value = cast_value.value;
                        final_type = cast_value.type;
                    }
                }

                aligned_store(ctx, ctx->builder, value, final_type, elem_ptr);
                local_index++;
                if (outer_index)
                {
                    (*outer_index)++;
                }
            }
        }
    }
}

static int
find_struct_field_index(ir_generator_ctx_t * ctx, LLVMTypeRef struct_type, char const * member_name)
{
    if (!struct_type || !member_name)
        return -1;

    type_info_t * info = scope_find_type_by_llvm_type(ctx->current_scope, struct_type);

    if (!info)
        return -1;

    for (size_t i = 0; i < info->field_count; ++i)
    {
        if (info->fields[i].name && strcmp(info->fields[i].name, member_name) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

static unsigned
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

static TypedValue
cast_value_to_type(ir_generator_ctx_t * ctx, TypedValue src_value, LLVMTypeRef target_type, bool is_src_signed)
{
    debug_info("%s: in", __func__);
    dump_typed_value("pre-cast", src_value);

    src_value = ensure_rvalue(ctx, "cast_rval", src_value);

    LLVMValueRef value = src_value.value;
    LLVMTypeRef src_type = src_value.type;

    if (value == NULL || target_type == NULL || src_type == target_type)
    {
        debug_info(
            "no cast needed - value: %p target_type: %p src_type: %p",
            (void *)value,
            (void *)target_type,
            (void *)src_type
        );
        return src_value;
    }

    LLVMTypeKind src_kind = LLVMGetTypeKind(src_type);
    LLVMTypeKind target_kind = LLVMGetTypeKind(target_type);

    debug_info("%s: target_kind: %u, src_kind: %u", __func__, target_kind, src_kind);
    // 1. Integer to Integer (Truncate, ZExt, or SExt)
    if (src_kind == LLVMIntegerTypeKind && target_kind == LLVMIntegerTypeKind)
    {
        unsigned src_bits = LLVMGetIntTypeWidth(src_type);
        unsigned target_bits = LLVMGetIntTypeWidth(target_type);

        debug_info("%s target bits: %u, src_bits: %u", __func__, target_bits, src_bits);

        if (target_bits < src_bits)
        {
            return (TypedValue){
                .value = LLVMBuildTrunc(ctx->builder, value, target_type, "cast_trunc"),
                .type = target_type,
            };
        }
        if (target_bits > src_bits)
        {
            LLVMValueRef new_value = is_src_signed ? LLVMBuildSExt(ctx->builder, value, target_type, "cast_sext")
                                                   : LLVMBuildZExt(ctx->builder, value, target_type, "cast_zext");
            return (TypedValue){.value = new_value, .type = target_type};
        }
        dump_typed_value("post-cast", src_value);
        return src_value;
    }

    unsigned float_src_bits = get_fp_width(src_type);
    unsigned float_target_bits = get_fp_width(target_type);
    if (float_src_bits > 0 && float_target_bits > 0)
    {
        debug_info("FP bits differ src %u, target: %u", float_src_bits, float_target_bits);

        if (float_src_bits > float_target_bits)
            return (TypedValue){
                .value = LLVMBuildFPTrunc(ctx->builder, value, target_type, "cast_fptrunc"),
                .type = target_type,
            };
        if (float_src_bits < float_target_bits)
            return (TypedValue){
                .value = LLVMBuildFPExt(ctx->builder, value, target_type, "cast_fpext"),
                .type = target_type,
            };
        dump_typed_value("post-cast", src_value);
        return src_value; // Same width, do nothing
    }

    // 2. Integer to Floating Point
    if (src_kind == LLVMIntegerTypeKind && (target_kind == LLVMFloatTypeKind || target_kind == LLVMDoubleTypeKind))
    {
        LLVMValueRef new_value = is_src_signed ? LLVMBuildSIToFP(ctx->builder, value, target_type, "cast_sitofp")
                                               : LLVMBuildUIToFP(ctx->builder, value, target_type, "cast_uitofp");
        TypedValue res = {.value = new_value, .type = target_type};
        dump_typed_value("post-cast", res);

        return res;
    }

    // 3. Floating Point to Integer
    if ((src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind) && target_kind == LLVMIntegerTypeKind)
    {
        // Note: Floating point to int usually truncates the fractional part in C
        LLVMValueRef new_value = is_src_signed ? LLVMBuildFPToSI(ctx->builder, value, target_type, "cast_fptosi")
                                               : LLVMBuildFPToUI(ctx->builder, value, target_type, "cast_fptoui");

        TypedValue res = {.value = new_value, .type = target_type};
        dump_typed_value("post-cast", res);

        return res;
    }

    // 4. Pointer to Pointer (BitCast)
    if (src_kind == LLVMPointerTypeKind && target_kind == LLVMPointerTypeKind)
    {
        LLVMValueRef new_value = LLVMBuildBitCast(ctx->builder, value, target_type, "cast_bitcast");

        TypedValue res = {.value = new_value, .type = target_type};
        dump_typed_value("post-cast", res);

        return res;
    }

    // 5. Pointer to Integer / Integer to Pointer
    if (src_kind == LLVMPointerTypeKind && target_kind == LLVMIntegerTypeKind)
    {
        LLVMValueRef new_value = LLVMBuildPtrToInt(ctx->builder, value, target_type, "cast_ptrtoint");

        TypedValue res = {.value = new_value, .type = target_type};
        dump_typed_value("post-cast", res);

        return res;
    }
    if (src_kind == LLVMIntegerTypeKind && target_kind == LLVMPointerTypeKind)
    {
        LLVMValueRef new_value = LLVMBuildIntToPtr(ctx->builder, value, target_type, "cast_inttoptr");

        TypedValue res = {.value = new_value, .type = target_type};
        dump_typed_value("post-cast", res);

        return res;
    }

    dump_typed_value("post-cast", src_value);
    return src_value;
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

static LLVMTypeRef
get_type_from_name(ir_generator_ctx_t * ctx, char const * type_name)
{
    LLVMTypeRef type_ref = NULL;

    debug_info("%s type name: '%s'", __func__, type_name);

    LLVMTypeRef struct_type = find_type_by_tag(ctx, type_name);
    if (struct_type)
    {
        type_ref = struct_type;
    }
    // Then check for basic types
    else if (strncmp(type_name, "int", 3) == 0)
        type_ref = ctx->ref_type.i32_type;
    else if (strncmp(type_name, "char", 4) == 0)
        type_ref = ctx->ref_type.i8_type;
    else if (strncmp(type_name, "void", 4) == 0)
        type_ref = LLVMVoidTypeInContext(ctx->context);
    else if (strncmp(type_name, "float", 5) == 0)
        type_ref = LLVMFloatTypeInContext(ctx->context);
    else if (strstr(type_name, "long") != NULL && strstr(type_name, "double") != NULL)
        type_ref = LLVMX86FP80TypeInContext(ctx->context);
    else if (strncmp(type_name, "double", 6) == 0)
        type_ref = LLVMDoubleTypeInContext(ctx->context);
    else if (strstr(type_name, "long") != NULL)
        type_ref = LLVMInt64TypeInContext(ctx->context);
    else if (strncmp(type_name, "short", 5) == 0)
        type_ref = LLVMInt16TypeInContext(ctx->context);
    else if (strncmp(type_name, "_Bool", 5) == 0 || strncmp(type_name, "bool", 4) == 0)
        type_ref = LLVMInt1TypeInContext(ctx->context);

    if (type_ref != NULL)
    {
        debug_info("got type kind: %d", LLVMGetTypeKind(type_ref));
    }

    return type_ref;
}

static void
dump_llvm_type(char const * label, LLVMTypeRef type)
{
    debug_info("%s %s (%p):", __func__, label, type);
    if (type == NULL)
    {
        return;
    }
    debug_info("type kind: %u", LLVMGetTypeKind(type));
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
map_type_to_llvm_t(ir_generator_ctx_t * ctx, c_grammar_node_t const * specifiers, c_grammar_node_t const * declarator)
{
    debug_info("%s: specifier_type: %s", __func__, get_node_type_name_from_node(specifiers));
    static int map_type_depth = 0;
    if (map_type_depth > 64)
    {
        debug_error("map_type: recursion depth exceeded");
        return ctx->ref_type.i32_type;
    }
    map_type_depth++;
    LLVMTypeRef base_type = NULL;
    int pointer_level = 0;
    size_t array_depth = 0;
    size_t array_capacity = 4;
    size_t * array_sizes = malloc(array_capacity * sizeof(*array_sizes));

    if (array_sizes == NULL)
    {
        map_type_depth--;
        return ctx->ref_type.i32_type;
    }

    // 1. Process Specifiers (extract base type and any pointers in specifiers)
    if (specifiers->type == AST_NODE_STRUCT_SPECIFIER_QUALIFIER_LIST && specifiers->list.count == 1)
    {
        c_grammar_node_t const * child = specifiers->list.children[0];
        if (child->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
        {
            specifiers = child->typedef_specifier_qualifier.typedef_specifier;
        }
    }

    if (specifiers != NULL)
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
                                base_type = info->type_desc->llvm_type;
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
        /* Handle TypedefSpecifier directly (e.g. from struct member type_spec) */
        else if (specifiers->type == AST_NODE_TYPEDEF_SPECIFIER)
        {
            char const * typedef_name = extract_typedef_name(specifiers);
            if (typedef_name != NULL)
            {
                TypeDescriptor const * typedef_type_desc = find_typedef_type_descriptor(ctx, typedef_name);
                if (typedef_type_desc != NULL)
                {
                    base_type = typedef_type_desc->llvm_type;
                }
            }
        }
        /* Handle DeclarationSpecifiers - use structured fields */
        else if (specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
        {
            // Use the structured fields from ast_node_decl_specifiers_t
            c_grammar_node_t const * typedef_specifier_node = specifiers->decl_specifiers.typedef_specifier;
            c_grammar_node_t const * specifier_list = specifiers->decl_specifiers.type_specifiers;
            debug_info(
                "handling node type %s and specifier list %s",
                get_node_type_name_from_node(specifiers),
                get_node_type_name_from_node(specifier_list)
            );

            // Check for typedef name first
            if (typedef_specifier_node != NULL)
            {
                char const * typedef_name = extract_typedef_name(typedef_specifier_node);
                if (typedef_name != NULL)
                {
                    TypeDescriptor const * typedef_type_desc = find_typedef_type_descriptor(ctx, typedef_name);
                    if (typedef_type_desc != NULL)
                    {
                        base_type = typedef_type_desc->llvm_type;
                    }
                }
            }
            // Check type specifier
            else if (specifier_list->list.count > 0)
            {
                bool is_struct_or_union_or_enum = false;
                c_grammar_node_t const * type_spec_node = specifier_list->list.children[0];
                c_grammar_node_t const * child
                    = type_spec_node->list.count > 0 ? type_spec_node->list.children[0] : NULL;
                if (child != NULL)
                {
                    is_struct_or_union_or_enum
                        = child->type == AST_NODE_STRUCT_DEFINITION || child->type == AST_NODE_UNION_DEFINITION
                          || child->type == AST_NODE_ENUM_DEFINITION || child->type == AST_NODE_STRUCT_TYPE_REF
                          || child->type == AST_NODE_UNION_TYPE_REF || child->type == AST_NODE_ENUM_TYPE_REF;
                }
                if (!is_struct_or_union_or_enum)
                {
                    TypeSpecifier type_spec = build_type_specifiers(specifier_list);
                    TypeDescriptor const * type_desc
                        = get_or_create_builtin_type(ctx->type_descriptors, type_spec, (TypeQualifier){0});
                    if (type_desc != NULL && type_desc->llvm_type != NULL)
                    {
                        debug_info("and got type kind: %d", LLVMGetTypeKind(type_desc->llvm_type));
                        base_type = type_desc->llvm_type;
                    }
                }

                if (base_type == NULL)
                {
                    c_grammar_node_t const * type_spec_node = specifier_list->list.children[0];

                    // Handle case where type_spec_node is AST_NODE_TYPE_SPECIFIER with text (terminal)
                    if (type_spec_node->text != NULL)
                    {
                        char const * type_name = type_spec_node->text;

                        // First check for typedef
                        TypeDescriptor const * typedef_type_desc = find_typedef_type_descriptor(ctx, type_name);
                        if (typedef_type_desc != NULL)
                        {
                            base_type = typedef_type_desc->llvm_type;
                        }
                        else
                        {
                            base_type = get_type_from_name(ctx, type_name);
                        }
                    }
                    else if (type_spec_node->type == AST_NODE_TYPE_SPECIFIER && type_spec_node->list.count > 0)
                    {
                        debug_info("handling type specifier list with %u items", type_spec_node->list.count);
                        // Type specifier is a list - find first child with type name text
                        for (size_t i = 0; i < type_spec_node->list.count; ++i)
                        {
                            c_grammar_node_t const * child = type_spec_node->list.children[i];
                            debug_info("processing child type: %s", get_node_type_name_from_node(child));
                            if (child->text != NULL)
                            {
                                // Check for typedef first
                                TypeDescriptor const * typedef_type_desc
                                    = find_typedef_type_descriptor(ctx, child->text);
                                if (typedef_type_desc != NULL)
                                {
                                    base_type = typedef_type_desc->llvm_type;
                                    break;
                                }
                                base_type = get_type_from_name(ctx, child->text);
                                if (base_type != NULL)
                                {
                                    break;
                                }
                            }
                            else if (
                                child->type == AST_NODE_STRUCT_DEFINITION || child->type == AST_NODE_UNION_DEFINITION
                            )
                            {
                                type_info_t const * info = register_struct_definition(ctx, child);
                                if (info != NULL)
                                {
                                    base_type = info->type_desc->llvm_type;
                                    break;
                                }
                            }
                            else if (child->type == AST_NODE_STRUCT_TYPE_REF || child->type == AST_NODE_UNION_TYPE_REF)
                            {
                                char const * tag = extract_struct_or_union_or_enum_tag(child);
                                if (tag != NULL)
                                {
                                    base_type = find_type_by_tag(ctx, tag);
                                    if (base_type != NULL)
                                    {
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        // Use helper to determine if this is a struct/union reference
                        char const * struct_name = extract_struct_or_union_or_enum_tag(type_spec_node);
                        if (struct_name != NULL)
                        {
                            LLVMTypeRef struct_type = find_type_by_tag(ctx, struct_name);
                            if (struct_type != NULL)
                            {
                                base_type = struct_type;
                            }
                        }
                    }
                }
            }
        }
    }
    debug_info("check declarator");
    // 2. Process Declarator (extract pointers and arrays)
    bool is_function_pointer = false;
    LLVMTypeRef func_ptr_param_types[16];
    (void)func_ptr_param_types;
    size_t func_ptr_num_params = 0;

    if (declarator && declarator->type == AST_NODE_DECLARATOR)
    {
        pointer_level = declarator->declarator.pointer_list->list.count;
        debug_info("is declarator and pointer level %u", pointer_level);
        {
            c_grammar_node_t const * direct_decl = declarator->declarator.direct_declarator;
            // Check inside DirectDeclarator for pointers and arrays
            // The structure can be: DirectDeclarator -> Declarator -> {Pointer, ..., DeclaratorSuffix}
            for (size_t j = 0; j < direct_decl->list.count; ++j)
            {
                c_grammar_node_t * direct_child = direct_decl->list.children[j];
                if (direct_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                {
                    // Function pointer parameter: int (*func)(int, int)
                    is_function_pointer = true;
                    pointer_level++;

                    c_grammar_node_t const * suffix_list
                        = direct_child->function_pointer_declarator.declarator_suffix_list;

                    for (size_t si = 0; si < suffix_list->list.count; si++)
                    {
                        c_grammar_node_t const * suffix = suffix_list->list.children[si];

                        // Check for array size inside FunctionPointerDeclarator (e.g., (*ops[2]))
                        for (size_t m = 0; m < suffix->list.count; ++m)
                        {
                            c_grammar_node_t * suffix_child = suffix->list.children[m];

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
        c_grammar_node_t const * suffix_list = declarator->declarator.declarator_suffix_list;

        for (size_t i = 0; i < suffix_list->list.count; ++i)
        {
            c_grammar_node_t * suffix = suffix_list->list.children[i];
            debug_info(
                "map_type: suffix[%zu] type=%s count=%zu",
                i,
                get_node_type_name_from_type(suffix->type),
                suffix->list.count
            );
            // Check if this is a function suffix (contains DeclarationSpecifiers for params)
            // vs array suffix (contains IntegerLiteral for size)
            bool has_function_params = false;
            bool has_array_size = false;

            c_grammar_node_t const * parameter_list = suffix;
            if (suffix->list.count == 1)
            {
                if (suffix->list.children[0]->type == AST_NODE_PARAMETER_LIST)
                {
                    parameter_list = suffix->list.children[0];
                }
            }
            for (size_t j = 0; j < parameter_list->list.count; ++j)
            {
                c_grammar_node_t * params_child = parameter_list->list.children[j];

                debug_info("map_type: param[%zu] type=%s", j, get_node_type_name_from_type(params_child->type));
                if (params_child->type == AST_NODE_NAMED_DECL_SPECIFIERS)
                {
                    // This is a function parameter type
                    has_function_params = true;
                    if (func_ptr_num_params < 16)
                    {
                        func_ptr_param_types[func_ptr_num_params++] = map_type_to_llvm_t(ctx, params_child, NULL);
                    }
                }
                else if (params_child->type == AST_NODE_INTEGER_LITERAL)
                {
                    // This is an array size
                    unsigned long long size_val = params_child->integer_lit.integer_literal.value;
                    if (array_depth < array_capacity)
                    {
                        array_sizes[array_depth] = (size_t)size_val;
                        array_depth++;
                        has_array_size = true;
                    }
                }
                else if (params_child->type == AST_NODE_DECLARATOR)
                {
                    // Function parameter with declarator (e.g., int (*func)(int))
                    // For now, just extract the type specifier
                    has_function_params = true;
                    if (func_ptr_num_params < 16)
                    {
                        func_ptr_param_types[func_ptr_num_params++] = map_type_to_llvm_t(ctx, suffix, NULL);
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

    // Handle Abstract Declarator (used in type names, e.g., cast expressions)
    if (declarator != NULL && declarator->type == AST_NODE_ABSTRACT_DECLARATOR)
    {
        debug_info("is abstract delcarator");
        // AbstractDeclarator = (PointerPlus DeclaratorSuffixList) | DeclaratorSuffixList
        // The children can be: [PointerList?, DeclaratorSuffixList?]

        // First child might be a pointer list
        if (declarator->list.count > 0)
        {
            c_grammar_node_t const * first_child = declarator->list.children[0];
            if (first_child->type == AST_NODE_POINTER_LIST)
            {
                pointer_level = first_child->list.count;
            }
        }

        // Look for declarator suffix list (array sizes, function params)
        for (size_t i = 0; i < declarator->list.count; ++i)
        {
            c_grammar_node_t const * child = declarator->list.children[i];
            if (child->type == AST_NODE_DECLARATOR_SUFFIX_LIST)
            {
                for (size_t j = 0; j < child->list.count; ++j)
                {
                    c_grammar_node_t const * suffix = child->list.children[j];
                    bool has_array_size = false;

                    for (size_t k = 0; k < suffix->list.count; ++k)
                    {
                        c_grammar_node_t const * suffix_child = suffix->list.children[k];
                        debug_info("%u child type: %s", __LINE__, get_node_type_name_from_node(suffix_child));

                        if (suffix_child->type == AST_NODE_INTEGER_LITERAL)
                        {
                            unsigned long long size_val = suffix_child->integer_lit.integer_literal.value;
                            if (array_depth < array_capacity)
                            {
                                array_sizes[array_depth] = (size_t)size_val;
                                array_depth++;
                                has_array_size = true;
                            }
                        }
                    }

                    // Empty brackets [] - unsized array
                    if (!has_array_size && suffix->list.count > 0)
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
    }

    if (base_type == NULL)
    {
        base_type = ctx->ref_type.i32_type;
    }

    // Handle function pointer - return pointer type (possibly wrapped in array)
    if (is_function_pointer)
    {
        LLVMTypeRef func_ptr_type = LLVMPointerType(ctx->ref_type.i8_type, 0);

        // If there are array sizes, this is an array of function pointers
        if (array_depth > 0)
        {
            for (int i = (int)array_depth - 1; i >= 0; --i)
            {
                func_ptr_type = LLVMArrayType(func_ptr_type, (unsigned)array_sizes[i]);
            }
        }

        free(array_sizes);
        map_type_depth--;
        return func_ptr_type;
    }

    // Add pointer types FIRST (before arrays, so for "char * arr[64]" we get [64 x ptr])
    LLVMTypeRef final_type = base_type;
    debug_info(
        "map_type: array_depth=%zu, pointer_level=%d, base_type kind=%d",
        array_depth,
        pointer_level,
        LLVMGetTypeKind(base_type)
    );
    debug_info("now making a pointer: level %u", pointer_level);
    for (int i = 0; i < pointer_level; ++i)
    {
        final_type = LLVMPointerType(final_type, 0);
    }

    // Then build array types from innermost to outermost
    for (int i = (int)array_depth - 1; i >= 0; --i)
    {
        debug_info("map_type: array_size[%d]=%zu", i, array_sizes[i]);
        final_type = LLVMArrayType(final_type, (unsigned)array_sizes[i]);
    }

    free(array_sizes);
    map_type_depth--;

    dump_llvm_type(__func__, final_type);

    return final_type;
}

static struct_or_union_members_st
extract_struct_or_union_members(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child);

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

static struct_or_union_members_st
extract_struct_or_union_members(ir_generator_ctx_t * ctx, c_grammar_node_t const * type_child)
{
    debug_info("%s:", __func__);
    struct_or_union_members_st object_members = {0};

    if (type_child == NULL
        || (type_child->type != AST_NODE_STRUCT_DEFINITION && type_child->type != AST_NODE_UNION_DEFINITION))
    {
        return object_members;
    }

    c_grammar_node_t const * members_node = type_child->struct_definition.declaration_list;

    if (members_node == NULL || members_node->list.count == 0)
    {
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
        debug_info("%s: spec_qual_list_type is %s", __func__, get_node_type_name_from_node(specifier_qualifier_list));

        c_grammar_node_t const * type_spec = NULL;
        if (specifier_qualifier_list->list.count == 1
            && specifier_qualifier_list->list.children[0]->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
        {
            c_grammar_node_t const * child = specifier_qualifier_list->list.children[0];
            type_spec = child->typedef_specifier_qualifier.typedef_specifier;
            debug_info("ssql type spec is a %s node", get_node_type_name_from_node(type_spec));
        }
        else
        {
            for (size_t j = 0; j < specifier_qualifier_list->list.count; j++)
            {
                c_grammar_node_t const * child = specifier_qualifier_list->list.children[j];
                if (child != NULL
                    && (child->type == AST_NODE_TYPE_SPECIFIER || child->type == AST_NODE_TYPEDEF_SPECIFIER))
                {
                    type_spec = child;
                    break;
                }
            }
        }

        if (type_spec == NULL)
        {
            continue;
        }

        if (declarator_list == NULL || declarator_list->list.count == 0)
        {
            continue;
        }

        c_grammar_node_t const * struct_decl_node = declarator_list->list.children[0];

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
                    new_member.bit_width = (unsigned)width_node->integer_lit.integer_literal.value;
                }

                new_member.type = map_type_to_llvm_t(ctx, type_spec, NULL);
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
                new_member.type = map_type_to_llvm_t(ctx, type_spec, decl);

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
                if (new_member.type == NULL)
                {
                    free(new_member.name);
                    continue;
                }

                struct_field_t * previous_member = NULL;
                if (num_members > 0)
                {
                    previous_member = &members[num_members - 1];
                }
                new_member.storage_index = (previous_member == NULL) ? 0 : (previous_member->storage_index + 1);

                if (LLVMGetTypeKind(new_member.type) == LLVMPointerTypeKind)
                {
                    LLVMTypeRef base = map_type_to_llvm_t(ctx, type_spec, NULL);
                    if (base != NULL && LLVMGetTypeKind(base) == LLVMStructTypeKind)
                    {
                        new_member.pointee_struct_type = base;
                    }
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
    bool is_complete = false;
    struct_or_union_members_st * members = NULL;
    opaque.type_desc = register_struct_type(
        ctx->type_descriptors,
        LLVMStructCreateNamed(ctx->context, tag),
        (TypeQualifier){0},
        is_union,
        is_complete,
        members
    );
    type_info_t const * registered = scope_add_tagged_type(ctx->current_scope, opaque);
    if (registered == NULL)
    {
        free((void *)opaque.tag);
        return NULL;
    }

    return registered;
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
        return NULL;
    }

    struct_or_union_members_st members = extract_struct_or_union_members_type_descriptor(ctx, type_child);

    type_info_t opaque = {0};
    opaque.tag = strdup(tag);
    opaque.kind = kind;
    bool is_complete = true;

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

    opaque.type_desc = register_struct_type(
        ctx->type_descriptors,
        LLVMStructCreateNamed(ctx->context, tag),
        (TypeQualifier){0},
        kind == TYPE_KIND_UNION,
        is_complete,
        &members
    );

    type_info_t const * registered = scope_add_tagged_type(ctx->current_scope, opaque);
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
    unsigned num_storage_units = last_field->storage_index + 1;
    LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
    if (field_types == NULL)
    {
        return registered;
    }
    int current_storage_unit = -1;
    for (size_t i = 0; i < members.num_members; i++)
    {
        struct_field_t * field = &members.members[i];
        if (field->storage_index != (unsigned)current_storage_unit)
        {
            current_storage_unit = (int)field->storage_index;
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
    bool is_complete = true;

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

    new_struct.type_desc = register_struct_type(
        ctx->type_descriptors,
        LLVMStructCreateNamed(ctx->context, new_struct.tag),
        (TypeQualifier){0},
        kind == TYPE_KIND_UNTAGGED_UNION,
        is_complete,
        &members
    );

    type_info_t const * registered = scope_add_untagged_type(ctx->current_scope, new_struct, new_type_id);
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
    unsigned num_storage_units = last_field->storage_index + 1;
    LLVMTypeRef * field_types = calloc(num_storage_units, sizeof(*field_types));
    int current_storage_unit = -1;
    for (size_t i = 0; i < new_struct.field_count; i++)
    {
        struct_field_t * field = &members.members[i];
        if (field->storage_index != (unsigned)current_storage_unit)
        {
            current_storage_unit = field->storage_index;
            field_types[current_storage_unit] = field->type_desc->llvm_type;
        }
    }

    LLVMStructSetBody(new_struct.type_desc->llvm_type, field_types, (unsigned)num_storage_units, false);
    free(field_types);

    for (size_t i = 0; i < members.num_members; i++)
    {
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

    return register_tagged_struct_or_union_definition(ctx, type_child, struct_tag, kind);
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
    enum_info.type_desc = type_descriptor_get_int32_type(ctx->type_descriptors, true);
    enum_info.fields = NULL;
    enum_info.field_count = 0;

    return scope_add_untagged_type(ctx->current_scope, enum_info, new_enum_id);
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
    enum_info.type_desc = type_descriptor_get_int32_type(ctx->type_descriptors, true);
    enum_info.fields = NULL;
    enum_info.field_count = 0;
    enum_info.tag = strdup(tag);

    return scope_add_tagged_type(ctx->current_scope, enum_info);
}

static type_info_t const *
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
ir_generator_init(char const * module_name, ir_generation_flags flags)
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

    /* TODO: Remove once the type registry is up and running. */
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

    ctx->type_descriptors = type_descriptors_create_registry(ctx->context);
    if (ctx->type_descriptors == NULL)
    {
        ir_generator_dispose(ctx);
        return NULL;
    }

    ctx->module = LLVMModuleCreateWithName(module_name);
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
    ctx->current_scope = scope_create(NULL, ctx->context, ctx->builder); // NULL parent = global scope
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
        add_symbol(ctx, "__FILE__", val, NULL);
    }

    if (ctx->generation_flags.generate_default_variables)
    {
        /* Create a replacement for NULL, which won't be available if not preprocessing. */
        LLVMTypeRef null_type = ctx->ref_type.ptr_type;
        LLVMValueRef null_const = LLVMConstPointerNull(null_type);
        LLVMSetGlobalConstant(null_const, true);

        TypeDescriptor const * void_desc = type_descriptor_get_void_type(ctx->type_descriptors);
        TypeDescriptor const * ptr_desc
            = get_or_create_pointer_type(ctx->type_descriptors, void_desc, (TypeQualifier){0});
        TypedValue null_val = create_typed_value(null_const, ptr_desc, false);
        add_symbol(ctx, "NULL", null_val, NULL);
    }

    // Initialize error collection (any error will be fatal since max_errors=1)
    ir_gen_error_collection_init(&ctx->errors, 1);

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
pop_all_scopes(ir_generator_ctx_t * ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    // Free all scopes in the chain
    while (ctx->current_scope)
    {
        scope_pop(ctx);
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

/* --- Cycle detection for AST node processing --- */
#define VISIT_STACK_MAX 4096
static c_grammar_node_t const * visit_stack[VISIT_STACK_MAX];
static int visit_stack_top = 0;

static bool
visit_stack_push(c_grammar_node_t const * node)
{
    for (int i = 0; i < visit_stack_top; i++)
    {
        if (visit_stack[i] == node)
        {
            fprintf(
                stderr,
                "DEBUG: cycle detected in _process_ast_node! node=%p type=%s\n",
                (void *)node,
                get_node_type_name_from_node(node)
            );
            return false; /* cycle */
        }
    }
    if (visit_stack_top < VISIT_STACK_MAX)
    {
        visit_stack[visit_stack_top++] = node;
    }
    return true;
}

static void
visit_stack_pop(c_grammar_node_t const * node)
{
    if (visit_stack_top > 0 && visit_stack[visit_stack_top - 1] == node)
    {
        visit_stack_top--;
    }
}

static void
add_function_scope_builtin_macros(ir_generator_ctx_t * ctx, char const * func_name)
{
    // __FUNC__ and __func__ as string constants of the function name
    char const * func_name_macro = func_name;
    size_t flen = strlen(func_name_macro);
    LLVMTypeRef farr_type = LLVMArrayType(ctx->ref_type.i8_type, (unsigned)(flen + 1));
    LLVMValueRef fglobal = LLVMAddGlobal(ctx->module, farr_type, "__FUNC__");
    LLVMSetLinkage(fglobal, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(fglobal, true);
    LLVMSetInitializer(fglobal, LLVMConstStringInContext(ctx->context, func_name_macro, (unsigned)flen, false));

    LLVMValueRef findices[2]
        = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};
    LLVMValueRef fptr = LLVMConstInBoundsGEP2(farr_type, fglobal, findices, 2);

    TypeDescriptor const * char_desc = type_descriptor_get_int8_type(ctx->type_descriptors, true);
    TypeDescriptor const * farr_desc = get_or_create_array_type(ctx->type_descriptors, char_desc, flen + 1);
    TypedValue fval = create_typed_value(fptr, farr_desc, false);
    add_symbol(ctx, "__FUNC__", fval, NULL);
    // __func__ alias to same value
    add_symbol(ctx, "__func__", fval, NULL);

    // __LINE__ as integer constant 0 (i32)
    LLVMValueRef line_const = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
    TypeDescriptor const * i32_desc = type_descriptor_get_uint32_type(ctx->type_descriptors, true);
    TypedValue lval = create_typed_value(line_const, i32_desc, false);
    add_symbol(ctx, "__LINE__", lval, NULL);
}

static bool
is_a_function_declaration(c_grammar_node_t const * declarator_node)
{
    debug_info("%s: node type: %s", __func__, get_node_type_name_from_node(declarator_node));
    c_grammar_node_t const * parameters_list = search_parameters_list_in_declarator(declarator_node);

    /* We have a function declaration if the parameters list is found. */
    if (parameters_list != NULL)
    {
        debug_info("have function declaration");
    }
    return parameters_list != NULL;
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
    LLVMTypeRef var_type,
    LLVMTypeRef function_signature,
    char const * var_name,
    bool is_const,
    c_grammar_node_t const * initializer_expr_node,
    c_grammar_node_t const * decl_specifiers
)
{
    debug_info("Creating global for variable '%s'", var_name);

    /* Handle unsized array with string literal initializer */
    bool is_unsized_array = (LLVMGetTypeKind(var_type) == LLVMArrayTypeKind && LLVMGetArrayLength(var_type) == 0);

    if (is_unsized_array && initializer_expr_node && initializer_expr_node->type == AST_NODE_STRING_LITERAL)
    {
        char const * raw_text = initializer_expr_node->text;
        char const * decoded = decode_string(raw_text);
        char const * str = decoded ? decoded : raw_text;

        size_t str_len = strlen(str);
        LLVMTypeRef elem_type = LLVMGetElementType(var_type);
        var_type = LLVMArrayType(elem_type, (unsigned)(str_len + 1));

        LLVMValueRef global_var = LLVMAddGlobal(ctx->module, var_type, var_name);
        LLVMSetLinkage(global_var, LLVMInternalLinkage);
        LLVMSetGlobalConstant(global_var, true);
        LLVMSetInitializer(global_var, LLVMConstStringInContext(ctx->context, str, (unsigned)str_len, false));
        symbol_data_t data = {
            .function_signature = function_signature,
        };

        TypeDescriptor const * char_desc = type_descriptor_get_int8_type(ctx->type_descriptors, is_const);
        TypeDescriptor const * arr_desc = get_or_create_array_type(ctx->type_descriptors, char_desc, str_len + 1);
        TypedValue val = create_typed_value(global_var, arr_desc, true);
        add_symbol(ctx, var_name, val, &data);

        free((char *)decoded);
        return global_var;
    }

    /* Create the global variable */
    LLVMValueRef global_var = LLVMAddGlobal(ctx->module, var_type, var_name);
    LLVMSetLinkage(global_var, LLVMInternalLinkage);
    if (is_const)
    {
        LLVMSetGlobalConstant(global_var, true);
    }

    /* Zero-initialize by default for globals without an explicit initializer */
    if (var_type != NULL && LLVMGetTypeKind(var_type) != LLVMVoidTypeKind)
    {
        LLVMSetInitializer(global_var, LLVMConstNull(var_type));
    }

    /* Register symbol with pointee type for pointer types */
    LLVMTypeRef pointee_type = NULL;
    if (var_type != NULL)
    {
        LLVMTypeKind kind = LLVMGetTypeKind(var_type);
        debug_info(
            "create_global: var_type kind=%d (ArrayKind=%d, PointerKind=%d) for '%s', type ptr=%p",
            kind,
            LLVMArrayTypeKind,
            LLVMPointerTypeKind,
            var_name,
            (void *)var_type
        );
        if (kind == LLVMPointerTypeKind)
        {
            pointee_type = map_type_to_llvm_t(ctx, decl_specifiers, NULL);
            debug_info("create_global: pointer type, pointee_type set");
        }
        else if (kind == LLVMArrayTypeKind)
        {
            /* For arrays of pointers (e.g., char * arr[64]), get element type directly */
            LLVMTypeRef elem_type = LLVMGetElementType(var_type);
            debug_info("create_global: array type, elem_type kind=%d", LLVMGetTypeKind(elem_type));
            if (elem_type != NULL && LLVMGetTypeKind(elem_type) == LLVMPointerTypeKind)
            {
                pointee_type = elem_type; /* Use element type directly, don't re-parse declarator */
                debug_info("create_global: array of pointers, pointee_type set");
            }
        }
    }
    TypedValue val = (TypedValue){
        .value = global_var,
        .type = var_type,
        .pointee_type = pointee_type,
        .is_lvalue = true,
    };
    add_symbol(ctx, var_name, val, NULL);

    /* Handle explicit initializer if present */
    if (initializer_expr_node)
    {
        debug_info(
            "Initializer expr type: %d (%s), var_type kind: %d",
            initializer_expr_node->type,
            get_node_type_name_from_node(initializer_expr_node),
            LLVMGetTypeKind(var_type)
        );

        if (LLVMGetTypeKind(var_type) == LLVMArrayTypeKind && initializer_expr_node->type == AST_NODE_INITIALIZER_LIST)
        {
            LLVMSetInitializer(global_var, LLVMGetUndef(var_type));
        }
        else if (
            LLVMGetTypeKind(var_type) == LLVMStructTypeKind && initializer_expr_node->type == AST_NODE_INITIALIZER_LIST
        )
        {
            /* Build constant aggregate for struct initializer */
            LLVMValueRef const_aggregate = LLVMGetUndef(var_type);
            int current_index = 0;

            for (size_t i = 0; i < initializer_expr_node->list.count; ++i)
            {
                c_grammar_node_t const * list_entry = initializer_expr_node->list.children[i];
                c_grammar_node_t const * element_init = list_entry->initializer_list_entry.initializer;

                /* Unwrap from Initializer wrapper if needed */
                if (element_init->list.count > 0)
                {
                    element_init = element_init->list.children[0];
                }

                TypedValue elem_value = process_expression(ctx, element_init);
                if (elem_value.value != NULL)
                {
                    const_aggregate = LLVMBuildInsertValue(
                        ctx->builder, const_aggregate, elem_value.value, current_index, "init_elem"
                    );
                }
                current_index++;
            }
            LLVMSetInitializer(global_var, const_aggregate);
        }
        else
        {
            TypedValue initializer_value = process_expression(ctx, initializer_expr_node);
            if (initializer_value.value != NULL)
            {
                LLVMSetInitializer(global_var, initializer_value.value);
            }
        }
    }

    return global_var;
}

static LLVMTypeRef
generate_function_signature(parameter_definitions_t params, LLVMTypeRef return_type)
{
    LLVMTypeRef empty_params[1] = {0};
    LLVMTypeRef func_type
        = LLVMFunctionType(return_type, params.count > 0 ? params.types : empty_params, (unsigned)params.count, false);

    debug_info(
        "%s: generated function type %d for return type kind %d with %zu params",
        __func__,
        LLVMGetTypeKind(func_type),
        LLVMGetTypeKind(return_type),
        params.count
    );
    return func_type;
}

static void
parameter_definitions_cleanup(parameter_definitions_t * params)
{
    free(params->names);
    free(params->types);
    free(params->pointee_types);
}

static bool
parameter_definitions_init(parameter_definitions_t * params, c_grammar_node_t const * params_list_node)
{
    if (params_list_node == NULL || params_list_node->type != AST_NODE_PARAMETER_LIST)
    {
        debug_error("Invalid parameter list node");
        return false;
    }

    size_t params_list_count = params_list_node->list.count;
    if (params_list_count > 1 && params_list_node->list.children[params_list_count - 1]->type == AST_NODE_ELLIPSIS)
    {
        params_list_count--; /* Exclude ellipsis from count */
    }

    size_t count = params_list_count / 3;
    params->count = count;
    params->names = calloc(count, sizeof(*params->names));
    params->types = calloc(count, sizeof(*params->types));
    params->pointee_types = calloc(params->count, sizeof(*params->pointee_types));

    if (params->names == NULL || params->types == NULL || params->pointee_types == NULL)
    {
        debug_error("failed to init param definitions");
        parameter_definitions_cleanup(params);
        return false;
    }

    return true;
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

    TypeDescriptor const * type_desc = resolve_type_descriptor(ctx, decl_specifiers, declarator_node);
    if (type_desc != NULL)
    {
        debug_info(
            "%s: got type desc: type %d pointee type: %d",
            __func__,
            (int)LLVMGetTypeKind(type_desc->llvm_type),
            (type_desc->pointee != NULL && type_desc->pointee->llvm_type != NULL)
                ? (int)LLVMGetTypeKind(type_desc->pointee->llvm_type)
                : -1
        );
    }
    else
    {
        debug_info("%s: no type desc found", __func__);
    }
    c_grammar_node_t const * initializer_expr_node = NULL;
    if (init_decl_initializer != NULL && init_decl_initializer->list.count > 0)
    {
        initializer_expr_node = init_decl_initializer->list.children[0];
    }
    char const * var_name = NULL;
    c_grammar_node_t const * direct_decl_node = declarator_node->declarator.direct_declarator;
    LLVMTypeRef function_signature = NULL;

    // For regular variables: DirectDeclarator -> Identifier
    // For function pointers: DirectDeclarator -> FunctionPointerDeclarator -> {Pointer, Identifier,
    // DeclaratorSuffix*}
    if (direct_decl_node && direct_decl_node->list.count > 0)
    {
        c_grammar_node_t * first_child = direct_decl_node->list.children[0];
        debug_info(
            "Direct decl first child type: %s (%u)", get_node_type_name_from_type(first_child->type), first_child->type
        );
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
            // FunctionPointerDeclarator contains Pointer, Identifier, DeclaratorSuffixList
            char const * id = first_child->function_pointer_declarator.identifier->text;
            if (id != NULL)
            {
                var_name = (char *)id;
            }
            c_grammar_node_t const * params_list = search_parameters_list_in_declarator(declarator_node);
            parameter_definitions_t params = extract_function_parameters(ctx, params_list);

            LLVMTypeRef return_type = map_type_to_llvm_t(ctx, decl_specifiers, NULL);
            function_signature = generate_function_signature(params, return_type);
            parameter_definitions_cleanup(&params);
            debug_info("Function pointer signature: %p", function_signature);
        }
    }

    LLVMTypeRef var_type = map_type_to_llvm_t(ctx, decl_specifiers, declarator_node);
    debug_info(
        "Processing declaration for '%s', type %p kind=%d",
        var_name ? var_name : "NULL",
        (void *)var_type,
        var_type ? (int)LLVMGetTypeKind(var_type) : -1
    );
    if (var_type == NULL)
    {
        debug_info("NO VAR TYPE FOUND for var: %s", var_name);
        return;
    }

    if (var_name != NULL)
    {
        debug_info("got var name: %s", var_name);
        LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
        bool is_global = current_block == NULL;
        bool is_static = false;
        bool is_const = false;
        if (decl_specifiers != NULL && decl_specifiers->type == AST_NODE_NAMED_DECL_SPECIFIERS)
        {
            is_static = decl_specifiers->decl_specifiers.storage.has_static;
            is_const = decl_specifiers->decl_specifiers.type.is_const;
        }

        if (is_static || is_global)
        {
            if (is_a_function_declaration(declarator_node))
            {
                debug_info("skipping function decl");
                return;
            }

            /* Create static global variable using helper */
            create_global_variable(
                ctx, var_type, function_signature, var_name, is_const, initializer_expr_node, decl_specifiers
            );
        }
        else
        {
            // Inside a function - use stack allocation
            LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, var_type, var_name);

            // Find struct name for pointer-to-struct types
            char const * struct_name = NULL;
            if (decl_specifiers != NULL)
            {
                c_grammar_node_t const * typedef_specifier = decl_specifiers->decl_specifiers.typedef_specifier;

                if (typedef_specifier != NULL)
                {
                    char const * typedef_name = extract_typedef_name(typedef_specifier);
                    if (typedef_name != NULL)
                    {
                        TypeDescriptor const * typedef_type_desc = find_typedef_type_descriptor(ctx, typedef_name);
                        if (typedef_type_desc != NULL)
                        {
                            type_info_t * info
                                = scope_find_type_by_type_descriptor(ctx->current_scope, typedef_type_desc);
                            if (info != NULL)
                            {
                                struct_name = info->tag;
                            }
                        }
                    }
                }

                /* Iterate through DeclarationSpecifiers children */
                c_grammar_node_t const * specifiers_list = decl_specifiers->decl_specifiers.type_specifiers;
                for (size_t si = 0; si < specifiers_list->list.count && !struct_name; si++)
                {
                    c_grammar_node_t * child = specifiers_list->list.children[si];

                    /* Handle terminal TypeSpecifier (typedef name like "FloatMember") */
                    if (child->text != NULL)
                    {
                        /* First try struct list */
                        if (find_type_by_tag(ctx, child->text))
                        {
                            struct_name = child->text;
                        }
                        else
                        {
                            /* Try typedef - get underlying struct name */
                            TypeDescriptor const * typedef_type_desc = find_typedef_type_descriptor(ctx, child->text);
                            if (typedef_type_desc != NULL)
                            {
                                /* Need to find the struct name from the typedef entry */
                                /* For now, set struct_name to the typedef name itself - */
                                /* we'll need to find the actual underlying struct */
                                /* This is a limitation - we'll fix by looking up the typedef entry directly
                                 */
                                /* Actually, let's look up the struct by the type */
                                type_info_t const * info
                                    = scope_find_type_by_type_descriptor(ctx->current_scope, typedef_type_desc);
                                if (info != NULL)
                                {
                                    struct_name = info->tag;
                                }
                            }
                        }
                    }
                    /* Handle non-terminal TypeSpecifier */
                    else
                    {
                        /* Try to extract struct/union/enum tag directly */
                        char const * name_from_struct = extract_struct_or_union_or_enum_tag(child);
                        if (name_from_struct != NULL)
                        {
                            if (find_type_by_tag(ctx, name_from_struct))
                            {
                                struct_name = name_from_struct;
                            }
                        }
                    }
                }
            }
            bool added_using_type_descriptor = false;
            // Compute pointee_type for pointer variables
            // We need to compute this BEFORE map_type adds pointer types, because
            // LLVMGetElementType returns NULL/invalid for opaque pointers
            LLVMTypeRef pointee_type = NULL;
            if (function_signature != NULL)
            {
                debug_info("using function signature as pointee type");
                pointee_type = function_signature;
            }
            else if (decl_specifiers != NULL)
            {
                /*
                 * Eventually, resolve_type_descriptor should always succeed and the code within the
                 * if (!added_using_type_descriptor) condition can be removed.
                 */

                if (type_desc != NULL)
                {
                    debug_info("DDDDDDD got type desc");
                    TypedValue sym_val = create_typed_value(alloca_inst, type_desc, true);
                    debug_info("%s: adding symbol %s type using type descriptor", __func__, var_name);
                    add_symbol_with_struct(ctx, var_name, sym_val, struct_name, NULL);
                    added_using_type_descriptor = true;
                }
                else
                {
                    debug_info("DDDDDDD failed to get type desc");
                    print_ast(decl_specifiers);
                }

                if (type_desc != NULL)
                {
                    debug_info("using type descriptor for to provide pointee");
                    pointee_type = type_desc->pointee != NULL ? type_desc->pointee->llvm_type : NULL;
                }
                else
                {
                    debug_info("computing pointee type decl specs: %p", decl_specifiers);
                    // Get the base type from specifiers
                    pointee_type = map_type_to_llvm_t(ctx, decl_specifiers, NULL);
                    // If there's a declarator with pointers, this is the pointee type
                    dump_llvm_type("Node declaration pointee type", pointee_type);
                    if (pointee_type != NULL && declarator_node != NULL)
                    {
                        // Check if there are pointers in the declarator
                        bool has_pointer = declarator_node->declarator.pointer_list->list.count > 0;
                        if (!has_pointer)
                        {
                            debug_info("no pointee type");
                            pointee_type = NULL;
                        }
                    }
                }
            }

            // Extract pointer qualifiers at each level of indirection
            pointer_qualifiers_t pointer_quals = {0};
            if (declarator_node && declarator_node->declarator.pointer_list)
            {
                extract_pointer_qualifiers(declarator_node->declarator.pointer_list, decl_specifiers, &pointer_quals);
            }

            // For pointer types, we don't set symbol_is_const here.
            // All const checking is done via pointer_qualifiers:
            // - is_const[level-1] for direct pointer assignment (can't reassign pointer)
            // - is_const[0] for data pointed to (can't modify through pointer)
            if (!added_using_type_descriptor)
            {
                symbol_data_t symbol_data = {
                    .pointer_qualifiers = pointer_quals,
                    .function_signature = function_signature,
                };
                TypedValue sym_val = (TypedValue){
                    .value = alloca_inst,
                    .type = var_type,
                    .pointee_type = pointee_type,
                    .is_lvalue = true,
                };
                // Also store type_info if we have it - using helper for backward compat
                sym_val.type_info = NULL; // TODO: create TypeDescriptor from var_type
                debug_info(
                    "node decl adding symbol %s type %p (%d), pointee_type %p (%d)",
                    var_name,
                    (void *)var_type,
                    var_type != NULL ? (int)LLVMGetTypeKind(var_type) : -1,
                    (void *)pointee_type,
                    pointee_type != NULL ? (int)LLVMGetTypeKind(pointee_type) : -1
                );
                add_symbol_with_struct(ctx, var_name, sym_val, struct_name, &symbol_data);
            }

            // Process initializer if present
            if (initializer_expr_node != NULL)
            {
                if ((LLVMGetTypeKind(var_type) == LLVMArrayTypeKind || LLVMGetTypeKind(var_type) == LLVMStructTypeKind)
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
                                c_grammar_node_t const * list_entry = initializer_expr_node->list.children[i];
                                c_grammar_node_t const * initializer = list_entry->initializer_list_entry.initializer;

                                TypedValue tvalue = process_expression(ctx, initializer);
                                if (tvalue.value != NULL)
                                {
                                    ensure_rvalue(ctx, "init_elem_ptr", tvalue);

                                    // Create GEP to element
                                    LLVMValueRef indices[2];
                                    indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                                    indices[1] = LLVMConstInt(ctx->ref_type.i32_type, current_index, false);
                                    LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
                                        ctx->builder, var_type, alloca_inst, indices, 2, "init_elem_ptr"
                                    );
                                    aligned_store(ctx, ctx->builder, tvalue.value, elem_type, elem_ptr);
                                }
                                current_index++;
                            }
                        }
                        else
                        {
                            // Regular array or struct initializer
                            process_initializer_list(ctx, alloca_inst, var_type, initializer_expr_node, NULL);
                        }
                    }
                    else
                    {
                        process_initializer_list(ctx, alloca_inst, var_type, initializer_expr_node, NULL);
                    }
                }
                else
                {
                    TypedValue initializer_res = process_expression(ctx, initializer_expr_node);
                    LLVMValueRef initializer_value = initializer_res.value;
                    if (initializer_value != NULL)
                    {
                        TypedValue cast_value = cast_value_to_type(ctx, initializer_res, var_type, false);
                        aligned_store(ctx, ctx->builder, cast_value.value, cast_value.type, alloca_inst);
                        debug_info("stored");
                    }
                }
            }
        }
    }
}

static parameter_definitions_t
extract_function_parameters(ir_generator_ctx_t * ctx, c_grammar_node_t const * params_list)
{
    debug_info("%s", __func__);
    parameter_definitions_t params = {0};

    if (params_list != NULL && params_list->list.count > 0)
    {
        // Each parameter typically has [KwExtension, TypeSpecifier, Declarator]
        if (!parameter_definitions_init(&params, params_list))
        {
            ir_gen_error(&ctx->errors, "Memory error");
            return params;
        }

        for (size_t i = 0; i < params.count; ++i)
        {
            c_grammar_node_t const * p_spec = params_list->list.children[i * 3 + 1];
            c_grammar_node_t const * p_decl = params_list->list.children[i * 3 + 2];

            params.types[i] = map_type_to_llvm_t(ctx, p_spec, p_decl);

            c_grammar_node_t const * p_direct = p_decl->declarator.direct_declarator;
            if (p_direct && p_direct->list.count > 0)
            {
                c_grammar_node_t const * first_child = p_direct->list.children[0];
                if (first_child->type == AST_NODE_IDENTIFIER)
                {
                    params.names[i] = first_child->text;
                }
                else if (first_child->type == AST_NODE_DECLARATOR)
                {
                    // Nested declarator (e.g., for function pointers like *name)
                    // Find the DirectDeclarator inside and get the Identifier
                    c_grammar_node_t const * nested_direct = first_child->declarator.direct_declarator;
                    if (nested_direct && nested_direct->list.count > 0
                        && nested_direct->list.children[0]->type == AST_NODE_IDENTIFIER)
                    {
                        params.names[i] = nested_direct->list.children[0]->text;
                    }
                }
                else if (first_child->type == AST_NODE_FUNCTION_POINTER_DECLARATOR)
                {
                    // FunctionPointerDeclarator: contains Pointer, Identifier, DeclaratorSuffix*
                    char const * id = first_child->function_pointer_declarator.identifier->text;
                    if (id != NULL)
                    {
                        params.names[i] = id;
                    }
                }
            }
        }
    }

    return params;
}

static void
process_function_definition(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Create function scope for parameters and body
    scope_push(ctx);
    // --- Handle Function Definition ---
    c_grammar_node_t const * decl_specifiers_node = node->function_definition.declaration_specifiers;
    c_grammar_node_t const * declarator_node = node->function_definition.declarator;
    c_grammar_node_t const * compound_stmt_node = node->function_definition.body;

    if (decl_specifiers_node == NULL || declarator_node == NULL || compound_stmt_node == NULL)
    {
        debug_error("Function definition is missing declaration specifiers, declarator, or body.");
        scope_pop(ctx);
        return;
    }

    TypeDescriptor const * type_desc = resolve_type_descriptor(ctx, decl_specifiers_node, declarator_node);
    if (type_desc == NULL)
    {
        debug_info("%s: failed to get type desc for function definition", __func__);
        return;
    }

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
                ir_gen_error(&ctx->errors, "Function '%s' redeclared with different signature.", func_name);
                scope_pop(ctx);
                return;
            }

            if (decl->has_definition)
            {
                ir_gen_error(&ctx->errors, "Function '%s' already has a body.", func_name);
                scope_pop(ctx);
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
        TypedValue decl = create_typed_value(NULL, type_desc, false);
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
            scope_pop(ctx);

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
    for (size_t i = 0; i < type_desc->function_metadata.param_count; ++i)
    {
        char const * p_name = type_desc->function_metadata.names[i];
        TypeDescriptor const * p_type_desc = type_desc->function_metadata.params[i];
        debug_info("Processing parameter %zu name: %s type desc %p", i, p_name, (void *)p_type_desc);
        LLVMTypeRef p_type = p_type_desc->llvm_type;
        debug_info("llvm type %p", (void *)p_type);
        debug_info("llvm type kind %d", LLVMGetTypeKind(p_type));
        LLVMValueRef param_val = LLVMGetParam(func, (unsigned)i);
        debug_info("1");
        LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, p_type, p_name != NULL ? p_name : "");
        debug_info("2");
        aligned_store(ctx, ctx->builder, param_val, p_type, alloca_inst);
        debug_info("3");

        if (p_name != NULL)
        {

            TypedValue p_val = create_typed_value(alloca_inst, p_type_desc, true);

            // If your descriptor is a struct or points to one,
            // the symbol table can now just check p_type->kind
            add_symbol(ctx, p_name, p_val, NULL);
        }
        debug_info("Processed parameter %zu", i);
    }
    debug_info("processed parameters");
    // Process the compound statement (function body).
    process_ast_node(ctx, compound_stmt_node);
    if (ctx->errors.fatal)
    {
        return;
    }

    // --- Add a default return if the function doesn't end with one ---
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
    {
        if (LLVMGetTypeKind(type_desc->function_metadata.return_type->llvm_type) == LLVMVoidTypeKind)
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            LLVMBuildRet(ctx->builder, LLVMConstInt(ctx->ref_type.i32_type, 0, false));
        }
    }

    // Pop function scope
    scope_pop(ctx);

    /* Clear the builder insert point so subsequent declarations don't
     * mistakenly think we're inside a function body. */
    LLVMClearInsertionPosition(ctx->builder);
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

    if (!visit_stack_push(node))
    {
        return; /* cycle detected, abort */
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
        process_function_definition(ctx, node);
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
            existing_typedef_info = scope_lookup_typedef_entry_by_name(ctx->current_scope, typedef_name);
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
                    type_kind_t kind = scope_lookup_kind_by_type_descriptor(ctx->current_scope, typedef_type_desc);
                    debug_info("%s existing info: %p", __func__, (void *)existing_typedef_info);
                    scope_typedef_entry_t typedef_entry = {
                        .kind = kind,
                        .name = strdup(typedef_name),
                        .type_desc = typedef_type_desc,
                    };
                    scope_add_typedef_entry(ctx->current_scope, typedef_entry);
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
                                    scope_add_typedef_forward_decl(
                                        ctx->current_scope, typedef_name, tag_name_node->text, kind
                                    );
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
                        type_info = register_tagged_struct_or_union_definition(ctx, struct_def_node, struct_tag, kind);
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
                    scope_add_typedef_entry(ctx->current_scope, typedef_entry);
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
                    scope_add_typedef_entry(ctx->current_scope, typedef_entry);
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
                        scope_add_typedef_entry(ctx->current_scope, typedef_entry);
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
            LLVMTypeRef cond_type = cond_res.type;
            if (LLVMGetTypeKind(cond_type) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(cond_type) != 1)
            {
                LLVMValueRef zero = LLVMConstNull(cond_type);
                cond_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_res.value, zero, "for_cond_bool");
            }
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
        LLVMTypeRef cond_type = condition_res.type;
        if (LLVMGetTypeKind(cond_type) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(cond_type) != 1)
        {
            LLVMValueRef zero = LLVMConstNull(cond_type);
            condition_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, condition_res.value, zero, "cond_bool");
        }

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
        LLVMTypeRef cond_type = condition_res.type;
        if (LLVMGetTypeKind(cond_type) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(cond_type) != 1)
        {
            LLVMValueRef zero = LLVMConstNull(cond_type);
            condition_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, condition_res.value, zero, "do_cond_bool");
        }
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
        switch_val = ensure_rvalue(ctx, "switch_rval", switch_val);
        if (switch_val.value == NULL)
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
        condition_val = ensure_rvalue(ctx, "if_cond_rval", condition_val);
        if (condition_val.value == NULL)
        {
            debug_error("Failed to process condition for IfStatement.");
            return;
        }

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "then");
        LLVMBasicBlockRef else_block
            = else_node != NULL ? LLVMAppendBasicBlockInContext(ctx->context, current_func, "else") : NULL;
        LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx->context, current_func, "if_merge");

        // If it's an i32, we need to check if it's != 0 to get an i1
        debug_info(
            "IF type: %d width: %u", LLVMGetTypeKind(condition_val.type), LLVMGetIntTypeWidth(condition_val.type)
        );
        LLVMValueRef cond_val = condition_val.value;
        if (LLVMGetTypeKind(condition_val.type) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(condition_val.type) != 1)
        {
            debug_info("converting to bool");
            LLVMValueRef zero = LLVMConstNull(condition_val.type);
            cond_val = LLVMBuildICmp(ctx->builder, LLVMIntNE, condition_val.value, zero, "if_cond_bool");
        }
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
        c_grammar_node_t const * expr_node = node->return_statement.expression;
        if (expr_node != NULL)
        {
            TypedValue return_value = process_expression(ctx, expr_node);
            return_value = ensure_rvalue(ctx, "return_rval", return_value);

            if (return_value.value == NULL)
            {
                debug_error("Failed to process return expression.");
                return;
            }
            LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
            LLVMTypeRef func_ret_type = LLVMGetReturnType(LLVMGlobalGetValueType(parent_func));

            return_value = cast_value_to_type(ctx, return_value, func_ret_type, true);

            LLVMBuildRet(ctx->builder, return_value.value);
        }
        else
        {
            // Handle 'return;' for functions with no return expression.
            // Return a zero of the function's declared return type, or build a void return if appropriate.
            LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
            LLVMTypeRef func_ret_type = LLVMGetReturnType(LLVMGlobalGetValueType(parent_func));
            if (LLVMGetTypeKind(func_ret_type) == LLVMVoidTypeKind)
            {
                LLVMBuildRetVoid(ctx->builder);
            }
            else
            {
                // Create a zero constant of the correct integer/float type.
                LLVMValueRef zero_const;
                if (LLVMGetTypeKind(func_ret_type) == LLVMIntegerTypeKind)
                {
                    unsigned bits = LLVMGetIntTypeWidth(func_ret_type);
                    LLVMTypeRef int_type = LLVMIntTypeInContext(ctx->context, bits);
                    zero_const = LLVMConstInt(int_type, 0, false);
                }
                else if (LLVMGetTypeKind(func_ret_type) == LLVMFloatTypeKind)
                {
                    zero_const = LLVMConstReal(func_ret_type, 0.0);
                }
                else if (LLVMGetTypeKind(func_ret_type) == LLVMDoubleTypeKind)
                {
                    zero_const = LLVMConstReal(func_ret_type, 0.0);
                }
                else
                {
                    // Fallback: bitcast a zero integer of same size.
                    unsigned bits = LLVMGetIntTypeWidth(func_ret_type);
                    LLVMTypeRef int_type = LLVMIntTypeInContext(ctx->context, bits);
                    zero_const = LLVMConstInt(int_type, 0, false);
                    zero_const = LLVMBuildIntCast2(ctx->builder, zero_const, func_ret_type, false, "zero_cast");
                }
                LLVMBuildRet(ctx->builder, zero_const);
            }
        }
        break;
    }
    case AST_NODE_GOTO_STATEMENT:
    {
        char const * label_name = node->goto_statement.label->text;
        LLVMBasicBlockRef target = scope_get_or_create_label(ctx->current_scope, label_name);
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
        LLVMBasicBlockRef label_block = scope_get_or_create_label(ctx->current_scope, label_name);

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
    visit_stack_pop(node);
}

static void
process_ast_node(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (ctx->errors.fatal)
    {
        return; /* Stop processing if a fatal error has occurred */
    }
    debug_info("%s node type: %s (%u)\n", __func__, get_node_type_name_from_node(node), node->type);
    print_ast_with_label(node, "process_ast");

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

    TypedValue res;
    debug_info("%s: %s", __func__, identifier_node->text);
    find_symbol(ctx, identifier_node->text, &res);

    return res;
}

static TypedValue
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
        int_type = ctx->ref_type.i32_type;
    }

    return (TypedValue){
        .value = LLVMConstInt(int_type, int_node->integer_literal.value, !int_node->integer_literal.is_unsigned),
        .type = int_type,
    };
}

static TypedValue
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

    return (TypedValue){
        .value = LLVMConstReal(float_type, value),
        .type = float_type,
    };
}

static TypedValue
process_string_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Handle string literals like "Hello".
    if (node->text == NULL)
    {
        return NullTypedValue;
    }

    char const * raw_text = node->text;
    char const * decoded = decode_string(raw_text);
    size_t len = strlen(decoded);
    LLVMTypeRef str_type = LLVMArrayType(ctx->ref_type.i8_type, (unsigned)(len + 1));
    LLVMValueRef global_str;

    if (LLVMGetInsertBlock(ctx->builder) != NULL)
    {
        global_str = LLVMBuildGlobalStringPtr(ctx->builder, decoded, "str_tmp");
    }
    else
    {
        /* No insert point (global scope) — create a global string constant directly */
        LLVMValueRef global = LLVMAddGlobal(ctx->module, str_type, "str_tmp");
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(global, true);
        LLVMSetInitializer(global, LLVMConstStringInContext(ctx->context, decoded, (unsigned)len, false));
        LLVMValueRef indices[2]
            = {LLVMConstInt(ctx->ref_type.i32_type, 0, false), LLVMConstInt(ctx->ref_type.i32_type, 0, false)};
        global_str = LLVMConstInBoundsGEP2(str_type, global, indices, 2);
    }

    free((char *)decoded);

    return (TypedValue){
        .value = global_str,
        .type = str_type,
    };
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

    return (TypedValue){
        .value = LLVMConstInt(ctx->ref_type.i8_type, value, false),
        .type = ctx->ref_type.i8_type,
    };
}

static TypedValue
process_postfix_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // AST structure for PostfixExpression: [BaseExpression, SuffixPart1, SuffixPart2, ...]
    c_grammar_node_t const * base_node = node->postfix_expression.base_expression;
    TypedValue base_value = NullTypedValue;
    TypedValue current_value = NullTypedValue;
    bool have_ptr = false;
    bool did_member_access = false; /* tracks whether we've done at least one member access */

    debug_info("%s", __func__);
    print_ast_with_label(node, "node");
    print_ast_with_label(base_node, "base");

    // Check if base is a symbol (for array access)
    // Do this before process_expression to avoid double GEP for arrays
    if (base_node->type == AST_NODE_CAST_EXPRESSION)
    {
        base_node = base_node->cast_expression.expression;
    }
    if (base_node->type == AST_NODE_IDENTIFIER)
    {
        char const * var_name = base_node->text;
        TypedValue var;

        if (find_symbol(ctx, var_name, &var))
        {
            current_value = var;
            dump_typed_value("initial current value from identifier", current_value);
            have_ptr = true;
        }
    }

    c_grammar_node_t const * postfix_parts_node = node->postfix_expression.postfix_parts;
    print_ast_with_label(postfix_parts_node, "postfix_parts");

    {
        // For function calls, don't call process_expression on the identifier
        // The function call suffix handling will get the function pointer directly
        bool has_func_call_suffix = false;

        for (size_t i = 0; i < postfix_parts_node->list.count; ++i)
        {
            if (postfix_parts_node->list.children[i]->type == AST_NODE_OPTIONAL_ARGUMENT_LIST)
            {
                has_func_call_suffix = true;
                break;
            }
        }
        if (!has_func_call_suffix)
        {
            debug_info("processing base node");
            base_value = process_expression(ctx, base_node);
            dump_typed_value("base value post processing base AST node", base_value);
        }

        // If base_val is a pointer type and have_ptr is false, set up current_ptr for member access
        // This handles cases like ((Point *)ptr)->member where base is a cast expression
        if (/*!have_ptr && */ base_value.value != NULL && base_value.type != NULL)
        {
            LLVMTypeRef base_type = base_value.type;

            current_value = base_value;
            if (LLVMGetTypeKind(base_type) == LLVMPointerTypeKind)
            {
                // For member access, we need the pointee type, not the pointer type
                // For non-opaque, LLVMGetElementType works. For opaque, leave current_type as NULL
                have_ptr = true;
                debug_info(
                    "Set current_ptr from base_val pointer for member access, current_type kind=%d",
                    LLVMGetTypeKind(current_value.type)
                );
                dump_typed_value("current and base values from process_expression", current_value);
            }
        }
    }

    for (size_t i = 0; i < postfix_parts_node->list.count; ++i)
    {
        c_grammar_node_t * suffix = postfix_parts_node->list.children[i];
        print_ast_with_label(suffix, "suffix");

        if (suffix->type == AST_NODE_OPTIONAL_ARGUMENT_LIST)
        {
            // 1. Determine what we are trying to call
            TypedValue callable = current_value.value != NULL ? current_value : base_value;

            callable = ensure_rvalue(ctx, "func_ptr_load", callable);

            LLVMTypeRef func_sig = callable.pointee_type;

            if (func_sig == NULL && base_node->type == AST_NODE_IDENTIFIER && base_node->text != NULL)
            {
                char const * name = base_node->text;
                TypedValue var;
                if (find_symbol(ctx, name, &var))
                {
                    // This is where 'func' is found as a parameter!
                    // 1. Get the value (the pointer address)
                    TypedValue rval = ensure_rvalue(ctx, "func_ptr_load", var);

                    // 2. Use the rval.value for the call, NOT the name
                    callable.value = rval.value;
                    func_sig = rval.pointee_type;
                }
                else
                {
                    debug_info("no function signature - will auto-declare: '%s'", base_node->text);
                    // For undeclared functions like printf, auto-declare as variadic returning i32
                    // with no required arguments to support different call patterns
                    callable.value = LLVMGetNamedFunction(ctx->module, base_node->text);
                    debug_info("get named func for %s returned %p", base_node->text, (void *)callable.value);
                    if (callable.value != NULL)
                    {
                        func_sig = LLVMGlobalGetValueType(callable.value);
                    }
                    else
                    {
                        LLVMTypeRef ret_type = ctx->ref_type.i32_type;
                        func_sig = LLVMFunctionType(ret_type, NULL, 0, true);
                        callable.value = LLVMAddFunction(ctx->module, base_node->text, func_sig);
                        callable.type = LLVMGetReturnType(func_sig);
                    }
                }
            }
            if (func_sig == NULL)
            {
                debug_error("Could not resolve function for call.");
                ir_gen_error(&ctx->errors, "Could not resolve function for call.");
                return NullTypedValue;
            }

            char const * call_name = "";
            LLVMTypeRef return_type = LLVMGetReturnType(func_sig);
            if (return_type != LLVMVoidTypeInContext(ctx->context))
            {
                call_name = "call_tmp";
            }

            // Handle function call. Arguments might be children directly or in an ArgumentList
            size_t num_args = 0;
            TypedValue * args = NULL;

            if (suffix->list.count > 0)
            {
                num_args = suffix->list.count;
                args = malloc(num_args * sizeof(*args));
                for (size_t j = 0; j < num_args; ++j)
                {
                    args[j] = process_expression(ctx, suffix->list.children[j]);
                    args[j] = ensure_rvalue(ctx, "postfix_arg_list_arg_rval", args[j]);
                }
            }

            // For zero-argument calls, pass NULL for args (per LLVM C API docs)
            LLVMValueRef * call_args = NULL;
            if (num_args > 0)
            {
                call_args = calloc(num_args, sizeof *call_args);
                for (size_t i = 0; i < num_args; i++)
                {
                    call_args[i] = args[i].value;
                }
            }

            LLVMValueRef call_value
                = LLVMBuildCall2(ctx->builder, func_sig, callable.value, call_args, (unsigned)num_args, call_name);
            free(call_args);
            free(args);

            TypedValue call_res = {
                .type = return_type,
            };
            if (return_type != LLVMVoidTypeInContext(ctx->context))
            {
                // For void functions, leave base_val set to NULL (void calls don't produce values)
                call_res.value = call_value;
            }

            base_value = call_res;
        }
        else if (suffix->type == AST_NODE_ARRAY_SUBSCRIPT)
        {
            // Array subscript: use helper function
            if (have_ptr && current_value.type != NULL)
            {
                if (LLVMGetTypeKind(current_value.type) == LLVMPointerTypeKind)
                {
                    dump_typed_value("prior to process_array_subscript - ensuring rvalue", current_value);
                    // We are holding the address of a pointer variable (e.g., &o.inner).
                    // We need the ACTUAL address stored there (e.g., the value of inner).
                    current_value = ensure_rvalue(ctx, "subscript_ptr_load", current_value);
                    // FIXME - This should be inside process_array_subscript, but need to refactor to pass TypedValue
                    // first.
                }
                dump_typed_value("to process_array_subscript", current_value);
                TypedValue new_value = process_array_subscript(ctx, suffix, current_value);
                dump_typed_value("from process_array_subscript", current_value);
                if (new_value.value != NULL)
                {
                    // Update current_ptr for next iteration
                    current_value = new_value;
                    base_value = current_value;
                    debug_info(
                        "current_type kind is: now %u pointee: %u",
                        LLVMGetTypeKind(current_value.type),
                        base_value.pointee_type != NULL ? (int)LLVMGetTypeKind(base_value.pointee_type) : -1
                    );
                }
                else
                {
                    debug_error("Could not process array subscript.");
                    return NullTypedValue;
                }
            }
        }
        else if (suffix->type == AST_NODE_MEMBER_ACCESS_DOT || suffix->type == AST_NODE_MEMBER_ACCESS_ARROW)
        {
            // Struct member access: s.x or p->x
            // AST_MEMBER_ACCESS_DOT/ARROW children: [Dot/Arrow, Identifier]
            char const * member_name = suffix->identifier.identifier->text;
            if (member_name == NULL)
            {
                debug_error("Could not find member name in member access AST node.");
                return NullTypedValue;
            }

            debug_info(
                "\n\n\n\nMember access start: base_val=%p, have_ptr=%d, current_ptr=%p, current_type= %p (%d) "
                "current_pointee_type: %p "
                "(%d)",
                (void *)base_value.value,
                have_ptr,
                (void *)current_value.value,
                current_value.type,
                current_value.type != NULL ? (int)LLVMGetTypeKind(current_value.type) : -1,
                current_value.pointee_type,
                current_value.pointee_type != NULL ? (int)LLVMGetTypeKind(current_value.pointee_type) : -1
            );

            bool is_arrow = (suffix->type == AST_NODE_MEMBER_ACCESS_ARROW);

            if (is_arrow)
            {
                // base_value is currently the result of the PREVIOUS step (e.g., &o.inr)
                // We load it to get the address of the 'Inner' struct
                base_value = ensure_rvalue(ctx, "arrow_base_load", base_value);

                if (base_value.pointee_type)
                {
                    // Now swap the type so the member lookup looks at 'Inner', not 'ptr'
                    base_value.type = base_value.pointee_type;
                }
            }
            TypedValue struct_value = base_value;
            dump_typed_value("new struct value", struct_value);

            // Handle case where base_val is NULL but we have current_ptr from array subscript
            // OR for arrow access where current_ptr is set
            if (current_value.value != NULL && have_ptr
                && ((struct_value.value == NULL && current_value.type != NULL) || is_arrow))
            {
                // current_ptr points to the element, current_type is its type
                LLVMTypeKind type_kind = current_value.type ? LLVMGetTypeKind(current_value.type) : 0;
                if (type_kind == LLVMStructTypeKind)
                {
                    struct_value = current_value;
                }
                else if (type_kind == LLVMPointerTypeKind)
                {
                    LLVMTypeRef elem_type = LLVMGetElementType(current_value.type);
                    if (elem_type && LLVMGetTypeKind(elem_type) == LLVMStructTypeKind)
                    {
                        struct_value.value = current_value.value;
                        struct_value.type = elem_type;
                    }
                }
                else
                {
                    debug_error("Member access on unsupported type kind %d.", type_kind);
                    ir_gen_error(&ctx->errors, "Member access on unsupported type kind %d.", type_kind);
                }
            }

            if (struct_value.value == NULL || struct_value.type == NULL)
            {
                debug_error(
                    "Struct value didn't have required fields - value: %p, type: %p",
                    (void *)struct_value.value,
                    (void *)struct_value.type
                );
                ir_gen_error(&ctx->errors, "Struct value didn't have required fields");
            }

            debug_info(
                "Checking struct_type after assignment: is_arrow=%d, struct_type=%p (kind=%d), "
                "base_node type=%d, current_type: %d, is_lvalue: %d",
                is_arrow,
                (void *)struct_value.type,
                struct_value.type ? (int)LLVMGetTypeKind(struct_value.type) : -1,
                base_node ? (int)base_node->type : -1,
                current_value.type != NULL ? (int)LLVMGetTypeKind(current_value.type) : -1,
                struct_value.is_lvalue
            );

            // For arrow access with cast expressions like ((Point *)ptr)->member
            // if current_type is already the struct type, use it directly
            // OR if base is a cast expression, get type from the cast's target type
            if (is_arrow && current_value.type != NULL && LLVMGetTypeKind(current_value.type) == LLVMPointerTypeKind)
            {
                LLVMTypeRef pointee = LLVMGetElementType(current_value.type);
                if (pointee != NULL && LLVMGetTypeKind(pointee) == LLVMStructTypeKind)
                {
                    struct_value.type = pointee;
                    struct_value.value = current_value.value;
                }
            }

            debug_info(
                "decision: is_arrow=%d, did_member_access: %d, struct_val=%p, struct_type=%d (%p), current_type: "
                "%p (%d)",
                is_arrow,
                did_member_access,
                (void *)struct_value.value,
                LLVMGetTypeKind(struct_value.type),
                (void *)struct_value.type,
                current_value.type,
                current_value.type != NULL ? (int)LLVMGetTypeKind(current_value.type) : -1
            );
            // For LLVM 18+ opaque pointers, use struct name from symbol table
            // For arrow access, we need to look up the Pointee type from the symbol, even if struct_type
            // was already set (it might be the pointer type from earlier)
            bool do_symbol_lookup_using_pointee_type = false;
            if (is_arrow && did_member_access && current_value.type != NULL
                && LLVMGetTypeKind(current_value.type) == LLVMPointerTypeKind)
            {
                debug_info("Using existing struct type");
            }
            else if (is_arrow && base_node && base_node->type == AST_NODE_IDENTIFIER)
            {
                debug_info(
                    "Arrow access: is_arrow=%d, base_node->type=%d, struct_val=%p, struct_type=%d (%p)",
                    is_arrow,
                    base_node->type,
                    (void *)struct_value.value,
                    struct_value.type != NULL ? (int)LLVMGetTypeKind(struct_value.type) : -1,
                    (void *)struct_value.type
                );
                debug_info(
                    "done_member_access: %d current_type: %u",
                    did_member_access,
                    current_value.type != NULL ? (int)LLVMGetTypeKind(current_value.type) : -1
                );
                /* For chained arrow access, current_type was set to the pointee struct
                 * type after the previous member access — use it with priority. */
                char const * tag = find_symbol_tag_name(ctx, base_node->text);
                debug_info("Looking up struct type by tag '%s' for opaque pointer.", tag ? tag : "NULL");
                if (tag != NULL)
                {
                    struct_value.type = find_type_by_tag(ctx, tag);
                }

                /* For anonymous struct typedefs (tag not in tagged list), use pointee_type from symbol */
                if (struct_value.type == NULL)
                {
                    debug_info("Checking find_symbol for '%s', struct_type is NULL", base_node->text);
                    TypedValue sym;

                    if (find_symbol(ctx, base_node->text, &sym))
                    {
                        debug_info(
                            "Arrow access: found symbol '%s', sym_type kind=%d, sym_pointee=%p (%d)",
                            base_node->text,
                            LLVMGetTypeKind(sym.type),
                            (void *)sym.pointee_type,
                            sym.pointee_type != NULL ? (int)LLVMGetTypeKind(sym.pointee_type) : -1
                        );
                        if (sym.pointee_type != NULL && LLVMGetTypeKind(sym.pointee_type) == LLVMStructTypeKind)
                        {
                            debug_info("Found struct type from pointee!");
                            struct_value = sym;
                            do_symbol_lookup_using_pointee_type = true;
                        }
                    }
                    else
                    {
                        debug_info("find_symbol returned false for '%s'", base_node->text);
                    }
                }
            }
            debug_info("assign struct type");
            dump_typed_value("struct value", struct_value);
            dump_typed_value("current value", current_value);
            LLVMTypeRef struct_type
                = do_symbol_lookup_using_pointee_type ? struct_value.pointee_type : struct_value.type;

            if (struct_type == NULL)
            {
                debug_error("Could not find struct type for member access of '%s'.", member_name);
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
                "Looking up struct info for member access by type. Struct type: %p (%u), found info: %s",
                (void *)struct_value.type,
                struct_value.type != NULL ? (int)LLVMGetTypeKind(struct_value.type) : -1,
                struct_info ? "yes" : "no"
            );
            if (struct_info != NULL && struct_info->fields != NULL)
            {
                for (unsigned j = 0; j < struct_info->field_count; ++j)
                {
                    if (struct_info->fields[j].name && strcmp(struct_info->fields[j].name, member_name) == 0
                        && struct_info->fields[j].storage_index < num_elements)
                    {
                        struct_field_t const * field = &struct_info->fields[j];
                        member_index = j;
                        storage_index = field->storage_index;
                        current_value.type = field->type;
                        current_value.pointee_type = field->pointee_struct_type;
                        found = true;
                        break;
                    }
                }
            }
            else
            {
                ir_gen_error(&ctx->errors, "struct member '%s' not found", member_name);
                return NullTypedValue;
            }
            debug_info(
                "Member '%s' access - found: %s, member_index: %u, storage_index: %u, num_elements: %u, "
                "current_val pointee_type: %p (%u)",
                member_name,
                found ? "yes" : "no",
                member_index,
                storage_index,
                num_elements,
                current_value.pointee_type,
                current_value.pointee_type != NULL ? (int)LLVMGetTypeKind(current_value.pointee_type) : -1
            );
            if (found || num_elements > 0)
            {
                // Create GEP to access member
                LLVMValueRef indices[2];
                indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
                indices[1] = LLVMConstInt(ctx->ref_type.i32_type, storage_index, false);

                // Check if struct_val (which could be base_val or current_ptr) is a pointer
                // Now GEP into the ACTUAL address
                TypedValue loaded_struct_value = struct_value;
                if (is_arrow)
                {
                    loaded_struct_value = ensure_rvalue(ctx, "postfix_struct_rval", struct_value);
                    debug_info("loaded struct pointer and type: %d", LLVMGetTypeKind(loaded_struct_value.type));
                    if (current_value.pointee_type != NULL)
                    {
                        debug_info("have pointee type!!");
                        struct_value.value = current_value.value;
                        struct_value.type = current_value.pointee_type;
                        struct_type = struct_value.type;
                    }
                }
                else
                {
                    loaded_struct_value = struct_value;
                    debug_info(
                        "have pointer and loaded struct type: %p (%u) struct type %p (%u)",
                        loaded_struct_value.type,
                        LLVMGetTypeKind(loaded_struct_value.type),
                        struct_type,
                        struct_type != NULL ? (int)LLVMGetTypeKind(struct_type) : -1
                    );
                }

                debug_info("end of block and struct info: %p", (void *)struct_info);
                if (struct_info == NULL)
                {
                    debug_error("have no struct_info - bailing out!");
                    ir_gen_error(&ctx->errors, "Compiler error: have no struct_info - bailing out!");
                    return NullTypedValue;
                }
                base_value = (TypedValue){
                    .value = LLVMBuildInBoundsGEP2(
                        ctx->builder, struct_type, loaded_struct_value.value, indices, 2, "memberptr"
                    ),
                    .type = struct_info->fields[member_index].type,
                    .pointee_type = struct_info->fields[member_index].pointee_struct_type,
                    .is_lvalue = true,
                };

                dump_typed_value("end_of_block_pre_bitfield_extraction_base_value", base_value);
                base_value = handle_bitfield_extraction(base_value, struct_info, member_index);
                dump_typed_value("end_of_block_base_value", base_value);
                current_value = base_value;
                dump_typed_value("end_of_block_current_value", current_value);
                did_member_access = true;
            }
        }
        else if (suffix->type == AST_NODE_POSTFIX_OPERATOR)
        {
            // Handle postfix increment/decrement: i++ or i--
            if (current_value.is_lvalue && current_value.value)
            {
                // 1. Get the ORIGINAL value (this is what the expression returns)
                TypedValue original_val = ensure_rvalue(ctx, "postfix_op_rval", current_value);
                LLVMValueRef modified_val;
                LLVMTypeRef type = original_val.type;
                LLVMTypeKind kind = LLVMGetTypeKind(type);

                if (kind == LLVMPointerTypeKind)
                {
                    // POINTER ARITHMETIC: use GEP to move by 1 element
                    LLVMValueRef indices[] = {LLVMConstInt(ctx->ref_type.i32_type, 1, false)};

                    // Note: we GEP from the original value (the address)
                    modified_val = LLVMBuildInBoundsGEP2(
                        ctx->builder, current_value.pointee_type, original_val.value, indices, 1, "ptr_step"
                    );
                }
                else if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                {
                    LLVMValueRef one = LLVMConstReal(type, 1.0);
                    if (suffix->op.postfix.op == POSTFIX_OP_INC)
                        modified_val = LLVMBuildFAdd(ctx->builder, original_val.value, one, "postfix_inc");
                    else
                        modified_val = LLVMBuildFSub(ctx->builder, original_val.value, one, "postfix_dec");
                }
                else
                {
                    // INTEGER: match the bit width of the type (i8, i32, i64, etc.)
                    LLVMValueRef one = LLVMConstInt(type, 1, false);
                    if (suffix->op.postfix.op == POSTFIX_OP_INC)
                        modified_val = LLVMBuildAdd(ctx->builder, original_val.value, one, "postfix_inc");
                    else
                        modified_val = LLVMBuildSub(ctx->builder, original_val.value, one, "postfix_dec");
                }

                // 2. SIDE EFFECT: Store the modified value back into the original memory
                aligned_store(ctx, ctx->builder, modified_val, current_value.type, current_value.value);

                // 3. RESULT: Return the ORIGINAL value as an RVALUE
                base_value = original_val;
                base_value.is_lvalue = false;
            }
        }
        else
        {
            debug_warning("Unhandled postfix suffix type %u", suffix->type);
        }
    }

#if 0
    // If base_val is NULL but we have a pointer (from array subscript or member access),
    // load the value from the pointer
    if (base_value.value == NULL && have_ptr && current_value.value && current_value.type)
    {
        // If current_type is a pointer, we may have already subscripted the pointer
        // and current_ptr now points to the element. We need to load the element type.
        if (LLVMGetTypeKind(current_value.type) == LLVMPointerTypeKind)
        {
            LLVMTypeRef elem_type = LLVMGetElementType(current_value.type);
            // Handle opaque pointers (elem_type is NULL) and char* (elem_type is i8)
            if (elem_type == NULL || elem_type == ctx->ref_type.i8_type)
            {
                // For char* or opaque pointers from subscript, load as i8
                base_value.value = aligned_load(ctx, ctx->builder, ctx->ref_type.i8_type, current_value.value, "load_tmp");
                base_value.type = ctx->ref_type.i8_type;
            }
            else
            {
                // Valid element type - load the element
                base_value.value = aligned_load(ctx, ctx->builder, elem_type, current_value.value, "load_tmp");
                base_value.type = elem_type;
            }
        }
        else
        {
            base_value.value = aligned_load(ctx, ctx->builder, current_value.type, current_value.value, "load_tmp");
            base_value.type = current_value.type;
        }
    }
#endif
    dump_typed_value("XXX - process_postfix_expression", base_value);

    return base_value;
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

    LLVMTypeRef target_type = map_type_to_llvm_t(ctx, spec_qual, abstract_decl);
    TypedValue val_to_cast = process_expression(ctx, inner_expr_node);
    val_to_cast = ensure_rvalue(ctx, "cast_rval", val_to_cast);
    val_to_cast = cast_value_to_type(ctx, val_to_cast, target_type, false);

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
        lhs_res = process_expression(ctx, lhs_node);
        dump_typed_value("assignment_lhs", lhs_res);
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
            lhs_res.value
                = aligned_load(ctx, ctx->builder, typed_value_get_llvm_type(&lhs_res), lhs_res.value, "ptr_deref");
            if (!typed_value_switch_to_pointee(&lhs_res))
            {
                ir_gen_error(&ctx->errors, "Failed to switch LHS to pointee type in assignment.");
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
        print_ast_with_label(lhs_node, "LHS");
    }

    // TODO: perform const checking once type descriptor conversion is complete.
    /* if (lhs_res.type_info->qualifiers.is_const) {ERROR} */

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
        rhs_res = ensure_rvalue(ctx, "assign_compound_rhs_rval", rhs_res);

        if (rhs_res.value == NULL)
        {
            debug_error("Failed to process RHS expression in compound assignment.");
            return NullTypedValue;
        }

        // Determine if this is a floating point operation
        LLVMTypeKind lhs_kind = LLVMGetTypeKind(lhs_res.type);
        bool is_float = (lhs_kind == LLVMFloatTypeKind || lhs_kind == LLVMDoubleTypeKind);

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
        rhs_res = ensure_rvalue(ctx, "assign_rhs_rval", rhs_res);
        if (rhs_res.value == NULL)
        {
            debug_error("Failed to process RHS expression in assignment.");
            return rhs_res;
        }
    }

    // Generate the store instruction.
    // if (is_bitfield_assign && bitfield_storage_unit_type != NULL)
    if (lhs_res.bit_width > 0)
    {
        debug_info("assigning to bitfield");
        /* * 1. Target the storage unit.
         * bitfield_storage_unit_ptr is the GEP result (the address of the i32/i64).
         * bitfield_storage_unit_type is the type of that storage unit (e.g., i32).
         */
        // TypedValue lhs_res_rval = ensure_rvalue(ctx, "assign_bitfield_lhs_rval", lhs_res);
        LLVMValueRef container
            = aligned_load(ctx, ctx->builder, lhs_res.type, lhs_res.value, "assign_bitfield_lhs_rval");

        LLVMTypeRef storage_type = lhs_res.type;

        // 2. LOAD only the affected storage unit
        LLVMValueRef current_val = container;

        // 3. PREPARE MASKS
        // width_mask: (1 << width) - 1
        unsigned long long width_mask = (lhs_res.bit_width == 64) ? ~0ULL : (1ULL << lhs_res.bit_width) - 1;
        // clear_mask: ~ (width_mask << offset)
        unsigned long long clear_mask = ~(width_mask << lhs_res.bit_offset);

        LLVMValueRef llvm_clear_mask = LLVMConstInt(storage_type, clear_mask, false);
        LLVMValueRef llvm_width_mask = LLVMConstInt(storage_type, width_mask, false);
        LLVMValueRef llvm_offset = LLVMConstInt(ctx->ref_type.i32_type, lhs_res.bit_offset, false);

        // 4. MODIFY
        // Clear the old bits
        LLVMValueRef cleared = LLVMBuildAnd(ctx->builder, current_val, llvm_clear_mask, "bf_clear");

        // Mask the RHS value to ensure it doesn't overflow its width
        TypedValue casted_rhs = cast_value_to_type(ctx, rhs_res, storage_type, true);
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
        rhs_res = cast_value_to_type(ctx, rhs_res, lhs_res.type, true);
        aligned_store(ctx, ctx->builder, rhs_res.value, lhs_res.type, lhs_res.value);
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
            return (TypedValue){
                .value = LLVMConstInt(ctx->ref_type.i1_type, 1, false),
                .type = ctx->ref_type.i1_type,
            };
        }
        if (strcmp(node->text, "false") == 0)
        {
            return (TypedValue){
                .value = LLVMConstInt(ctx->ref_type.i1_type, 0, false),
                .type = ctx->ref_type.i1_type,
            };
        }
    }

    // Get the variable's pointer and its element type from the symbol table.
    TypedValue var_res = get_variable_pointer(ctx, node);

    if (var_res.value != NULL && var_res.type != NULL)
    {
        // Check if the symbol is an integer constant (like enum values)
        // These are global i32 values, not pointers - we can just return them directly
        // But only for globals that are marked as constants (e.g., enum values, const globals),
        // not for regular static/global variables (which can be modified)
        if (LLVMGetTypeKind(var_res.type) == LLVMIntegerTypeKind && LLVMIsAGlobalValue(var_res.value))
        {
            LLVMValueRef initializer = LLVMGetInitializer(var_res.value);
            // Only return initializer directly for actual constants (LLVMIsGlobalConstant)
            // For non-const globals like "static int x;", we need to load from the global
            if (initializer != NULL && LLVMIsGlobalConstant(var_res.value))
            {
                // Return the constant initializer directly
                return (TypedValue){
                    .value = initializer,
                    .type = var_res.type,
                };
            }
        }

        // Check if the type is an array (for file-scope or local arrays)
        if (LLVMGetTypeKind(var_res.type) == LLVMArrayTypeKind)
        {
            LLVMValueRef indices[2];
            indices[0] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
            indices[1] = LLVMConstInt(ctx->ref_type.i32_type, 0, false);
            return (TypedValue){
                .value = LLVMBuildInBoundsGEP2(ctx->builder, var_res.type, var_res.value, indices, 2, "array_ptr"),
                .type = var_res.type,
            };
        }

        return var_res;
    }
    else if (var_res.value == NULL)
    {
        debug_info("%s: no symbol found - check functions for %s", __func__, node->text);
        // Check if it's a function name - return the function pointer
        TypedValue func;
        struct function_decl_entry * decl_entry = find_function_declaration(ctx, node->text);
        if (decl_entry != NULL)
        {
            dump_typed_value("existing_func", decl_entry->func);
            func = decl_entry->func;
        }
        if (1 || func.type == NULL)
        {
            /* FIXME - For now, drop back to the old way if not found in the table. */
            debug_info("not found in table");
            LLVMValueRef func_val = LLVMGetNamedFunction(ctx->module, node->text);
            if (func_val == NULL)
            {
                debug_info("did not find symbol '%' in LLVMGetNamedFunction", node->text);
                return NullTypedValue;
            }
            func = (TypedValue){
                .value = func_val,
                .type = ctx->ref_type.ptr_type,
                .pointee_type = LLVMGlobalGetValueType(func_val),
            };
            dump_typed_value("func from LLVM", func);
        }

        return func;
    }

    debug_error("NULL element type for variable '%s'.", node->text);

    return NullTypedValue;
}

static TypedValue
process_bitwise_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Bitwise ops from chainl1: [LHS, RHS], operator is implied by node type
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    lhs_res = ensure_rvalue(ctx, "bitwise_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    rhs_res = ensure_rvalue(ctx, "bitwise_rhs_rval", rhs_res);
    if (lhs_res.value == NULL || rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    LLVMTypeRef lhs_type = lhs_res.type;
    LLVMTypeRef rhs_type = rhs_res.type;
    LLVMTypeKind lhs_type_kind = LLVMGetTypeKind(lhs_type);
    LLVMTypeKind rhs_type_kind = LLVMGetTypeKind(rhs_type);

    TypedValue res = lhs_res;

    // Handle type promotion for integer operands - both sides must match
    if (!(lhs_type_kind == LLVMFloatTypeKind || lhs_type_kind == LLVMDoubleTypeKind)
        && lhs_type_kind == LLVMIntegerTypeKind && rhs_type_kind == LLVMIntegerTypeKind)
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_res.value = LLVMBuildZExt(ctx->builder, rhs_res.value, lhs_res.type, "promote_rhs");
            rhs_res.type = lhs_res.type;
            res.type = lhs_res.type;
            /* Default to lhs value just so a value is always assigned.*/
            res.value = lhs_res.value;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_res.value = LLVMBuildZExt(ctx->builder, lhs_res.value, rhs_res.type, "promote_lhs");
            lhs_res.type = rhs_res.type;
            res.type = rhs_res.type;
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
    lhs_res = ensure_rvalue(ctx, "shift_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    rhs_res = ensure_rvalue(ctx, "shift_rhs_rval", rhs_res);
    if (lhs_res.value == NULL || rhs_res.value == NULL)
    {
        return NullTypedValue;
    }

    // Ensure shift amount has same integer width as lhs
    LLVMTypeRef lhs_type = lhs_res.type;
    LLVMTypeKind lhs_kind = LLVMGetTypeKind(lhs_type);
    LLVMTypeRef rhs_type = rhs_res.type;
    LLVMTypeKind rhs_kind = LLVMGetTypeKind(rhs_type);

    TypedValue res = lhs_res; /* Default to LHS value so something is always assigned. */

    if (lhs_kind == LLVMIntegerTypeKind && rhs_kind == LLVMIntegerTypeKind)
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_res.value = LLVMBuildZExt(ctx->builder, rhs_res.value, lhs_type, "promote_shift_rhs");
        }
        else if (rhs_bits > lhs_bits)
        {
            // Shift amount larger than lhs width: truncate to lhs width
            rhs_res.value = LLVMBuildTrunc(ctx->builder, rhs_res.value, lhs_type, "trunc_shift_rhs");
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
    lhs_res = ensure_rvalue(ctx, "arith_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    rhs_res = ensure_rvalue(ctx, "arith_rhs_rval", rhs_res);
    if (lhs_res.value == NULL || rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    LLVMTypeRef lhs_type = lhs_res.type;
    LLVMTypeRef rhs_type = rhs_res.type;
    LLVMTypeKind lhs_type_kind = LLVMGetTypeKind(lhs_type);
    LLVMTypeKind rhs_type_kind = LLVMGetTypeKind(rhs_type);

    bool is_float_op = (lhs_type_kind == LLVMFloatTypeKind || lhs_type_kind == LLVMDoubleTypeKind);

    // Handle type promotion for integer operands
    // If lhs is wider than rhs (e.g., long vs int), promote rhs to match
    TypedValue res = lhs_res;

    if (!is_float_op && lhs_type_kind == LLVMIntegerTypeKind && rhs_type_kind == LLVMIntegerTypeKind)
    {
        unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
        unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
        if (lhs_bits > rhs_bits)
        {
            rhs_res.value = LLVMBuildSExt(ctx->builder, rhs_res.value, lhs_res.type, "promote_rhs");
            res.type = lhs_res.type;
        }
        else if (rhs_bits > lhs_bits)
        {
            lhs_res.value = LLVMBuildSExt(ctx->builder, lhs_res.value, rhs_res.type, "promote_lhs");
            res.type = rhs_res.type;
        }
    }
    debug_info("result bit width: %u", LLVMGetIntTypeWidth(lhs_type));
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
    lhs_res = ensure_rvalue(ctx, "rel_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    rhs_res = ensure_rvalue(ctx, "rel_rhs_r_val", rhs_res);

    if (lhs_res.value == NULL || rhs_res.value == NULL)
    {
        return NullTypedValue;
    }
    LLVMTypeRef lhs_type = lhs_res.type;
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);

    rhs_res = cast_value_to_type(ctx, rhs_res, lhs_res.type, false);

    bool is_float_op = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

    c_grammar_node_t const * op_node = node->binary_expression.op;
    relational_operator_type_t operator= op_node->op.rel.op;

    TypedValue res = {.type = ctx->ref_type.i1_type};
    switch (operator)
    {
    case REL_OP_LT:
        res.value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs_res.value, rhs_res.value, "flt_tmp")
                                : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs_res.value, rhs_res.value, "lt_tmp");
        break;
    case REL_OP_GT:
        res.value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs_res.value, rhs_res.value, "fgt_tmp")
                                : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs_res.value, rhs_res.value, "gt_tmp");
        break;
    case REL_OP_LE:
        res.value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs_res.value, rhs_res.value, "fle_tmp")
                                : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs_res.value, rhs_res.value, "le_tmp");
        break;
    case REL_OP_GE:
        res.value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs_res.value, rhs_res.value, "fge_tmp")
                                : LLVMBuildICmp(ctx->builder, LLVMIntSGE, lhs_res.value, rhs_res.value, "ge_tmp");
        break;
    default:
        ir_gen_error(&ctx->errors, "Unknown relational operator");
        return NullTypedValue;
    }
#if 0
    // Convert the i1 result to i32
    res.value = LLVMBuildZExt(ctx->builder, res.value, ctx->ref_type.i32_type, "bool_to_int");
    res.type = ctx->ref_type.i32_type;
#endif

    return res;
}

static TypedValue
process_equality_expression(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // Standard binary ops: [LHS, OP, RHS]
    TypedValue lhs_res = process_expression(ctx, node->binary_expression.left);
    lhs_res = ensure_rvalue(ctx, "eq_lhs_rval", lhs_res);
    TypedValue rhs_res = process_expression(ctx, node->binary_expression.right);
    rhs_res = ensure_rvalue(ctx, "eq_rhs_rval", rhs_res);

    if (lhs_res.value == NULL || rhs_res.value == NULL)
    {
        return NullTypedValue;
    }

#if 1
    char * val_str = LLVMPrintValueToString(lhs_res.value);
    debug_info("LHS Value %p contents: %s", (void *)lhs_res.value, val_str);
    LLVMDisposeMessage(val_str); // CRITICAL: You must free this string!
    val_str = LLVMPrintValueToString(rhs_res.value);
    debug_info("RHS Value %p contents: %s", (void *)rhs_res.value, val_str);
    LLVMDisposeMessage(val_str); // CRITICAL: You must free this string!
#endif

    LLVMTypeRef lhs_type = lhs_res.type;
    LLVMTypeRef rhs_type = rhs_res.type;
    LLVMTypeKind lhs_type_kind = LLVMGetTypeKind(lhs_type);
    LLVMTypeKind rhs_type_kind = LLVMGetTypeKind(rhs_type);

    bool is_float_op = (lhs_type_kind == LLVMFloatTypeKind || lhs_type_kind == LLVMDoubleTypeKind);

    // Handle type promotion for integer operands - both sides must match
    if (!is_float_op && lhs_type_kind == LLVMIntegerTypeKind && rhs_type_kind == LLVMIntegerTypeKind)
    {
        rhs_res = cast_value_to_type(ctx, rhs_res, lhs_type, true);
    }

    c_grammar_node_t const * op_node = node->binary_expression.op;
    equality_operator_type_t operator= op_node->op.eq.op;
    debug_info("%s: now comparing results operator %d", __func__, operator);

    TypedValue res = {.type = ctx->ref_type.i1_type};
    switch (operator)
    {
    case EQ_OP_EQ:
        res.value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs_res.value, rhs_res.value, "feq_tmp")
                                : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs_res.value, rhs_res.value, "eq_tmp");
        break;
    case EQ_OP_NE:
        res.value = is_float_op ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs_res.value, rhs_res.value, "fne_tmp")
                                : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_res.value, rhs_res.value, "ne_tmp");
        break;
    default:
        ir_gen_error(&ctx->errors, "Unsupported equality operator");
        return NullTypedValue;
    }

    debug_info("%s result: %p", __func__, res.value);
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
    lhs_res = ensure_rvalue(ctx, "log_lhs_rval", lhs_res);
    if (lhs_res.value == NULL)
    {
        ir_gen_error(&ctx->errors, "LHS processing of logical expression failed");
        return NullTypedValue;
    }
    // Convert to i1
    if (LLVMGetTypeKind(lhs_res.type) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(lhs_res.type) != 1)
    {
        // This handles ints (is x != 0?), pointers (is ptr != null?), etc.
        LLVMValueRef zero = LLVMConstNull(lhs_res.type);
        lhs_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs_res.value, zero, "to_bool");
    }

    LLVMValueRef res_alloca = LLVMBuildAlloca(ctx->builder, ctx->ref_type.i1_type, "logical_res");

    aligned_store(ctx, ctx->builder, lhs_res.value, ctx->ref_type.i1_type, res_alloca);
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
    rhs_res = ensure_rvalue(ctx, "log_rhs_rval", rhs_res);

    if (rhs_res.value == NULL)
    {
        ir_gen_error(&ctx->errors, "RHS processing of logical expression failed");
        return NullTypedValue;
    }

    if (LLVMGetTypeKind(lhs_res.type) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(lhs_res.type) != 1)
    {
        // This handles ints (is x != 0?), pointers (is ptr != null?), etc.
        LLVMValueRef zero = LLVMConstNull(rhs_res.type);
        rhs_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs_res.value, zero, "to_bool");
    }

    aligned_store(ctx, ctx->builder, rhs_res.value, ctx->ref_type.i1_type, res_alloca);
    LLVMBuildBr(ctx->builder, merge_block);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_block);

#if 0
    /* Upcast to i32. */
    LLVMValueRef result_val = LLVMBuildZExt(ctx->builder, v, ctx->ref_type.i32_type, "logical_final_i32");
    return (TypedValue){.value = result_val, .type = ctx->ref_type.i32_type};
#else
    return (TypedValue){.value = res_alloca, .type = ctx->ref_type.i1_type, .is_lvalue = true};
#endif
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
    cond_res = ensure_rvalue(ctx, "shift_cond_rval", cond_res);
    if (cond_res.value == NULL)
    {
        return NullTypedValue;
    }

    // Convert condition to i1 if needed
    LLVMTypeRef cond_type = cond_res.type;
    if (LLVMGetTypeKind(cond_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(cond_type) != 1)
    {
        LLVMValueRef zero = LLVMConstNull(cond_type);
        cond_res.value = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_res.value, zero, "cond_bool");
    }

    // Branch to then or else
    LLVMBuildCondBr(ctx->builder, cond_res.value, then_block, else_block);

    // Generate then block
    LLVMPositionBuilderAtEnd(ctx->builder, then_block);
    TypedValue true_res = process_expression(ctx, true_expr_node);
    true_res = ensure_rvalue(ctx, "conditional_then", true_res);
    if (true_res.value == NULL)
    {
        return NullTypedValue;
    }
    // After processing true_expr (which might be a nested ternary), the builder
    // is positioned at the nested ternary's merge block. Save this block
    // before branching to our merge block.
    LLVMBasicBlockRef true_block = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_block);

    // Generate else block
    LLVMPositionBuilderAtEnd(ctx->builder, else_block);
    TypedValue false_res = process_expression(ctx, false_expr_node);
    false_res = ensure_rvalue(ctx, "conditional_else", false_res);
    if (false_res.value == NULL)
    {
        return false_res;
    }
    // After processing false_expr (which might be a nested ternary), the builder
    // is positioned at the nested ternary's merge block. Save this block
    // before branching to our merge block.
    LLVMBasicBlockRef false_block = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_block);

    // Merge and create phi node
    LLVMPositionBuilderAtEnd(ctx->builder, merge_block);

    TypedValue res = {
        .value = LLVMBuildPhi(ctx->builder, true_res.type, "cond_result"),
        .type = true_res.type,
    };

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
    TypeDescriptor const * compound_type_desc
        = is_typedef ? find_typedef_type_descriptor(ctx, type_name) : find_type_descriptor_by_tag(ctx, type_name);
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
        process_initializer_list(ctx, alloca_inst, compound_type_desc->llvm_type, init_list_node, NULL);
    }

    return create_typed_value(alloca_inst, compound_type_desc, true);
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
                ir_gen_error(&ctx->errors, "Cannot take the address of an rvalue");
                return NullTypedValue;
            }

            // --- The Bridge Logic ---
            TypeDescriptor const * base_desc = v.type_info;

            if (base_desc == NULL)
            {
                ir_gen_error(&ctx->errors, "No type information found for compound literal");
                debug_error("No type descriptor found for compound literal, attempting fallback");
                return NullTypedValue;
            }
            v = create_typed_value(
                v.value, get_or_create_pointer_type(ctx->type_descriptors, base_desc, (TypeQualifier){0}), false
            );

            return v;
        }
        TypedValue v = process_expression(ctx, operand_node);

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
            return operand_res;
        }

        if (operand_res.pointee_type == NULL)
        {
            ir_gen_error(
                &ctx->errors, "Error: Dereference operand is not a pointer (value: %p)\n", (void *)operand_res.value
            );
            return NullTypedValue;
        }
        operand_res = ensure_rvalue(ctx, "un_op_deref", operand_res);
        if (!typed_value_switch_to_pointee(&operand_res))
        {
            ir_gen_error(&ctx->errors, "Error: Failed to switch to pointee type for dereference");
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
            ir_gen_error(&ctx->errors, "Operand processing failed for unary minus");
            return NullTypedValue;
        }
        if (operand_res.type != NULL
            && (LLVMGetTypeKind(operand_res.type) == LLVMFloatTypeKind
                || LLVMGetTypeKind(operand_res.type) == LLVMDoubleTypeKind))
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
        operand_res = ensure_rvalue(ctx, "un_not_rval", operand_res);

        // 1. Comparison produces an i1 (1-bit integer)
        LLVMValueRef zero = LLVMConstNull(operand_res.type);
        LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand_res.value, zero, "is_zero_tmp");
        TypeDescriptor const * bool_desc = type_descriptor_get_bool_type(ctx->type_descriptors, false);

        return create_typed_value(is_zero, bool_desc, false);
    }

    case UNARY_OP_BITNOT:
    {
        TypedValue operand_res = process_expression(ctx, operand_node);
        operand_res = ensure_rvalue(ctx, "bit_not_rval", operand_res);

        operand_res.value = LLVMBuildNot(ctx->builder, operand_res.value, "bitnot_tmp");
        operand_res.is_lvalue = false;
        return operand_res;
    }

    case UNARY_OP_INC:
    case UNARY_OP_DEC:
    {
        TypedValue var_res = process_expression(ctx, operand_node);

        if (var_res.value == NULL || var_res.type == NULL)
        {
            return NullTypedValue;
        }

        TypedValue rvalue_res = ensure_rvalue(ctx, "unary_inc_dev_rval", var_res);
        LLVMValueRef original_val = rvalue_res.value;
        LLVMValueRef one = LLVMConstInt(ctx->ref_type.i32_type, 1, false);

        LLVMValueRef new_val;
        if (op->op.unary.op == UNARY_OP_INC)
        {
            LLVMTypeKind kind = LLVMGetTypeKind(var_res.type);
            if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                new_val = LLVMBuildFAdd(ctx->builder, original_val, LLVMConstReal(var_res.type, 1.0), "inc_tmp");
            else
                new_val = LLVMBuildAdd(ctx->builder, original_val, one, "inc_tmp");
        }
        else
        {
            LLVMTypeKind kind = LLVMGetTypeKind(var_res.type);
            if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
                new_val = LLVMBuildFSub(ctx->builder, original_val, LLVMConstReal(var_res.type, 1.0), "dec_tmp");
            else
                new_val = LLVMBuildSub(ctx->builder, original_val, one, "dec_tmp");
        }
        aligned_store(ctx, ctx->builder, new_val, var_res.type, var_res.value);
        var_res.value = new_val;
        var_res.is_lvalue = false;

        return var_res;
    }

    case UNARY_OP_PLUS:
    {
        TypedValue var_res = process_expression(ctx, operand_node);
        var_res.is_lvalue = false;

        return var_res;
    }

    case UNARY_OP_SIZEOF:
    {
        LLVMTypeRef target_type = NULL;

        // Check if operand is a TypeName (e.g., sizeof(int) or sizeof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            // TypeName contains TypeSpecifier(s), possibly with struct/union keyword
            c_grammar_node_t const * qualifier_list = operand_node->type_name.specifier_qualifier_list;

            for (size_t i = 0; i < qualifier_list->list.count && target_type == NULL; i++)
            {
                c_grammar_node_t * child = qualifier_list->list.children[i];

                debug_info("qualifier list child type: %s", get_node_type_name_from_node(child));

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
                        target_type = map_type_to_llvm_t(ctx, child, NULL);
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
                // Handle TypedefSpecifier (typedef name in sizeof(MyType))
                else if (child->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
                {
                    c_grammar_node_t const * specifier = child->typedef_specifier_qualifier.typedef_specifier;
                    char const * typedef_name = extract_typedef_name(specifier);
                    if (typedef_name != NULL)
                    {
                        TypeDescriptor const * typedef_type_desc = find_typedef_type_descriptor(ctx, typedef_name);
                        if (typedef_type_desc != NULL && typedef_type_desc->llvm_type != NULL)
                        {
                            target_type = typedef_type_desc->llvm_type;
                        }
                    }
                }
            }
        }
        else
        {
            debug_info("Operand node type for sizeof: %s", get_node_type_name_from_node(operand_node));
            TypedValue operand_res = process_expression(ctx, operand_node);

            target_type = typed_value_get_llvm_type(&operand_res);
            debug_info(
                "sizeof operand expression type: %d", target_type != NULL ? (int)LLVMGetTypeKind(target_type) : -1
            );
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
        LLVMTypeRef target_type = NULL;

        // Handle TypeName (e.g., alignof(int) or alignof(struct Point))
        if (operand_node->type == AST_NODE_TYPE_NAME)
        {
            c_grammar_node_t const * qualifier_list = operand_node->type_name.specifier_qualifier_list;

            for (size_t i = 0; i < qualifier_list->list.count && target_type == NULL; i++)
            {
                c_grammar_node_t * child = qualifier_list->list.children[i];

                if (child->type == AST_NODE_TYPE_SPECIFIER)
                {
                    if (child->text != NULL)
                    {
                        char const * type_name = child->text;

                        target_type = get_type_from_name(ctx, type_name);
                    }
                    else
                    {
                        target_type = map_type_to_llvm_t(ctx, child, NULL);
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
        else
        {
            debug_info("Operand node type for alignof: %s", get_node_type_name_from_node(operand_node));
            TypedValue operand_res = process_expression(ctx, operand_node);

            target_type = typed_value_get_llvm_type(&operand_res);
            debug_info(
                "alignment operand expression type: %d", target_type != NULL ? (int)LLVMGetTypeKind(target_type) : -1
            );
        }
        debug_info("unary operator getting alignment of type: %p", target_type);
        uint64_t alignment = get_type_alignment(ctx, target_type);
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
            return result;
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
    case AST_NODE_STORAGE_CLASS_SPECIFIERS:
    case AST_NODE_FUNCTION_SPECIFIER:
    case AST_NODE_TYPE_QUALIFIER:
    case AST_NODE_TYPE_QUALIFERS:
    case AST_NODE_DECLARATION_SPECIFIERS:
    case AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER:
    case AST_NODE_TYPE_SPECIFIERS:
    case AST_NODE_PARAMETER_LIST:
    case AST_NODE_ELLIPSIS:
    default:
        // Attempt to recursively process if it might yield a value.
        if (node->list.count > 0)
        {
            debug_info("Default processing for list node: %s %u", get_node_type_name_from_type(node->type), node->type);
            for (size_t i = 0; i < node->list.count; ++i)
            {
                TypedValue res = process_expression(ctx, node->list.children[i]);
                if (res.value != NULL)
                {
                    return res; // Return the first valid result found.
                }
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
    return NullTypedValue; // Return NULL if expression processing failed or not implemented.
}

static bool
check_expression_result_has_type(TypedValue val)
{
    /* If the TypedValue has a value, then it must also have a type. */
    return val.value == NULL || val.type != NULL;
}

static TypedValue
_process_expression_impl(ir_generator_ctx_t * ctx, c_grammar_node_t const * node, int line)
{
    if (ctx->errors.fatal)
    {
        return NullTypedValue;
    }
    if (!visit_stack_push(node))
    {
        return NullTypedValue; /* cycle detected */
    }
    debug_info("%s from line: %u", __func__, line);
    print_ast_with_label(node, __func__);

    TypedValue result = _process_expression(ctx, node);
    debug_info("%s processed and result val: %p type: %p from line %u", __func__, result.value, result.type, line);

    visit_stack_pop(node);

    if (!check_expression_result_has_type(result))
    {
        debug_error(
            "expression result has a value but no type after evalualting node: %s", get_node_type_name_from_node(node)
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
    print_ast_with_label(node, "done with");

    return result;
}
