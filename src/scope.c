/**
 * @file scope.c
 * @brief Type and symbol table management implementation for NCC compiler.
 */

#include "scope.h"

#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the full definition from the header to get ir_generator_ctx_t
#include "llvm_ir_generator.h"

// --- Scope lifecycle ---

scope_t *
scope_create(scope_t * parent)
{
    scope_t * scope = calloc(1, sizeof(*scope));
    if (scope == NULL)
    {
        return NULL;
    }

    scope->symbol_capacity = 16;
    scope->symbols = calloc(scope->symbol_capacity, sizeof(*scope->symbols));
    if (!scope->symbols)
    {
        free(scope);
        return NULL;
    }

    scope->tagged_types.capacity = 4;
    scope->tagged_types.entries = calloc(scope->tagged_types.capacity, sizeof(*scope->tagged_types.entries));
    if (!scope->tagged_types.entries)
    {
        free(scope->symbols);
        free(scope);
        return NULL;
    }

    scope->untagged_types.capacity = 4;
    scope->untagged_types.entries = calloc(scope->untagged_types.capacity, sizeof(*scope->untagged_types.entries));
    if (!scope->untagged_types.entries)
    {
        free(scope->tagged_types.entries);
        free(scope->symbols);
        free(scope);
        return NULL;
    }

    scope->typedefs.capacity = 4;
    scope->typedefs.entries = calloc(scope->typedefs.capacity, sizeof(*scope->typedefs.entries));
    if (!scope->typedefs.entries)
    {
        free(scope->untagged_types.entries);
        free(scope->tagged_types.entries);
        free(scope->symbols);
        free(scope);
        return NULL;
    }

    scope->parent = parent;
    return scope;
}

void
scope_free(scope_t * scope)
{
    if (scope == NULL)
    {
        return;
    }

    /* Free all symbol names and struct names in this scope */
    for (size_t i = 0; i < scope->symbol_count; ++i)
    {
        free(scope->symbols[i].name);
        free(scope->symbols[i].tag_name);
    }
    free(scope->symbols);

    /* Free all local types (structs/unions) in this scope */
    for (size_t i = 0; i < scope->tagged_types.count; ++i)
    {
        free(scope->tagged_types.entries[i].tag);
        for (size_t j = 0; j < scope->tagged_types.entries[i].field_count; ++j)
        {
            free(scope->tagged_types.entries[i].fields[j].name);
        }
        free(scope->tagged_types.entries[i].fields);
    }
    free(scope->tagged_types.entries);

    /* Free all untagged structs in this scope */
    free(scope->untagged_types.entries);

    /* Free all typedefs in this scope */
    for (size_t i = 0; i < scope->typedefs.count; ++i)
    {
        free(scope->typedefs.entries[i].name);
        free(scope->typedefs.entries[i].tag);
    }
    free(scope->typedefs.entries);

    free(scope);
}

// --- Type management ---

void
free_type_info(type_info_t * info)
{
    if (info == NULL)
    {
        return;
    }

    free(info->tag);
    if (info->fields)
    {
        for (size_t i = 0; i < info->field_count; ++i)
        {
            free(info->fields[i].name);
        }
        free(info->fields);
    }
}

type_info_t const *
add_info_to_list(scope_types_t * list, type_info_t info)
{
    if (info.tag != NULL && info.tag[0] == '\0')
    {
        /* Check if tag already exists */
        for (size_t i = 0; i < list->count; ++i)
        {
            if (list->entries[i].tag && strcmp(list->entries[i].tag, info.tag) == 0)
            {
                /* Same tag exists - check if kind matches */
                if (list->entries[i].kind != info.kind)
                {
                    /* Kind mismatch - silently fail (keep original) */
                    return NULL;
                }
                /* Same kind - update the entry */
                free_type_info(&list->entries[i]);
                list->entries[i] = info;
                return &list->entries[i];
            }
        }
    }

    /* New entry - add to array */
    if (list->count >= list->capacity)
    {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        type_info_t * new_entries = realloc(list->entries, new_cap * sizeof(*new_entries));
        if (new_entries == NULL)
        {
            return NULL;
        }
        list->entries = new_entries;
        list->capacity = new_cap;
    }

    list->entries[list->count++] = info;
    debug_info(
        "Added type info: tag='%s', kind=%d, total count=%zu", info.tag ? info.tag : "(null)", info.kind, list->count
    );
    return &list->entries[list->count - 1];
}

