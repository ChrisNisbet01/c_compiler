#include "hash_table.h"

#include "debug.h"

#include <stdlib.h>
#include <string.h>

/* Simple djb2 hash function */
static size_t
hash_djb2(char const * str)
{
    size_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + (size_t)c; /* hash * 33 + c */
    return hash;
}

typedef struct hash_entry_t
{
    char * key;
    struct hash_entry_t * next;
} hash_entry_t;

struct hash_table_t
{
    size_t bucket_count;
    size_t size;             /* number of stored keys */
    hash_entry_t ** buckets; /* array of bucket pointers */
    float load_factor;       /* threshold to trigger resize */
};

static bool hash_table_resize(hash_table_t * ht, size_t new_bucket_count);

hash_table_t *
hash_table_create(size_t initial_bucket_count)
{
    if (initial_bucket_count == 0)
        initial_bucket_count = 16;
    hash_table_t * ht = calloc(1, sizeof(*ht));
    if (!ht)
        return NULL;
    ht->bucket_count = initial_bucket_count;
    ht->load_factor = 0.75f;
    ht->buckets = calloc(ht->bucket_count, sizeof(*ht->buckets));
    if (!ht->buckets)
    {
        free(ht);
        return NULL;
    }
    return ht;
}

static void
hash_table_free_entries(hash_entry_t * entry)
{
    while (entry)
    {
        hash_entry_t * next = entry->next;
        free(entry->key);
        free(entry);
        entry = next;
    }
}

void
hash_table_free(hash_table_t * ht)
{
    if (!ht)
        return;
    for (size_t i = 0; i < ht->bucket_count; ++i)
    {
        hash_table_free_entries(ht->buckets[i]);
    }
    free(ht->buckets);
    free(ht);
}

static bool
hash_table_insert_internal(hash_table_t * ht, char const * key, bool duplicate)
{
    size_t idx = hash_djb2(key) % ht->bucket_count;
    for (hash_entry_t * e = ht->buckets[idx]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
        {
            return false; // already present
        }
    }
    hash_entry_t * new_entry = malloc(sizeof(*new_entry));
    if (!new_entry)
    {
        debug_error("hash_table: allocation failed for entry");
        return false;
    }
    if (duplicate)
    {
        new_entry->key = strdup(key);
        if (!new_entry->key)
        {
            free(new_entry);
            debug_error("hash_table: strdup failed");
            return false;
        }
    }
    else
    {
        new_entry->key = (char *)key; // assumes caller already allocated
    }
    new_entry->next = ht->buckets[idx];
    ht->buckets[idx] = new_entry;
    ht->size++;

    /* Resize if load factor exceeded */
    if ((float)ht->size / (float)ht->bucket_count > ht->load_factor)
    {
        hash_table_resize(ht, ht->bucket_count * 2);
    }
    return true;
}

char *
hash_table_insert(hash_table_t * ht, char const * key)
{
    if (!ht || !key)
        return NULL;
    /* Insert and duplicate the key; on success return pointer to stored copy */
    bool ok = hash_table_insert_internal(ht, key, true);
    if (!ok)
        return NULL;
    /* Find the entry we just inserted to get its stored pointer */
    size_t idx = hash_djb2(key) % ht->bucket_count;
    for (hash_entry_t * e = ht->buckets[idx]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
        {
            return e->key;
        }
    }
    return NULL; /* should not happen */
}

bool
hash_table_remove(hash_table_t * ht, char const * key)
{
    if (!ht || !key)
        return false;
    size_t idx = hash_djb2(key) % ht->bucket_count;
    hash_entry_t * prev = NULL;
    for (hash_entry_t * e = ht->buckets[idx]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
        {
            if (prev)
                prev->next = e->next;
            else
                ht->buckets[idx] = e->next;
            free(e->key);
            free(e);
            ht->size--;
            return true;
        }
        prev = e;
    }
    return false;
}

bool
hash_table_contains(hash_table_t const * ht, char const * key)
{
    if (!ht || !key)
        return false;
    size_t idx = hash_djb2(key) % ht->bucket_count;
    for (hash_entry_t * e = ht->buckets[idx]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
            return true;
    }
    return false;
}

size_t
hash_table_size(hash_table_t const * ht)
{
    return ht ? ht->size : 0;
}

static bool
hash_table_resize(hash_table_t * ht, size_t new_bucket_count)
{
    if (!ht)
        return false;
    hash_entry_t ** new_buckets = calloc(new_bucket_count, sizeof(*new_buckets));
    if (!new_buckets)
    {
        debug_error("hash_table: resize allocation failed");
        return false;
    }
    /* Rehash all entries */
    for (size_t i = 0; i < ht->bucket_count; ++i)
    {
        hash_entry_t * e = ht->buckets[i];
        while (e)
        {
            hash_entry_t * next = e->next;
            size_t idx = hash_djb2(e->key) % new_bucket_count;
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
