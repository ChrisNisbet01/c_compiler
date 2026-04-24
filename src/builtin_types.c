#include "builtin_types.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct BuiltinEntry
{
    char * name;
    TypeDescriptor const * descriptor;
    struct BuiltinEntry * next;
} BuiltinEntry;

typedef struct Builtins
{
    TypeDescriptors * type_descriptors;
    BuiltinEntry * head;
} Builtins;

Builtins *
builtins_create(TypeDescriptors * type_descriptors)
{
    if (type_descriptors == NULL)
    {
        return NULL;
    }
    Builtins * list = calloc(1, sizeof(*list));
    if (list == NULL)
    {
        return NULL;
    }
    list->type_descriptors = type_descriptors;

    return list;
}

void
builtins_destroy(Builtins * list)
{
    if (list == NULL)
    {
        return;
    }
    BuiltinEntry * cur = list->head;
    while (cur != NULL)
    {
        BuiltinEntry * next = cur->next;
        free(cur->name);
        free(cur);
        cur = next;
    }
    free(list);
}

static bool
builtin_entry_matches(BuiltinEntry const * entry, char const * name)
{
    if (entry == NULL || name == NULL)
    {
        return false;
    }
    return strcmp(entry->name, name) == 0;
}

static bool
builtin_entry_type_matches(BuiltinEntry const * entry, LLVMTypeRef type)
{
    if (entry == NULL || entry->descriptor == NULL)
    {
        return false;
    }
    return entry->descriptor->llvm_type == type;
}

bool
builtins_create_builtin(Builtins * list, char const * name, LLVMTypeRef type)
{
    if (list == NULL || name == NULL || type == NULL)
    {
        return false;
    }
    BuiltinEntry * cur = list->head;
    while (cur != NULL)
    {
        if (builtin_entry_matches(cur, name))
        {
            if (builtin_entry_type_matches(cur, type))
            {
                return true;
            }
            return false;
        }
        cur = cur->next;
    }

    TypeDescriptor const * descriptor = type_descriptors_get_or_create(
        list->type_descriptors, NCC_TYPE_KIND_BUILTIN, type, NULL
    );
    if (descriptor == NULL)
    {
        return false;
    }

    char * name_copy = strdup(name);
    if (name_copy == NULL)
    {
        return false;
    }

    BuiltinEntry * entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        free(name_copy);
        return false;
    }
    entry->name = name_copy;
    entry->descriptor = descriptor;
    entry->next = list->head;
    list->head = entry;

    return true;
}

TypeDescriptor const *
builtins_get(Builtins * list, char const * name)
{
    if (list == NULL || name == NULL)
    {
        return NULL;
    }
    BuiltinEntry * cur = list->head;
    while (cur != NULL)
    {
        if (builtin_entry_matches(cur, name))
        {
            return cur->descriptor;
        }
        cur = cur->next;
    }
    return NULL;
}