/**
 * @file scope.c
 * @brief Type and symbol table management implementation for NCC compiler.
 */

#include "scope.h"

#include "debug.h"
#include "typed_value.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Scope lifecycle ---

scope_t *
scope_create(scope_t * parent, LLVMContextRef context, LLVMBuilderRef builder)
{
    scope_t * scope = calloc(1, sizeof(*scope));
    debug_info("%s: scope: %p", __func__, (void *)scope);

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

    for (size_t i = 0; i < TYPE_KIND_COUNT__; i++)
    {
        scope_types_t * list = &scope->type_lists[i].tag_or_index;

        debug_info("%s init list %zu (%p)", __func__, i, (void *)list);
        if (!scope_types_init(list))
        {
            scope_free(scope);

            return NULL;
        }
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
    debug_info("%s, %p", __func__, (void *)scope);
    if (scope == NULL)
    {
        return;
    }

    scope_symbols_free(&scope->symbols);
    scope_typedefs_free(&scope->typedefs);
    labels_list_destroy(scope->labels);

    for (size_t i = 0; i < TYPE_KIND_COUNT__; i++)
    {
        scope_types_t * list = &scope->type_lists[i].tag_or_index;

        scope_types_free(list);
    }

    free(scope);
}

// --- Type management ---

type_info_t const *
scope_add_tagged_type(scope_t * scope, type_info_t info)
{
    debug_info("%s", __func__);
    if (scope == NULL || info.tag == NULL)
    {
        return NULL;
    }
    debug_info("Adding tagged type: scope=%p, tag='%s', kind=%d", (void *)scope, info.tag, info.kind);
    scope_types_t * list = &scope->type_lists[info.kind].tag_or_index;

    return scope_types_add_entry(list, info);
}

type_info_t const *
scope_add_untagged_type(scope_t * scope, type_info_t info)
{
    debug_info("%s", __func__);
    if (scope == NULL)
    {
        return NULL;
    }

    scope_types_t * list = &scope->type_lists[info.kind].tag_or_index;
    type_info_t const * result = scope_types_add_entry(list, info);

    return result;
}

// --- Tagged type lookup ---

type_info_t *
scope_lookup_tagged_entry_by_tag_and_kind(scope_t const * scope, char const * tag, type_kind_t kind)
{
    debug_info("%s", __func__);
    while (scope != NULL && tag != NULL)
    {
        scope_types_t const * list = &scope->type_lists[kind].tag_or_index;
        type_info_t * entry = scope_types_lookup_entry_by_tag(list, tag);

        if (entry != NULL)
        {
            return entry;
        }
        scope = scope->parent;
    }
    return NULL;
}

static type_info_t *
scope_find_tagged_type(scope_t const * scope, char const * tag, type_kind_t kind)
{
    debug_info("%s", __func__);
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
    debug_info("%s", __func__);
    return scope_find_tagged_type(scope, tag, TYPE_KIND_STRUCT);
}

type_info_t *
scope_find_tagged_union(scope_t const * scope, char const * tag)
{
    debug_info("%s", __func__);
    return scope_find_tagged_type(scope, tag, TYPE_KIND_UNION);
}

type_info_t *
scope_find_tagged_enum(scope_t const * scope, char const * tag)
{
    debug_info("%s", __func__);
    return scope_find_tagged_type(scope, tag, TYPE_KIND_ENUM);
}

type_info_t *
scope_find_type_by_type_descriptor(scope_t const * scope, TypeDescriptor const * const type_desc)
{
    if (type_desc == NULL)
    {
        return NULL;
    }
    while (scope != NULL)
    {
        for (size_t i = 0; i < TYPE_KIND_COUNT__; i++)
        {
            type_info_t * entry
                = scope_types_lookup_entry_by_type_descriptor(&scope->type_lists[i].tag_or_index, type_desc);

            if (entry != NULL)
            {
                debug_info("%s: Found tagged type.", __func__);
                return entry;
            }
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
        "Adding typedef entry:\n\tname='%s'\n\tllvm_type: %d\n\tpointee type: %d",
        entry.name,
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

scope_typedef_entry_t *
scope_lookup_typedef_entry_by_type_descriptor(scope_t const * scope, TypeDescriptor const * type_desc)
{
    while (scope != NULL && type_desc != NULL)
    {
        scope_typedef_entry_t * typedef_entry
            = scope_typedefs_lookup_entry_by_type_descriptor(&scope->typedefs, type_desc);
        if (typedef_entry != NULL)
        {
            return typedef_entry;
        }

        scope = scope->parent;
    }

    return NULL;
}

type_info_t *
scope_lookup_type_info_by_type_descriptor(scope_t const * scope_in, TypeDescriptor const * type_desc)
{
    scope_t const * scope = scope_in;

    while (scope != NULL && type_desc != NULL)
    {
        type_info_t * type_entry = scope_find_type_by_type_descriptor(scope, type_desc);

        if (type_entry != NULL)
        {
            return type_entry;
        }

        scope = scope->parent;
    }

    return NULL;
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
    debug_info("Found typedef entry: name='%s', type desc: %p", entry->name, (void *)entry->type_desc);

    return entry->type_desc;
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

symbol_t *
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