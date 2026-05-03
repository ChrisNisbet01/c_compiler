#include "scope_lists.h"

#include <stdlib.h>
#include <string.h>

type_info_t const *
add_info_to_list(scope_types_t * list, type_info_t info)
{
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

type_info_t *
scope_types_lookup_entry_by_tag(scope_types_t const * list, char const * tag)
{
    if (list == NULL || tag == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < list->count; ++i)
    {
        type_info_t * entry = &list->entries[i];

        if (entry->tag != NULL && strcmp(entry->tag, tag) == 0)
        {
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
    for (size_t i = 0; i < list->count; ++i)
    {
        type_info_t * entry = &list->entries[i];

        free_type_info(entry);
    }
    free(list->entries);
}

void
scope_typedefs_free(scope_typedefs_t * list)
{
    for (size_t i = 0; i < list->count; ++i)
    {
        scope_typedef_entry_t * entry = &list->entries[i];

        free(entry->name);
        free((void *)entry->tag);
    }
    free(list->entries);
}

void
scope_symbols_free(scope_symbols_t * list)
{
    /* Free all symbol names and struct names in this scope */
    for (size_t i = 0; i < list->count; ++i)
    {
        symbol_t * symbol = &list->symbols[i];

        free((void *)symbol->name);
        free((void *)symbol->tag_name);
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

bool
scope_types_init(scope_types_t * list)
{
    list->capacity = 4;
    list->entries = calloc(list->capacity, sizeof(*list->entries));

    return list->entries != NULL;
}

bool
scope_typedefs_init(scope_typedefs_t * list)
{
    list->capacity = 4;
    list->entries = calloc(list->capacity, sizeof(*list->entries));

    return list->entries != NULL;
}