type_info_t const *
scope_add_tagged_type(scope_t * scope, type_info_t info)
{
    if (scope == NULL || info.tag == NULL)
    {
        return NULL;
    }
    debug_info("Adding tagged type: scope=%p, tag='%s', kind=%d", (void *)scope, info.tag, info.kind);
    scope_types_t * tagged = &scope->tagged_types;

    return add_info_to_list(tagged, info);
}

type_info_t const *
scope_add_untagged_type(scope_t * scope, type_info_t info)
{
    if (scope == NULL)
    {
        return NULL;
    }

    scope_types_t * untagged = &scope->untagged_types;
    return add_info_to_list(untagged, info);
}

// --- Tagged type lookup ---

type_info_t *
scope_lookup_tagged_entry_by_tag(scope_t const * scope, char const * tag)
{
    if (scope == NULL || tag == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < scope->tagged_types.count; ++i)
    {
        if (scope->tagged_types.entries[i].tag && strcmp(scope->tagged_types.entries[i].tag, tag) == 0)
        {
            return &scope->tagged_types.entries[i];
        }
    }

    return scope_lookup_tagged_entry_by_tag(scope->parent, tag);
}

type_info_t *
scope_find_tagged_type(scope_t const * scope, char const * tag, type_kind_t kind)
{
    type_info_t * info = scope_lookup_tagged_entry_by_tag(scope, tag);
    if (info == NULL)
    {
        return NULL;
    }

    /* Found by tag - verify kind matches */
    if (info->kind != kind)
    {
        return NULL;
    }

    return info;
}

type_info_t *
scope_find_tagged_struct(scope_t const * scope, char const * tag)
{
    return scope_find_tagged_type(scope, tag, TYPE_KIND_STRUCT);
}

type_info_t *
scope_find_tagged_union(scope_t const * scope, char const * tag)
{
    return scope_find_tagged_type(scope, tag, TYPE_KIND_UNION);
}

type_info_t *
scope_find_tagged_enum(scope_t const * scope, char const * tag)
{
    return scope_find_tagged_type(scope, tag, TYPE_KIND_ENUM);
}

// --- Untagged type lookup ---

type_info_t *
scope_lookup_untagged_entry_by_index(scope_t const * scope, int index)
{
    if (scope == NULL || index < 0)
    {
        return NULL;
    }
    debug_info("Looking up untagged type by index: %d", index);
    if ((size_t)index < scope->untagged_types.count)
    {
        return &scope->untagged_types.entries[index];
    }

    return scope_lookup_untagged_entry_by_index(scope->parent, index);
}

type_info_t const *
scope_find_untagged_type(scope_t const * scope, type_kind_t kind, int index)
{
    debug_info("Finding untagged type: index=%d, kind=%d", index, kind);
    type_info_t * entry = scope_lookup_untagged_entry_by_index(scope, index);
    if (entry == NULL)
    {
        debug_info("Untagged type not found for index: %d", index);
        return NULL;
    }

    if (entry->kind != kind)
    {
        return NULL;
    }

    return entry;
}

type_info_t const *
scope_find_untagged_struct(scope_t const * scope, int index)
{
    return scope_find_untagged_type(scope, TYPE_KIND_UNTAGGED_STRUCT, index);
}

type_info_t const *
scope_find_untagged_union(scope_t const * scope, int index)
{
    return scope_find_untagged_type(scope, TYPE_KIND_UNTAGGED_UNION, index);
}

type_info_t const *
scope_find_untagged_enum(scope_t const * scope, int index)
{
    return scope_find_untagged_type(scope, TYPE_KIND_UNTAGGED_ENUM, index);
}

// --- LLVM type lookup ---

