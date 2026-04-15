#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct symbol_table_t symbol_table_t;

symbol_table_t * symbol_table_create(void);
void symbol_table_free(symbol_table_t * st);
void symbol_table_add(symbol_table_t * st, char const * name);
size_t symbol_table_count(symbol_table_t * st);
char const * symbol_table_name_at(symbol_table_t * st, size_t index);
void symbol_table_clear_from(symbol_table_t * st, size_t index);
bool symbol_table_contains(symbol_table_t * st, char const * name);

#endif /* SYMBOL_TABLE_H */
