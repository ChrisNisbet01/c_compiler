#include "symbol_table.h"

#include "generic_hash_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_SYMBOL_CAPACITY 16

typedef struct symbol_table_t
{
    char const ** names;
    size_t count;
    size_t capacity;
    generic_hash_table_t * hash;
} symbol_table_t;

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

static generic_hash_table_key_ops_t string_key_ops = {
    .hash = hash_djb2,
    .equals = str_equals,
};

static void
free_string_entry(void * value, void * user_data)
{
    (void)user_data;
    char * str = (char *)value;
    if (str != NULL)
    {
        free(str);
    }
}

symbol_table_t *
symbol_table_create(void)
{
    symbol_table_t * st = calloc(1, sizeof(*st));
    if (st == NULL)
    {
        return NULL;
    }

    st->capacity = INITIAL_SYMBOL_CAPACITY;
    st->names = malloc(sizeof(*st->names) * st->capacity);
    if (st->names == NULL)
    {
        free(st);
        return NULL;
    }
    st->hash = generic_hash_table_create(16, &string_key_ops);
    if (st->hash == NULL)
    {
        free(st->names);
        free(st);
        return NULL;
    }
    st->count = 0;
    return st;
}

void
symbol_table_free(symbol_table_t * st)
{
    if (st == NULL)
    {
        return;
    }

    if (st->hash != NULL)
    {
        generic_hash_table_iterate(st->hash, free_string_entry, NULL);
        generic_hash_table_destroy(st->hash);
    }
    free(st->names);
    free(st);
}

void
symbol_table_add(symbol_table_t * st, char const * name)
{
    if (st == NULL || name == NULL)
    {
        return;
    }

    if (generic_hash_table_lookup(st->hash, name) != NULL)
    {
        return;
    }

    if (st->count >= st->capacity)
    {
        st->capacity *= 2;
        char const ** new_names = realloc(st->names, sizeof(*st->names) * st->capacity);
        if (new_names == NULL)
        {
            return;
        }
        st->names = new_names;
    }

    char * stored = strdup(name);
    if (stored == NULL)
    {
        return;
    }

    if (!generic_hash_table_insert(st->hash, stored, stored))
    {
        free(stored);
        return;
    }
    st->names[st->count++] = stored;
}

void
symbol_table_clear_from(symbol_table_t * st, size_t index)
{
    if (index >= st->count)
    {
        return;
    }

    for (size_t i = index; i < st->count; i++)
    {
        generic_hash_table_remove(st->hash, st->names[i]);
    }
    st->count = index;
}

bool
symbol_table_contains(symbol_table_t const * st, char const * name)
{
    if (st == NULL || name == NULL)
    {
        return false;
    }
    return generic_hash_table_lookup(st->hash, name) != NULL;
}

size_t
symbol_table_count(symbol_table_t const * st)
{
    return st->count;
}

char const *
symbol_table_name_at(symbol_table_t const * st, size_t index)
{
    if (index >= st->count)
    {
        return NULL;
    }

    return st->names[index];
}