#pragma once

#include "typed_value.h"

typedef struct symbol
{
    char const * name;
    char const * tag_name; /* e.g. struct <tag> {...}; */
    TypedValue value;
} symbol_t;
