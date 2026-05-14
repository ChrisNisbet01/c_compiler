#include "scope_lists.h"

#include <stdlib.h>
#include <string.h>

static type_info_t const *
scope_type_list_add_entry(scope_types_t * list, type_info_t info)
{
    /* Update existing entry if one with the same tag and kind already exists */
    if (list->count >= list->capacity)
    {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;

        type_info_t ** new_entries = realloc(list->entries, new_cap * sizeof(*new_entries));

        if (new_entries == NULL)
        {
            return NULL;
        }
        list->entries = new_entries;
        list->capacity = new_cap;
    }
    type_info_t * new_entry = malloc(sizeof *new_entry);
    if (new_entry == NULL)
    {
        return NULL;
    }

    *new_entry = info;

    list->entries[list->count++] = new_entry;
    debug_info(
        "Added type info: tag='%s', kind=%d, total count=%zu, desc: %p",
        info.tag ? info.tag : "(null)",
        info.kind,
        list->count,
        info.type_desc
    );

    return list->entries[list->count - 1];
}

type_info_t const *
scope_types_add_entry(type_lists_t * list, type_info_t info)
{
    if (info.tag != NULL)
    {
        type_info_t * existing = scope_types_lookup_entry_by_tag(list, info.tag);
        if (existing != NULL)
        {
            debug_warning("%s already exists");
            return existing;
        }
    }

    scope_type_list_add_entry(&list->tag_or_index, info);
    type_info_t const * new_entry = scope_type_list_add_entry(&list->type_desc, info);

    return new_entry;
}

type_info_t *
scope_types_lookup_entry_by_tag(type_lists_t const * list, char const * tag)
{
    if (list == NULL || tag == NULL)
    {
        return NULL;
    }

    scope_types_t const * tag_list = &list->tag_or_index;

    for (size_t i = 0; i < tag_list->count; ++i)
    {
        type_info_t * entry = tag_list->entries[i];

        if (entry->tag != NULL && strcmp(entry->tag, tag) == 0)
        {
            return entry;
        }
    }

    return NULL;
}

type_info_t *
scope_types_lookup_entry_by_type_descriptor(type_lists_t const * list, TypeDescriptor const * type_desc)
{
    if (list == NULL || type_desc == NULL)
    {
        return NULL;
    }

    scope_types_t const * tag_list = &list->type_desc;

    for (size_t i = 0; i < tag_list->count; ++i)
    {
        type_info_t * entry = tag_list->entries[i];

        debug_info("%s: Checking entry: %zu", __func__, i);
        if (entry->type_desc == type_desc)
        {
            debug_info("%s: Found match.", __func__);
            return entry;
        }
    }

    return NULL;
}

static void
free_type_info(type_info_t * info)
{
    if (info == NULL)
    {
        return;
    }

    free(info->tag);
    for (size_t i = 0; i < info->field_count; ++i)
    {
        free(info->fields[i].name);
    }
    free(info->fields);
}

void
scope_types_free(scope_types_t * list)
{
    if (list->is_master)
    {
        for (size_t i = 0; i < list->count; ++i)
        {
            type_info_t * entry = list->entries[i];

            free_type_info(entry);
            free(entry);
        }
    }
    free(list->entries);
}

bool
scope_types_init(scope_types_t * list, bool is_master)
{
    list->capacity = 4;
    list->entries = calloc(list->capacity, sizeof(*list->entries));
    list->is_master = is_master;
    return list->entries != NULL;
}

void
scope_type_lists_free(type_lists_t * list)
{
    scope_types_free(&list->tag_or_index);
    scope_types_free(&list->type_desc);
}

bool
scope_type_lists_init(type_lists_t * list)
{
    if (!scope_types_init(&list->tag_or_index, true))
    {
        return false;
    }
    if (!scope_types_init(&list->type_desc, false))
    {
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

    return list->entries != NULL;
}

void
scope_typedefs_add_entry(scope_typedefs_t * list, scope_typedef_entry_t entry)
{
    /* Update existing entry if one with the same name already exists */
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
                existing->type_desc,
                entry.type_desc
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
    /* Free all symbol names and struct names in this scope */
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
