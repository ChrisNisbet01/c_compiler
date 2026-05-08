#include "labels.h"

#include <stdlib.h>
#include <string.h>

struct label_list_t
{
    LLVMContextRef context;
    LLVMBuilderRef builder;
    label_t ** label;
    size_t count;
    size_t capacity;
};

label_list_t *
labels_list_create(LLVMContextRef context, LLVMBuilderRef builder)
{
    if (context == NULL || builder == NULL)
    {
        return NULL;
    }

    label_list_t * labels = calloc(1, sizeof(*labels));
    if (labels == NULL)
    {
        return NULL;
    }

    labels->context = context;
    labels->builder = builder;
    labels->capacity = 16;
    labels->label = calloc(labels->capacity, sizeof(*labels->label));
    if (labels->label == NULL)
    {
        free(labels);
        return NULL;
    }

    return labels;
}

void
labels_list_destroy(label_list_t * labels)
{
    if (labels == NULL)
    {
        return;
    }
    for (size_t i = 0; i < labels->count; i++)
    {
        label_t * label = labels->label[i];

        free(label->name);
        free(label);
    }
    free(labels->label);
    free(labels);
}

// Label management functions
LLVMBasicBlockRef
labels_get_or_create_label(label_list_t * labels, char const * name)
{
    if (labels == NULL || name == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < labels->count; i++)
    {
        label_t * label = labels->label[i];
        if (label->name != NULL && strcmp(label->name, name) == 0)
        {
            return label->block;
        }
    }

    if (labels->count >= labels->capacity)
    {
        size_t new_cap = labels->capacity == 0 ? 16 : labels->capacity * 2;
        label_t ** new_labels = realloc(labels->label, new_cap * sizeof(*new_labels));
        if (new_labels == NULL)
        {
            return NULL;
        }
        labels->label = new_labels;
        labels->capacity = new_cap;
    }

    label_t * label = calloc(1, sizeof(*label));
    if (label == NULL)
    {
        return NULL;
    }

    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(labels->builder));
    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(labels->context, current_func, name);

    label->name = strdup(name);
    label->block = block;
    labels->label[labels->count] = label;
    labels->count++;

    return block;
}
