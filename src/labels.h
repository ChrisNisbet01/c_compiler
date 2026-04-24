#pragma once

#include <llvm-c/Core.h>

// --- Label Management ---
typedef struct label
{
    char * name;
    LLVMBasicBlockRef block;
} label_t;

typedef struct label_list_t label_list_t;

label_list_t * labels_list_create(LLVMContextRef context, LLVMBuilderRef builder);

LLVMBasicBlockRef labels_get_or_create_label(label_list_t * labels, char const * name);

void labels_list_destroy(label_list_t * labels);
