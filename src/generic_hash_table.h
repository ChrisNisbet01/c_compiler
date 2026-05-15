#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct generic_hash_table_t generic_hash_table_t;

typedef size_t (*generic_hash_fn)(void const * key);

typedef bool (*generic_eq_fn)(void const * key1, void const * key2);

typedef void (*generic_val_free_fn)(void * value);

typedef struct
{
    generic_hash_fn hash;
    generic_eq_fn equals;
} generic_hash_table_key_ops_t;

generic_hash_table_t *
generic_hash_table_create(size_t initial_bucket_count, generic_hash_table_key_ops_t const * key_ops);

void generic_hash_table_destroy(generic_hash_table_t * ht);

bool generic_hash_table_insert(generic_hash_table_t * ht, void const * key, void * value);

void * generic_hash_table_lookup(generic_hash_table_t const * ht, void const * key);

void * generic_hash_table_remove(generic_hash_table_t * ht, void const * key);

void generic_hash_table_iterate(
    generic_hash_table_t const * ht, void (*callback)(void * value, void * user_data), void * user_data
);

size_t generic_hash_table_size(generic_hash_table_t const * ht);