type_info_t *
scope_find_type_by_llvm_type(scope_t const * scope, LLVMTypeRef type)
{
    if (scope == NULL || type == NULL)
    {
        debug_info("scope_find_type_by_llvm_type: Invalid scope or type. Scope: %p, Type: %p", (void*)scope, (void*)type);
        return NULL;
    }

    debug_info("scope_find_type_by_llvm_type: Searching for LLVMTypeRef %p", (void*)type);

    /* Search in tagged_types */
    for (size_t i = 0; i < scope->tagged_types.count; ++i)
    {
        debug_info("scope_find_type_by_llvm_type: Checking tagged_type[%zu].type = %p", i, (void*)scope->tagged_types.entries[i].type);
        if (scope->tagged_types.entries[i].type == type)
        {
            debug_info("scope_find_type_by_llvm_type: Found match in tagged_types.");
            return &scope->tagged_types.entries[i];
        }
    }

    /* Search in untagged_types */
    for (size_t i = 0; i < scope->untagged_types.count; ++i)
    {
        debug_info("scope_find_type_by_llvm_type: Checking untagged_type[%zu].type = %p", i, (void*)scope->untagged_types.entries[i].type);
        if (scope->untagged_types.entries[i].type == type)
        {
            debug_info("scope_find_type_by_llvm_type: Found match in untagged_types.");
            return &scope->untagged_types.entries[i];
        }
    }

    if (scope->parent != NULL)
    {
        debug_info("scope_find_type_by_llvm_type: Searching in parent scope.");
        return scope_find_type_by_llvm_type(scope->parent, type);
    }

    debug_info("scope_find_type_by_llvm_type: Type %p not found in any scope.", (void*)type);
    return NULL;
}

// --- Typedef management ---

void
scope_add_typedef_entry(scope_t * scope, scope_typedef_entry_t entry)
{
    debug_info("Adding typedef entry: name='%s', tag='%s', kind=%d", entry.name, entry.tag, entry.kind);
    if (scope == NULL)
    {
        return;
    }

    if (scope->typedefs.count >= scope->typedefs.capacity)
    {
        size_t new_cap = scope->typedefs.capacity == 0 ? 4 : scope->typedefs.capacity * 2;
        scope_typedef_entry_t * new_entries = realloc(scope->typedefs.entries, new_cap * sizeof(*new_entries));
        if (new_entries == NULL)
        {
            return;
        }
        scope->typedefs.entries = new_entries;
        scope->typedefs.capacity = new_cap;
    }

    scope->typedefs.entries[scope->typedefs.count++] = entry;
}

void
scope_add_typedef_forward_decl(scope_t * scope, char const * typedef_name, char const * tag, type_kind_t kind)
{
    scope_typedef_entry_t entry = {0};
    entry.name = strdup(typedef_name);
    entry.tag = strdup(tag);
    entry.kind = kind;
    scope_add_typedef_entry(scope, entry);
}

scope_typedef_entry_t *
scope_lookup_typedef_entry_by_name(scope_t const * scope, char const * name)
{
    if (scope == NULL || name == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < scope->typedefs.count; ++i)
    {
        if (scope->typedefs.entries[i].name && strcmp(scope->typedefs.entries[i].name, name) == 0)
        {
            return &scope->typedefs.entries[i];
        }
    }

    return scope_lookup_typedef_entry_by_name(scope->parent, name);
}

