#include "scope_lists.h"

#include <stdlib.h>
#include <string.h>

static size_t
hash_djb2(void const * key)
{
    char const * str = (char const *)key;
    size_t hash = 5381;

    int c;
    while ((c = (unsigned char)*str++) != '\0')
    {
        hash = ((hash << 5) + hash) + (size_t)c;
    }

    return hash;
}

static bool
str_equals(void const * key1, void const * key2)
{
    return strcmp((char const *)key1, (char const *)key2) == 0;
}

static size_t
hash_ptr(void const * pkey)
{
    size_t key = (size_t)(uintptr_t)pkey;

    key = (~key) + (key << 21); // key = (key << 21) - key - 1;
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8); // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4); // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return key;
}

static bool
ptr_equals(void const * key1, void const * key2)
{
    return key1 == key2;
}

static generic_hash_table_key_ops_t tag_key_ops = {
    .hash = hash_djb2,
    .equals = str_equals,
};

static generic_hash_table_key_ops_t type_desc_key_ops = {
    .hash = hash_ptr,
    .equals = ptr_equals,
};

static void
free_type_info_and_entry(void * value, void * user_data)
{
    (void)user_data;
    type_info_t * info = (type_info_t *)value;
    if (info == NULL)
    {
        return;
    }
    free(info->tag);
    free(info);
}

type_info_t const *
scope_types_add_entry(type_lists_t * list, type_info_t info)
{
    if (info.tag != NULL)
    {
        type_info_t * existing = scope_types_lookup_entry_by_tag(list, info.tag);
        if (existing != NULL)
        {
            free(existing->tag);
            *existing = info;
            return existing;
        }
    }
    else
    {
        type_info_t * existing = scope_types_lookup_entry_by_type_descriptor(list, info.type_desc);
        if (existing != NULL)
        {
            free(existing->tag);
            *existing = info;
            return existing;
        }
    }

    type_info_t * new_entry = malloc(sizeof *new_entry);
    if (new_entry == NULL)
    {
        return NULL;
    }

    *new_entry = info;

    if (info.tag != NULL)
    {
        if (!generic_hash_table_insert(list->tagged.by_tag, info.tag, new_entry))
        {
            free(new_entry);
            return NULL;
        }
    }
    else if (info.type_desc != NULL)
    {
        if (!generic_hash_table_insert(list->type_desc.by_type_desc, info.type_desc, new_entry))
        {
            free(new_entry);
            return NULL;
        }
    }

    return new_entry;
}

type_info_t *
scope_types_lookup_entry_by_tag(type_lists_t const * list, char const * tag)
{
    if (list == NULL || tag == NULL)
    {
        return NULL;
    }

    return (type_info_t *)generic_hash_table_lookup(list->tagged.by_tag, tag);
}

type_info_t *
scope_types_lookup_entry_by_type_descriptor(type_lists_t const * list, TypeDescriptor const * type_desc)
{
    if (list == NULL || type_desc == NULL)
    {
        return NULL;
    }

    return (type_info_t *)generic_hash_table_lookup(list->type_desc.by_type_desc, type_desc);
}

void
scope_types_free(scope_types_t * list)
{
    if (list->is_master)
    {
        generic_hash_table_iterate(list->by_tag, free_type_info_and_entry, NULL);
    }
    generic_hash_table_destroy(list->by_tag);
    generic_hash_table_destroy(list->by_type_desc);
    list->by_tag = NULL;
    list->by_type_desc = NULL;
}

bool
scope_types_init(scope_types_t * list, bool is_master)
{
    list->is_master = is_master;
    list->by_tag = generic_hash_table_create(128, &tag_key_ops);
    if (list->by_tag == NULL)
    {
        return false;
    }
    list->by_type_desc = generic_hash_table_create(128, &type_desc_key_ops);
    if (list->by_type_desc == NULL)
    {
        generic_hash_table_destroy(list->by_tag);
        list->by_tag = NULL;
        return false;
    }
    return true;
}

