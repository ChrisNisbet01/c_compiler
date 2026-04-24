#pragma once

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>

#include "type_descriptors.h"

typedef struct Builtins Builtins;

typedef struct BuiltinEntry BuiltinEntry;

Builtins * builtins_create(TypeDescriptors * type_descriptors);

void builtins_destroy(Builtins * list);

bool builtins_create_builtin(Builtins * list, char const * name, LLVMTypeRef type);

TypeDescriptor const * builtins_get(Builtins * list, char const * name);