LLVMTypeRef
scope_find_typedef(scope_t const * scope, char const * name)
{
    debug_info("Finding typedef by name: '%s'", name);
    scope_typedef_entry_t const * entry = scope_lookup_typedef_entry_by_name(scope, name);
    if (entry == NULL)
    {
        debug_info("Typedef not found for name: '%s'", name);
        return NULL;
    }
    debug_info(
        "Found typedef entry: name='%s', tag='%s', kind=%d index=%d",
        entry->name,
        entry->tag,
        entry->kind,
        entry->untagged_index
    );
    switch (entry->kind)
    {
    case TYPE_KIND_STRUCT:
    {
        /* Look up the struct by tag name */
        type_info_t * info = scope_find_tagged_struct(scope, entry->tag);
        if (info != NULL)
        {
            return info->type;
        }
        return NULL;
    }
    case TYPE_KIND_UNTAGGED_STRUCT:
    {
        type_info_t const * info = scope_find_untagged_struct(scope, entry->untagged_index);
        /* Look up by untagged index */
        if (info != NULL)
        {
            return info->type;
        }
        return NULL;
    }
    case TYPE_KIND_UNION:
    {
        /* Look up the union by tag name */
        type_info_t * info = scope_find_tagged_union(scope, entry->tag);
        if (info != NULL)
        {
            return info->type;
        }
        return NULL;
    }
    case TYPE_KIND_UNTAGGED_UNION:
    {
        type_info_t const * info = scope_find_untagged_union(scope, entry->untagged_index);
        /* Look up by untagged index */
        if (info != NULL)
        {
            return info->type;
        }
        return NULL;
    }
    case TYPE_KIND_ENUM:
    {
        /* Look up the enum by tag name */
        type_info_t * info = scope_find_tagged_enum(scope, entry->tag);
        if (info != NULL)
        {
            return info->type;
        }
        return NULL;
    }
    case TYPE_KIND_UNTAGGED_ENUM:
    {
        /* For untagged enums, return the type directly if set on entry */
        if (entry->type != NULL)
        {
            return entry->type;
        }
        /* Otherwise, look up the untagged type by index */
        type_info_t const * info = scope_find_untagged_enum(scope, entry->untagged_index);
        if (info != NULL)
        {
            return info->type;
        }
        return NULL;
    }
    case TYPE_KIND_UNKNOWN:
    default:
        /* For other kinds, return the type directly */
        return entry->type;
    }

    return NULL; /* Unreachable. */
}

// --- Symbol management ---

void
add_symbol_with_struct(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef ptr,
    LLVMTypeRef type,
    LLVMTypeRef pointee_type,
    char const * tag,
    symbol_data_t const * data
)
{
    if (!ctx || !name || !ptr || !type || !ctx->current_scope)
        return;

    scope_t * scope = ctx->current_scope;

    if (scope->symbol_count >= scope->symbol_capacity)
    {
        size_t new_cap = scope->symbol_capacity == 0 ? 16 : scope->symbol_capacity * 2;
        symbol_t * new_symbols = realloc(scope->symbols, new_cap * sizeof(*new_symbols));
        if (!new_symbols)
            return;
        scope->symbols = new_symbols;
        scope->symbol_capacity = new_cap;
    }
    symbol_t * new_symbol = &scope->symbols[scope->symbol_count];

    new_symbol->name = strdup(name);
    new_symbol->ptr = ptr;
    new_symbol->type = type;
    new_symbol->pointee_type = pointee_type;
    new_symbol->tag_name = tag ? strdup(tag) : NULL;
    if (data != NULL)
    {
        new_symbol->data = *data;
    }
    scope->symbol_count++;
    debug_info(
        "Added symbol: name='%s', ptr=%p, type=%p, pointee_type=%p, tag='%s'",
        name,
        (void *)ptr,
        (void *)type,
        (void *)pointee_type,
        tag ? tag : "(null)"
    );
}

void
add_symbol(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef ptr,
    LLVMTypeRef type,
    LLVMTypeRef pointee_type,
    symbol_data_t const * data
)
{
    add_symbol_with_struct(ctx, name, ptr, type, pointee_type, NULL, data);
}

static char const *
scope_find_symbol_tag_name(scope_t const * scope, char const * name)
{
    if (scope == NULL || name == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < scope->symbol_count; ++i)
    {
        if (scope->symbols[i].name != NULL && strcmp(scope->symbols[i].name, name) == 0)
        {
            return scope->symbols[i].tag_name;
        }
    }

    return scope_find_symbol_tag_name(scope->parent, name);
}

char const *
find_symbol_tag_name(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_find_symbol_tag_name(ctx->current_scope, name);
}

static symbol_t *
scope_find_symbol_entry(scope_t const * scope, char const * name)
{
    debug_info("Finding symbol entry: name='%s' in scope=%p", name, (void *)scope);
    if (scope == NULL || name == NULL)
    {
        debug_info("Invalid scope or name for finding symbol entry: name='%s', scope=%p", name, (void *)scope);
        return NULL;
    }

    for (size_t i = scope->symbol_count; i > 0; --i)
    {
        if (scope->symbols[i - 1].name != NULL && strcmp(scope->symbols[i - 1].name, name) == 0)
        {
            debug_info(
                "Found symbol entry in current scope: name='%s', ptr=%p, type=%p",
                name,
                (void *)scope->symbols[i - 1].ptr,
                (void *)scope->symbols[i - 1].type
            );
            return &scope->symbols[i - 1];
        }
    }

    return scope_find_symbol_entry(scope->parent, name);
}

