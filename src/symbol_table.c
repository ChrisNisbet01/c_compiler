#include "symbol_table.h"

#include "debug.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_SYMBOL_CAPACITY 16

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

    for (size_t i = 0; i < st->count; i++)
    {
        free(st->names[i]);
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

    for (size_t i = 0; i < st->count; i++)
    {
        if (strcmp(st->names[i], name) == 0)
        {
            return;
        }
    }

    if (st->count >= st->capacity)
    {
        st->capacity *= 2;
        char ** new_names = realloc(st->names, sizeof(*st->names) * st->capacity);
        if (new_names == NULL)
        {
            debug_error("Error: Failed to resize symbol table names array.");
            return;
        }
        st->names = new_names;
    }

    st->names[st->count++] = strdup(name);
}

bool
symbol_table_contains(symbol_table_t * st, char const * name)
{
    if (st == NULL || name == NULL || st->count == 0)
    {
        return false;
    }

    for (size_t i = 0; i < st->count; i++)
    {
        if (strcmp(st->names[i], name) == 0)
        {
            return true;
        }
    }

    return false;
}
