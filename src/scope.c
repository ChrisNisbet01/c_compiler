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
#include "llvm_typed_value.h"

// --- Scope lifecycle ---

scope_t *
scope_create(scope_t * parent, LLVMContextRef context, LLVMBuilderRef builder)
{
    scope_t * scope = calloc(1, sizeof(*scope));

    if (scope == NULL)
    {
        return NULL;
    }

    scope->context = context;
    scope->builder = builder;

    if (!scope_symbols_init(&scope->symbols))
    {
        scope_free(scope);

        return NULL;
    }

    scope->labels = labels_list_create(scope->context, scope->builder);
    if (scope->labels == NULL)
    {
        scope_free(scope);

        return NULL;
    }

    if (!scope_types_init(&scope->tagged_types))
    {
        scope_free(scope);

        return NULL;
    }

    if (!scope_types_init(&scope->untagged_types))
    {
        scope_free(scope);

        return NULL;
    }

    if (!scope_typedefs_init(&scope->typedefs))
    {
        scope_free(scope);

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

    scope_symbols_free(&scope->symbols);
    scope_types_free(&scope->tagged_types);
    scope_types_free(&scope->untagged_types);
    scope_typedefs_free(&scope->typedefs);
    labels_list_destroy(scope->labels);

    free(scope);
}

// --- Type management ---

type_info_t const *
scope_add_tagged_type(scope_t * scope, type_info_t info)
{
    if (scope == NULL || info.tag == NULL)
    {
        return NULL;
    }
    debug_info("Adding tagged type: scope=%p, tag='%s', kind=%d", (void *)scope, info.tag, info.kind);
    scope_types_t * tagged = &scope->tagged_types;

    return scope_types_add_entry(tagged, info);
}

type_info_t const *
scope_add_untagged_type(scope_t * scope, type_info_t info, int * untagged_index)
{
    if (scope == NULL)
    {
        return NULL;
    }

    scope_types_t * untagged = &scope->untagged_types;
    type_info_t const * result = scope_types_add_entry(untagged, info);
    if (result != NULL && untagged_index != NULL)
    {
        *untagged_index = untagged->count - 1;
    }
    return result;
}

// --- Tagged type lookup ---

static type_info_t *
scope_lookup_tagged_entry_by_tag_and_kind(scope_t const * scope, char const * tag, type_kind_t kind)
{
    while (scope != NULL && tag != NULL)
    {
        type_info_t * entry = scope_types_lookup_entry_by_tag_and_kind(&scope->tagged_types, tag, kind);

        if (entry != NULL)
        {
            return entry;
        }
        scope = scope->parent;
    }
    return NULL;
}

type_info_t *
scope_find_tagged_type(scope_t const * scope, char const * tag, type_kind_t kind)
{
    type_info_t * info = scope_lookup_tagged_entry_by_tag_and_kind(scope, tag, kind);
    if (info == NULL)
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
    while (scope != NULL && index >= 0)
    {
        debug_info("Looking up untagged type by index: %d", index);
        if ((size_t)index < scope->untagged_types.count)
        {
            return &scope->untagged_types.entries[index];
        }
        scope = scope->parent;
    }
    return NULL;
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
scope_find_type_by_type_descriptor(scope_t const * scope, TypeDescriptor const * const type_desc)
{
    if (type_desc == NULL)
    {
        return NULL;
    }
    while (scope != NULL)
    {
        type_info_t * entry;

        entry = scope_types_lookup_entry_by_type_descriptor(&scope->tagged_types, type_desc);
        if (entry != NULL)
        {
            debug_info("%s: Found tagged type.", __func__);
            return entry;
        }
        entry = scope_types_lookup_entry_by_type_descriptor(&scope->untagged_types, type_desc);
        if (entry != NULL)
        {
            debug_info("%s: Found untagged type.", __func__);
            return entry;
        }

        scope = scope->parent;
    }

    debug_info("%s: Type descriptor not found.", __func__);
    dump_type_descriptor(__func__, type_desc, DEBUG_LEVEL_INFO);

    return NULL;
}

// --- Typedef management ---

void
scope_add_typedef_entry(scope_t * scope, scope_typedef_entry_t entry)
{
    debug_info(
        "Adding typedef entry:\n\tname='%s'\n\ttag='%s'\n\tkind=%d\n\tllvm_type: %d\n\tpointee type: %d",
        entry.name,
        entry.tag,
        entry.kind,
        entry.type_desc != NULL ? (int)LLVMGetTypeKind(entry.type_desc->llvm_type) : -1,
        entry.type_desc != NULL && entry.type_desc->pointee != NULL
            ? (int)LLVMGetTypeKind(entry.type_desc->pointee->llvm_type)
            : -1
    );

    if (scope == NULL)
    {
        return;
    }

    scope_typedefs_add_entry(&scope->typedefs, entry);
}

void
scope_add_typedef_forward_decl(ir_generator_ctx_t * ctx, char const * typedef_name, char const * tag, type_kind_t kind)
{
    debug_info("%s: name: %s, tag: %s", __func__, typedef_name, tag);
    scope_typedef_entry_t entry = {
        .name = strdup(typedef_name),
        .tag = strdup(tag),
        .kind = kind,
    };

    /* FIXME: Need to add an 'incomplete' type into the registry, and fill it in once the typed type is defined. */
    switch (kind)
    {
    case TYPE_KIND_STRUCT:
    case TYPE_KIND_UNION:
        debug_error("TODO: Support required for struct/union typedef forward declarations");
        break;

    case TYPE_KIND_ENUM:
    {
        TypeDescriptor const * enum_desc = type_descriptor_get_int32_type(ctx->type_descriptors, false);
        entry.type_desc = enum_desc;
        type_info_t info = {
            .tag = strdup(tag),
            .type_desc = enum_desc,
            .kind = kind,
        };
        scope_add_tagged_type(ctx->current_scope, info);
        break;
    }
    case TYPE_KIND_UNTAGGED_STRUCT:
    case TYPE_KIND_UNTAGGED_UNION:
    case TYPE_KIND_UNTAGGED_ENUM:
    case TYPE_KIND_BUILTIN:
        break;
    }

    scope_add_typedef_entry(ctx->current_scope, entry);
}

scope_typedef_entry_t *
scope_lookup_typedef_entry_by_name(scope_t const * scope, char const * name)
{
    while (scope != NULL && name != NULL)
    {
        scope_typedef_entry_t * entry = scope_typedefs_lookup_entry_by_name(&scope->typedefs, name);
        if (entry != NULL)
        {
            return entry;
        }
        scope = scope->parent;
    }
    return NULL;
}

type_kind_t
scope_lookup_kind_by_type_descriptor(scope_t const * scope_in, TypeDescriptor const * type_desc)
{
    scope_t const * scope = scope_in;

    while (scope != NULL && type_desc != NULL)
    {
        scope_typedef_entry_t * typedef_entry
            = scope_typedefs_lookup_entry_by_type_descriptor(&scope->typedefs, type_desc);
        if (typedef_entry != NULL)
        {
            return typedef_entry->kind;
        }

        type_info_t * type_entry = scope_find_type_by_type_descriptor(scope, type_desc);
        if (type_entry != NULL)
        {
            return type_entry->kind;
        }

        scope = scope->parent;
    }

    return TYPE_KIND_BUILTIN;
}

TypeDescriptor const *
scope_find_typedef_type_descriptor(scope_t const * scope, char const * name)
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

    if (entry->type_desc != NULL)
    {
        return entry->type_desc;
    }

    type_info_t const * info = NULL;

    switch (entry->kind)
    {
    case TYPE_KIND_STRUCT:
        /* Look up the struct by tag name */
        info = scope_find_tagged_struct(scope, entry->tag);
        break;

    case TYPE_KIND_UNTAGGED_STRUCT:
        info = scope_find_untagged_struct(scope, entry->untagged_index);
        break;

    case TYPE_KIND_UNION:
        /* Look up the union by tag name */
        info = scope_find_tagged_union(scope, entry->tag);
        break;

    case TYPE_KIND_UNTAGGED_UNION:
        info = scope_find_untagged_union(scope, entry->untagged_index);
        break;

    case TYPE_KIND_ENUM:
        info = scope_find_tagged_enum(scope, entry->tag);
        break;

    case TYPE_KIND_UNTAGGED_ENUM:
        info = scope_find_untagged_enum(scope, entry->untagged_index);
        break;

    case TYPE_KIND_BUILTIN:
        debug_error("Typedef entry of builtin type has no type descriptor.");
        return NULL;

    default:
        debug_error("%s: Unsupported typedef kind: %d", __func__, entry->kind);
        return NULL;
    }

    return info != NULL ? info->type_desc : NULL;
}

// --- Symbol management ---

void
scope_add_symbol_with_tag(scope_t * scope, char const * name, TypedValue value, char const * tag)
{
    if (scope == NULL || name == NULL || value.value == NULL)
    {
        debug_error("%s: invalid parameters");
        return;
    }
    debug_info("%s: name: %s, tag: %s", __func__, name, tag);
    scope_symbols_add_entry_with_tag(&scope->symbols, name, value, tag);
}

void
add_symbol_with_struct(ir_generator_ctx_t * ctx, char const * name, TypedValue value, char const * tag)
{
    if (ctx == NULL)
    {
        return;
    }

    scope_add_symbol_with_tag(ctx->current_scope, name, value, tag);
}

void
add_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value)
{
    add_symbol_with_struct(ctx, name, value, NULL);
}