void
scope_type_lists_free(type_lists_t * list)
{
    scope_types_free(&list->tagged);
    scope_types_free(&list->type_desc);
}

bool
scope_type_lists_init(type_lists_t * list)
{
    if (!scope_types_init(&list->tagged, true))
    {
        return false;
    }
    if (!scope_types_init(&list->type_desc, false))
    {
        scope_types_free(&list->tagged);
        return false;
    }

    return true;
}

static void
free_scope_typedef_entry(void * value, void * user_data)
{
    (void)user_data;
    scope_typedef_entry_t * entry = (scope_typedef_entry_t *)value;
    if (entry == NULL)
    {
        return;
    }
    free((void *)entry->name);
    free(entry);
}

static void
free_symbol(void * value, void * user_data)
{
    (void)user_data;
    symbol_t * symbol = (symbol_t *)value;
    if (symbol == NULL)
    {
        return;
    }
    free((void *)symbol->name);
    free(symbol);
}

void
scope_typedefs_free(scope_typedefs_t * list)
{
    if (list->by_name != NULL)
    {
        generic_hash_table_iterate(list->by_name, free_scope_typedef_entry, NULL);
        generic_hash_table_destroy(list->by_name);
        list->by_name = NULL;
    }
}

scope_typedef_entry_t *
scope_typedefs_lookup_entry_by_name(scope_typedefs_t const * list, char const * name)
{
    if (list == NULL || name == NULL)
    {
        return NULL;
    }
    return (scope_typedef_entry_t *)generic_hash_table_lookup(list->by_name, name);
}

bool
scope_typedefs_init(scope_typedefs_t * list)
{
    list->by_name = generic_hash_table_create(128, &tag_key_ops);
    return list->by_name != NULL;
}

void
scope_typedefs_add_entry(scope_typedefs_t * list, scope_typedef_entry_t entry)
{
    if (list == NULL || entry.name == NULL)
    {
        return;
    }

    scope_typedef_entry_t * existing = scope_typedefs_lookup_entry_by_name(list, entry.name);
    if (existing != NULL)
    {
        /*
            Important - Don't replace the existing->name as the hash table uses that pointer as the key.
            Replacing it here would result in a use-after-free bug.
            (Alternatively, remove one entry and replace it with the other)
         */
        free((void *)entry.name);
        existing->type_desc = entry.type_desc;
        return;
    }

    scope_typedef_entry_t * new_entry = malloc(sizeof(*new_entry));
    if (new_entry == NULL)
    {
        return;
    }
    *new_entry = entry;
    generic_hash_table_insert(list->by_name, entry.name, new_entry);
}

void
scope_symbols_free(scope_symbols_t * list)
{
    if (list->by_name != NULL)
    {
        generic_hash_table_iterate(list->by_name, free_symbol, NULL);
        generic_hash_table_destroy(list->by_name);
        list->by_name = NULL;
    }
}

bool
scope_symbols_init(scope_symbols_t * list)
{
    list->by_name = generic_hash_table_create(128, &tag_key_ops);
    return list->by_name != NULL;
}

void
scope_symbols_add_entry(scope_symbols_t * list, char const * name, TypedValue value)
{
    if (list == NULL || name == NULL)
    {
        return;
    }

    symbol_t * existing = scope_symbols_lookup_entry_by_name(list, name);
    if (existing != NULL)
    {
        free((void *)existing->name);
        existing->name = strdup(name);
        existing->value = value;
        return;
    }

    symbol_t * new_symbol = calloc(1, sizeof(*new_symbol));
    if (new_symbol == NULL)
    {
        return;
    }
    new_symbol->name = strdup(name);
    new_symbol->value = value;
    generic_hash_table_insert(list->by_name, name, new_symbol);
}

symbol_t *
scope_symbols_lookup_entry_by_name(scope_symbols_t const * list, char const * name)
{
    if (list == NULL || name == NULL)
    {
        return NULL;
    }

    return (symbol_t *)generic_hash_table_lookup(list->by_name, name);
}