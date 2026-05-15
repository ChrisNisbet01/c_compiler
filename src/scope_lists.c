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

static bool
hash_skip_duplicate(void * existing_value, void * new_value)
{
    type_info_t const * existing_info = (type_info_t *)existing_value;
    type_info_t const * new_info = (type_info_t *)new_value;

    if (existing_info == NULL || new_info == NULL)
    {
        return false;
    }

    return existing_info->type_desc == new_info->type_desc;
}

static generic_hash_table_key_ops_t tag_key_ops = {.hash = hash_djb2, .equals = str_equals};

static generic_hash_table_key_ops_t type_desc_key_ops
    = {.hash = hash_ptr, .equals = ptr_equals, .skip_duplicate = hash_skip_duplicate};

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
            debug_info("%s: updating existing entry: %s", __func__, info.tag);
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
            debug_info("%s: updating existing entry", __func__);
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
    if (info.type_desc != NULL)
    {
        debug_info(
            "%s: inserting into by_type_desc with key %p, tag: %s",
            __func__,
            (void *)info.type_desc,
            info.tag != NULL ? info.tag : "NULL"
        );
        if (!generic_hash_table_insert(list->tagged.by_type_desc, info.type_desc, new_entry))
        {
            debug_info("%s: failed to insert into by_type_desc with key %p", __func__, (void *)info.type_desc);
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

    return (type_info_t *)generic_hash_table_lookup(list->tagged.by_type_desc, type_desc);
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

void
scope_typedefs_free(scope_typedefs_t * list)
{
    for (size_t i = 0; i < list->count; ++i)
    {
        scope_typedef_entry_t * entry = list->entries[i];
        free(entry->name);
        free(entry);
    }
    free(list->entries);
}

scope_typedef_entry_t *
scope_typedefs_lookup_entry_by_name(scope_typedefs_t const * list, char const * name)
{
    for (size_t i = 0; i < list->count; ++i)
    {
        scope_typedef_entry_t * entry = list->entries[i];
        if (entry->name && strcmp(entry->name, name) == 0)
        {
            return entry;
        }
    }

    return NULL;
}

scope_typedef_entry_t *
scope_typedefs_lookup_entry_by_type_descriptor(scope_typedefs_t const * list, TypeDescriptor const * type_desc)
{
    for (size_t i = 0; i < list->count; ++i)
    {
        scope_typedef_entry_t * entry = list->entries[i];
        if (type_desc == entry->type_desc)
        {
            return entry;
        }
    }

    return NULL;
}

bool
scope_typedefs_init(scope_typedefs_t * list)
{
    list->capacity = 4;
    list->entries = calloc(list->capacity, sizeof(*list->entries));
    list->count = 0;

    return list->entries != NULL;
}

void
scope_typedefs_add_entry(scope_typedefs_t * list, scope_typedef_entry_t entry)
{
    for (size_t i = 0; i < list->count; ++i)
    {
        scope_typedef_entry_t * existing = list->entries[i];
        if (existing->name != NULL && entry.name != NULL && strcmp(existing->name, entry.name) == 0)
        {
            debug_info(
                "%s updating typedef entry existing: %s, new: %s. previous type desc: %p, new type desc: %p",
                __func__,
                existing->name,
                entry.name,
                (void *)existing->type_desc,
                (void *)entry.type_desc
            );
            free(existing->name);
            existing->name = entry.name;
            existing->type_desc = entry.type_desc;

            return;
        }
    }

    debug_info("%s: adding new entry: %s", __func__, entry.name);
    if (list->count >= list->capacity)
    {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        scope_typedef_entry_t ** new_entries = realloc(list->entries, new_cap * sizeof(*new_entries));
        if (new_entries == NULL)
        {
            return;
        }
        list->entries = new_entries;
        list->capacity = new_cap;
    }

    scope_typedef_entry_t * new_entry = malloc(sizeof(*new_entry));
    if (new_entry == NULL)
    {
        return;
    }
    *new_entry = entry;

    list->entries[list->count++] = new_entry;
}

void
scope_symbols_free(scope_symbols_t * list)
{
    for (size_t i = 0; i < list->count; ++i)
    {
        symbol_t * symbol = list->symbols[i];
        free((void *)symbol->name);
        free(symbol);
    }
    free(list->symbols);
}

bool
scope_symbols_init(scope_symbols_t * list)
{
    list->capacity = 16;
    list->symbols = calloc(list->capacity, sizeof(*list->symbols));
    list->count = 0;

    return list->symbols != NULL;
}

void
scope_symbols_add_entry(scope_symbols_t * list, char const * name, TypedValue value)
{
    if (list->count >= list->capacity)
    {
        size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        symbol_t ** new_symbols = realloc(list->symbols, new_cap * sizeof(*new_symbols));

        if (new_symbols == NULL)
        {
            return;
        }
        list->symbols = new_symbols;
        list->capacity = new_cap;
    }

    symbol_t * new_symbol = calloc(1, sizeof(*new_symbol));
    if (new_symbol == NULL)
    {
        return;
    }
    new_symbol->name = strdup(name);
    new_symbol->value = value;
    list->symbols[list->count] = new_symbol;
    list->count++;

    debug_info("Added symbol: name='%s'", name);
    dump_typed_value("added symbol value", value);
}

symbol_t *
scope_symbols_lookup_entry_by_name(scope_symbols_t const * list, char const * name)
{
    for (size_t i = list->count; i > 0; --i)
    {
        symbol_t * symbol = list->symbols[i - 1];

        if (symbol->name != NULL && strcmp(symbol->name, name) == 0)
        {
            dump_typed_value("Found symbol entry value", symbol->value);
            return symbol;
        }
    }

    return NULL;
}