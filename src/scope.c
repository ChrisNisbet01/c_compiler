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
        type_lists_t * list = &scope->type_lists[i];

        debug_info("%s init list %zu (%p)", __func__, i, (void *)list);
        if (!scope_type_lists_init(list))
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
        type_lists_t * list = &scope->type_lists[i];
        scope_type_lists_free(list);
    }

    free(scope);
}

// --- Type management ---

type_info_t const *
scope_add_type_info(scope_t * scope, type_info_t info)
{
    debug_info("%s", __func__);
    debug_info(
        "Adding type: scope=%p, tag='%s', kind=%d", (void *)scope, info.tag != NULL ? info.tag : "NULL", info.kind
    );

    if (scope == NULL)
    {
        return NULL;
    }

    type_lists_t * list = &scope->type_lists[info.kind];
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
        type_lists_t const * list = &scope->type_lists[kind];
        type_info_t * entry = scope_types_lookup_entry_by_tag(list, tag);

        if (entry != NULL)
        {
            return entry;
        }
        scope = scope->parent;
    }
    return NULL;
}

type_info_t *
scope_lookup_type_info_by_type_descriptor(scope_t const * scope_in, TypeDescriptor const * type_desc)
{
    if (type_desc == NULL)
    {
        return NULL;
    }

    scope_t const * scope = scope_in;

    while (scope != NULL)
    {
        for (size_t i = 0; i < TYPE_KIND_COUNT__; i++)
        {
            type_lists_t const * list = &scope->type_lists[i];
            type_info_t * entry = scope_types_lookup_entry_by_type_descriptor(list, type_desc);
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
scope_add_symbol(scope_t * scope, char const * name, TypedValue value)
{
    if (scope == NULL || name == NULL || value.value == NULL)
    {
        debug_error("%s: invalid parameters");
        return;
    }
    debug_info("%s: name: %s", __func__, name);
    scope_symbols_add_entry(&scope->symbols, name, value);
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