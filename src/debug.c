/**
 * @file debug.c
 * @brief Debug output utilities implementation.
 */

#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

static debug_level_t current_level = DEBUG_LEVEL_OFF;

void
debug_set_level(debug_level_t level)
{
    current_level = level;
}

debug_level_t
debug_get_level(void)
{
    return current_level;
}

bool
debug_is_enabled(debug_level_t level)
{
    return level >= current_level;
}

void
debug_info(char const * fmt, ...)
{
    if (!debug_is_enabled(DEBUG_LEVEL_INFO))
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "INFO: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void
debug_warning(char const * fmt, ...)
{
    if (!debug_is_enabled(DEBUG_LEVEL_WARNING))
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "WARNING: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void
debug_error(char const * fmt, ...)
{
    if (!debug_is_enabled(DEBUG_LEVEL_ERROR))
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "DEBUG: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}