static symbol_t *
scope_find_symbol_entry(scope_t const * scope, char const * name)
{
    if (name == NULL)
    {
        return NULL;
    }

    while (scope != NULL && name != NULL)
    {
        debug_info("Finding symbol entry: name='%s' in scope=%p", name, (void *)scope);
        symbol_t * symbol = scope_symbols_lookup_entry_by_name(&scope->symbols, name);
        if (symbol != NULL)
        {
            return symbol;
        }

        scope = scope->parent;
    }

    debug_info("Invalid scope or name for finding symbol entry: name='%s', scope=%p", name, (void *)scope);

    return NULL;
}

static char const *
scope_find_symbol_tag_name(scope_t const * scope, char const * name)
{
    symbol_t * symbol = scope_find_symbol_entry(scope, name);
    if (symbol == NULL)
    {
        return NULL;
    }
    return symbol->tag_name;
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
scope_find_symbol(scope_t const * scope, char const * name, TypedValue * out_symbol)
{
    symbol_t * symbol = scope_find_symbol_entry(scope, name);
    if (symbol == NULL)
    {
        debug_info("Symbol not found for name: '%s'", name);
        if (out_symbol != NULL)
        {
            *out_symbol = NullTypedValue;
        }
        return false;
    }

    if (out_symbol != NULL)
    {
        *out_symbol = symbol->value;
    }

    return true;
}

bool
find_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue * out_symbol)
{
    // TODO: Refactor so that it returns TypedValue directly.
    if (ctx == NULL)
    {
        return false;
    }
    return scope_find_symbol(ctx->current_scope, name, out_symbol);
}

// --- Scope push/pop ---

void
scope_push(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;

    scope_t * new_scope = scope_create(ctx->current_scope, ctx->context, ctx->builder);
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

LLVMBasicBlockRef
scope_get_or_create_label(scope_t const * scope, char const * label_name)
{
    while (scope != NULL && scope->labels == NULL)
    {
        scope = scope->parent;
    }
    if (scope == NULL)
    {
        return NULL;
    }
    return labels_get_or_create_label(scope->labels, label_name);
}