#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration for opaque hash table */
typedef struct hash_table_t hash_table_t;

/**
 * Create a new hash table.
 * @param initial_bucket_count  Number of buckets to allocate initially. Must be > 0.
 * @return pointer to a new hash table or NULL on allocation failure.
 */
hash_table_t * hash_table_create(size_t initial_bucket_count);

/**
 * Free a hash table and all keys stored inside it.
 */
void hash_table_free(hash_table_t * ht);

/**
 * Insert a key into the hash table. The function makes its own copy of the key.
 * If the key already exists the table is left unchanged.
 * @return true if the key was inserted, false if it already existed or on error.
 */
/**
 * Insert a key into the hash table. The function makes its own copy of the key.
 * @return pointer to the stored copy on success, NULL on failure or if key already existed.
 */
char * hash_table_insert(hash_table_t * ht, char const * key);

/**
 * Remove a key from the hash table. The stored copy of the key is freed.
 * @return true if the key was found and removed, false otherwise.
 */
bool hash_table_remove(hash_table_t * ht, char const * key);

/**
 * Test whether a key exists in the hash table.
 */
bool hash_table_contains(hash_table_t const * ht, char const * key);

/**
 * Get the current number of stored keys.
 */
size_t hash_table_size(hash_table_t const * ht);