symbol_t const *
find_symbol_entry(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL || name == NULL)
    {
        return NULL;
    }

    return scope_find_symbol_entry(ctx->current_scope, name);
}

static bool
scope_find_symbol(
    scope_t const * scope,
    char const * name,
    LLVMValueRef * out_ptr,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
)
{
    symbol_t * symbol = scope_find_symbol_entry(scope, name);
    if (symbol == NULL)
    {
        debug_info("Symbol not found for name: '%s'", name);
        return false;
    }

    if (out_ptr != NULL)
    {
        *out_ptr = symbol->ptr;
    }
    if (out_type != NULL)
    {
        *out_type = symbol->type;
    }
    if (out_pointee_type != NULL)
    {
        *out_pointee_type = symbol->pointee_type;
    }

    return true;
}

bool
find_symbol(
    ir_generator_ctx_t * ctx,
    char const * name,
    LLVMValueRef * out_ptr,
    LLVMTypeRef * out_type,
    LLVMTypeRef * out_pointee_type
)
{
    if (ctx == NULL)
    {
        return false;
    }
    return scope_find_symbol(ctx->current_scope, name, out_ptr, out_type, out_pointee_type);
}

// --- Scope push/pop ---

void
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

void
scope_pop(ir_generator_ctx_t * ctx)
{
    if (ctx == NULL || ctx->current_scope == NULL)
    {
        return;
    }

    scope_t * old_scope = ctx->current_scope;
    ctx->current_scope = old_scope->parent;
    old_scope->parent = NULL; // Detach old scope from context before freeing
    scope_free(old_scope);
}

// --- Function declaration tracking ---

bool
function_signatures_match(LLVMTypeRef type1, LLVMTypeRef type2)
{
    if (type1 == NULL || type2 == NULL)
    {
        return false;
    }

    // Check return type
    LLVMTypeRef return1 = LLVMGetReturnType(type1);
    LLVMTypeRef return2 = LLVMGetReturnType(type2);
    if (LLVMGetTypeKind(return1) != LLVMGetTypeKind(return2))
    {
        return false;
    }

    // Check parameter count
    unsigned param_count1 = LLVMCountParamTypes(type1);
    unsigned param_count2 = LLVMCountParamTypes(type2);
    if (param_count1 != param_count2)
    {
        return false;
    }

    // Check parameter types
    if (param_count1 > 0)
    {
        LLVMTypeRef * params1 = malloc(param_count1 * sizeof(LLVMTypeRef));
        LLVMTypeRef * params2 = malloc(param_count2 * sizeof(LLVMTypeRef));
        if (params1 != NULL && params2 != NULL)
        {
            LLVMGetParamTypes(type1, params1);
            LLVMGetParamTypes(type2, params2);
            for (unsigned i = 0; i < param_count1; ++i)
            {
                if (LLVMGetTypeKind(params1[i]) != LLVMGetTypeKind(params2[i]))
                {
                    free(params1);
                    free(params2);
                    return false;
                }
            }
        }
        free(params1);
        free(params2);
    }

    return true;
}

struct function_decl_entry *
find_function_declaration(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL || name == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < ctx->function_declarations.count; ++i)
    {
        if (ctx->function_declarations.entries[i].name != NULL
            && strcmp(ctx->function_declarations.entries[i].name, name) == 0)
        {
            return &ctx->function_declarations.entries[i];
        }
    }

    return NULL;
}

bool
add_function_declaration(ir_generator_ctx_t * ctx, char const * name, LLVMTypeRef type, bool has_definition)
{
    if (ctx == NULL || name == NULL || type == NULL)
    {
        return false;
    }

    // Check if function already exists
    struct function_decl_entry * existing = find_function_declaration(ctx, name);

    if (existing != NULL)
    {
        // Function already declared - check for signature mismatch
        if (!function_signatures_match(existing->type, type))
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
    ctx->function_declarations.entries[ctx->function_declarations.count].type = type;
    ctx->function_declarations.entries[ctx->function_declarations.count].has_definition = has_definition;
    ctx->function_declarations.count++;

    return false;
}
