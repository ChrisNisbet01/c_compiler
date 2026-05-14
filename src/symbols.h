#pragma once

#include "typed_value.h"

typedef struct symbol
{
    char const * name;
    TypedValue value;
} symbol_t;
