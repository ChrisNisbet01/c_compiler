#include "symbol_table.h"
#include "hash_table.h"

#include "debug.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_SYMBOL_CAPACITY 16

#include "hash_table.h"

typedef struct symbol_table_t
{
    const char ** names;      /* pointers to strings owned by hash table */
    size_t count;
    size_t capacity;
    hash_table_t *hash;       /* internal hash table for O(1) lookups */
} symbol_table_t;

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
    /* create internal hash table with initial bucket count */
    st->hash = hash_table_create(16);
    if (st->hash == NULL) {
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

    /* free hash table (which also frees duplicated strings) */
    hash_table_free(st->hash);
    /* names array only holds non‑owned pointers, no need to free the strings */
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

    /* check hash table for existence (hash owns the string) */
    if (hash_table_contains(st->hash, name)) {
        return;
    }

    if (st->count >= st->capacity)
    {
        st->capacity *= 2;
        const char ** new_names = realloc(st->names, sizeof(*st->names) * st->capacity);
        if (new_names == NULL)
        {
            debug_error("Error: Failed to resize symbol table names array.");
            return;
        }
        st->names = new_names;
    }

    /* Insert into hash table; it returns the owned copy */
    char *stored = hash_table_insert(st->hash, name);
    if (!stored) {
        /* insertion failed (likely OOM or duplicate) */
        return;
    }
    st->names[st->count++] = stored; /* linear array points to owned string */
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
        /* remove each name from hash table (which frees the string) */
        hash_table_remove(st->hash, st->names[i]);
        /* linear array holds non‑owned pointers, no need to free */
    }
    st->count = index;
}

bool
symbol_table_contains(symbol_table_t * st, char const * name)
{
    if (st == NULL || name == NULL || st->count == 0)
    {
        return false;
    }
    return hash_table_contains(st->hash, name);
}

size_t
symbol_table_count(symbol_table_t * st)
{
    return st->count;
}

char const *
symbol_table_name_at(symbol_table_t * st, size_t index)
{
    if (index >= st->count)
    {
        return NULL;
    }

    return st->names[index];
}
