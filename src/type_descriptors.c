#include "type_descriptors.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct TypeDescriptor_private TypeDescriptor_private;
struct TypeDescriptor_private
{
    TypeDescriptor public;
    struct TypeDescriptor_private * next;
};

typedef struct TypeDescriptors
{
    TypeDescriptor_private * head;
} TypeDescriptors;

TypeDescriptors *
type_descriptors_create_list(void)
{
    TypeDescriptors * list = calloc(1, sizeof(*list));

    return list;
}

void
type_descriptors_destroy_list(TypeDescriptors * list)
{
    if (list == NULL)
    {
        return;
    }
    TypeDescriptor_private * cur = list->head;
    while (cur != NULL)
    {
        TypeDescriptor_private * next = cur->next;
        free(cur);
        cur = next;
    }
    free(list);
}

static bool
descriptor_matches(TypeDescriptor const * a, type_descriptor_type_kind_t kind, LLVMTypeRef type)
{
    if (a == NULL || type == NULL)
    {
        return false;
    }
    bool matches = a->kind == kind && a->llvm_type == type;

    return matches;
}

TypeDescriptor const *
type_descriptors_get_or_create(
    TypeDescriptors * list, type_descriptor_type_kind_t kind, LLVMTypeRef type, TypeDescriptor const * pointee
)
{
    if (list == NULL || type == NULL)
    {
        return NULL;
    }
    TypeDescriptor_private * cur = list->head;
    while (cur != NULL)
    {
        if (descriptor_matches(&cur->public, kind, type))
        {
            return &cur->public;
        }
        cur = cur->next;
    }

    cur = calloc(1, sizeof(*cur));
    if (cur == NULL)
    {
        return NULL;
    }
    cur->public.kind = kind;
    cur->public.llvm_type = type;
    cur->public.pointee = pointee;
    cur->next = list->head;
    list->head = cur;

    return &cur->public;
}
