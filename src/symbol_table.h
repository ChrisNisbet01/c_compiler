#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char ** names;
    size_t count;
    size_t capacity;
} symbol_table_t;

symbol_table_t * symbol_table_create(void);
void symbol_table_free(symbol_table_t * st);
void symbol_table_add(symbol_table_t * st, char const * name);
bool symbol_table_contains(symbol_table_t * st, char const * name);

#endif /* SYMBOL_TABLE_H */
