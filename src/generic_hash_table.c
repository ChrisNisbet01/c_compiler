#include "generic_hash_table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct generic_hash_entry_t
{
    void const * key;
    void * value;
    struct generic_hash_entry_t * next;
} generic_hash_entry_t;

struct generic_hash_table_t
{
    generic_hash_table_key_ops_t key_ops;
    size_t bucket_count;
    size_t size;
    generic_hash_entry_t ** buckets;
    float load_factor;
};

size_t
generic_hash_table_size(generic_hash_table_t const * ht)
{
    if (ht == NULL)
    {
        return 0;
    }
    return ht->size;
}

generic_hash_table_t *
generic_hash_table_create(size_t initial_bucket_count, generic_hash_table_key_ops_t const * key_ops)
{
    if (key_ops == NULL || key_ops->hash == NULL || key_ops->equals == NULL)
    {
        return NULL;
    }

    if (initial_bucket_count == 0)
    {
        initial_bucket_count = 128;
    }

    generic_hash_table_t * ht = calloc(1, sizeof(*ht));
    if (ht == NULL)
    {
        return NULL;
    }

    ht->key_ops = *key_ops;
    ht->bucket_count = initial_bucket_count;
    ht->load_factor = 0.75f;
    ht->buckets = calloc(ht->bucket_count, sizeof(*ht->buckets));

    if (ht->buckets == NULL)
    {
        free(ht);
        return NULL;
    }

    return ht;
}

static void
generic_hash_table_free_entries(generic_hash_entry_t * entry)
{
    while (entry != NULL)
    {
        generic_hash_entry_t * next = entry->next;
        free(entry);
        entry = next;
    }
}

void
generic_hash_table_destroy(generic_hash_table_t * ht)
{
    if (ht == NULL)
    {
        return;
    }

    for (size_t i = 0; i < ht->bucket_count; ++i)
    {
        generic_hash_table_free_entries(ht->buckets[i]);
    }

    free(ht->buckets);
    free(ht);
}

static bool
generic_hash_table_resize(generic_hash_table_t * ht, size_t new_bucket_count)
{
    if (ht == NULL)
    {
        return false;
    }

    generic_hash_entry_t ** new_buckets = calloc(new_bucket_count, sizeof(*new_buckets));
    if (new_buckets == NULL)
    {
        return false;
    }

    for (size_t i = 0; i < ht->bucket_count; ++i)
    {
        generic_hash_entry_t * e = ht->buckets[i];
        while (e != NULL)
        {
            generic_hash_entry_t * next = e->next;
            size_t idx = ht->key_ops.hash(e->key) % new_bucket_count;
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }

    free(ht->buckets);
    ht->buckets = new_buckets;
    ht->bucket_count = new_bucket_count;

    return true;
}

bool
generic_hash_table_insert(generic_hash_table_t * ht, void const * key, void * value)
{
    if (ht == NULL || key == NULL || value == NULL)
    {
        return false;
    }
    size_t hash = ht->key_ops.hash(key);
    size_t idx = hash % ht->bucket_count;

    for (generic_hash_entry_t * e = ht->buckets[idx]; e != NULL; e = e->next)
    {
        if (ht->key_ops.equals(e->key, key))
        {
            return false;
        }
    }

    generic_hash_entry_t * new_entry = malloc(sizeof(*new_entry));
    if (new_entry == NULL)
    {
        return false;
    }

    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = ht->buckets[idx];
    ht->buckets[idx] = new_entry;
    ht->size++;

    if ((float)ht->size / (float)ht->bucket_count > ht->load_factor)
    {
        generic_hash_table_resize(ht, ht->bucket_count * 2);
    }

    return true;
}

void *
generic_hash_table_lookup(generic_hash_table_t const * ht, void const * key)
{
    if (ht == NULL || key == NULL)
    {
        return NULL;
    }

    size_t hash = ht->key_ops.hash(key);
    size_t idx = hash % ht->bucket_count;
    for (generic_hash_entry_t * e = ht->buckets[idx]; e != NULL; e = e->next)
    {
        if (ht->key_ops.equals(e->key, key))
        {
            return e->value;
        }
    }

    return NULL;
}

void *
generic_hash_table_remove(generic_hash_table_t * ht, void const * key)
{
    if (ht == NULL || key == NULL)
    {
        return NULL;
    }

    size_t idx = ht->key_ops.hash(key) % ht->bucket_count;
    generic_hash_entry_t * prev = NULL;

    for (generic_hash_entry_t * e = ht->buckets[idx]; e != NULL; e = e->next)
    {
        if (ht->key_ops.equals(e->key, key))
        {
            if (prev != NULL)
            {
                prev->next = e->next;
            }
            else
            {
                ht->buckets[idx] = e->next;
            }

            void * result = e->value;
            free(e);
            ht->size--;
            return result;
        }
        prev = e;
    }

    return NULL;
}

void
generic_hash_table_iterate(
    generic_hash_table_t const * ht, void (*callback)(void * value, void * user_data), void * user_data
)
{
    if (ht == NULL || callback == NULL)
    {
        return;
    }

    for (size_t i = 0; i < ht->bucket_count; ++i)
    {
        for (generic_hash_entry_t * e = ht->buckets[i]; e != NULL; e = e->next)
        {
            callback(e->value, user_data);
        }
    }
}