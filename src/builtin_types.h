#pragma once

#include "type_descriptors.h"

#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>

void builtins_init(builtin_types * types, LLVMContextRef * context, TypeDescriptors * type_descriptors);
