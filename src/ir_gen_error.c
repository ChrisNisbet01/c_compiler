#include "ir_gen_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// --- Error and Warning Collection Implementation ---

void
ir_gen_error_collection_init(ir_gen_error_collection_t * collection, size_t max_errors)
{
    if (collection == NULL)
    {
        return;
    }
    collection->errors = NULL;
    collection->count = 0;
    collection->capacity = 0;
    collection->max_errors = max_errors;
    collection->fatal = false;
}

void
ir_gen_error_collection_free(ir_gen_error_collection_t * collection)
{
    if (collection == NULL)
    {
        return;
    }
    for (size_t i = 0; i < collection->count; ++i)
    {
        free(collection->errors[i].message);
    }
    free(collection->errors);
    collection->errors = NULL;
    collection->count = 0;
    collection->capacity = 0;
    collection->fatal = false;
}

bool
ir_gen_error(ir_gen_error_collection_t * collection, char const * fmt, ...)
{
    if (collection == NULL || collection->fatal)
    {
        return collection != NULL && collection->fatal;
    }

    va_list args;
    va_start(args, fmt);
    char * message = NULL;
    int len = vsnprintf(NULL, 0, fmt, args);
    if (len >= 0)
    {
        message = malloc(len + 1);
        if (message != NULL)
        {
            va_start(args, fmt);
            vsnprintf(message, len + 1, fmt, args);
            va_end(args);
        }
    }
    va_end(args);

    // Print immediately
    fprintf(stderr, "Error: %s\n", message ? message : "(unknown)");
    free(message);

    // Don't add to collection if already at/over limit
    if (collection->count >= collection->max_errors)
    {
        collection->fatal = true;
        return true;
    }

    // Add to collection
    if (collection->count >= collection->capacity)
    {
        size_t new_cap = collection->capacity == 0 ? 4 : collection->capacity * 2;
        ir_gen_error_t * new_errors = realloc(collection->errors, new_cap * sizeof(*new_errors));
        if (new_errors == NULL)
        {
            collection->fatal = true;
            return true;
        }
        collection->errors = new_errors;
        collection->capacity = new_cap;
    }

    // Re-print to capture the message for the collection
    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args);
    if (len >= 0)
    {
        message = malloc(len + 1);
        if (message != NULL)
        {
            va_start(args, fmt);
            vsnprintf(message, len + 1, fmt, args);
            va_end(args);
            collection->errors[collection->count].message = message;
            collection->count++;
        }
    }
    va_end(args);

    // Check if we hit the limit
    if (collection->count >= collection->max_errors)
    {
        collection->fatal = true;
        fprintf(stderr, "Error: Too many errors, stopping.\n");
    }

    return collection->fatal;
}

void
ir_gen_warning(ir_gen_error_collection_t * collection, char const * fmt, ...)
{
    if (collection == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    char * message = NULL;
    int len = vsnprintf(NULL, 0, fmt, args);
    if (len >= 0)
    {
        message = malloc(len + 1);
        if (message != NULL)
        {
            va_start(args, fmt);
            vsnprintf(message, len + 1, fmt, args);
            va_end(args);
        }
    }
    va_end(args);

    // Print warning (doesn't count toward error limit)
    fprintf(stderr, "Warning: %s\n", message ? message : "(unknown)");
    free(message);
}

bool
ir_gen_has_errors(ir_gen_error_collection_t * collection)
{
    return collection != NULL && collection->count > 0;
}

bool
ir_gen_is_fatal(ir_gen_error_collection_t * collection)
{
    return collection != NULL && collection->fatal;
}
