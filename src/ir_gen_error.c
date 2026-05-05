#include "ir_gen_error.h"

#include "source_location.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Error and Warning Collection Implementation ---

void
ir_gen_error_collection_init(
    ir_gen_error_collection_t * collection, size_t max_errors, epc_parser_ctx_t * parse_ctx, char const * module_name
)
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
    collection->parse_ctx = parse_ctx;
    collection->module_name = strdup(module_name);
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

static void
print_error_location(FILE * fp, ir_gen_error_collection_t * collection, c_grammar_node_t const * node)
{
    /* Get the preprocessed line and column */
    epc_line_col_t pp_lc = epc_calculate_line_and_column(collection->parse_ctx, node->source_data.offset);

    if (collection->loc_tracker != NULL)
    {
        /* Look up the mapping for this preprocessed line */
        source_location_entry_t const * entry = source_location_tracker_find(collection->loc_tracker, pp_lc.line);

        if (entry != NULL)
        {
            /* Calculate original line: entry's original_line + offset from marker */
            size_t original_line = entry->original_line + (pp_lc.line - entry->preprocessed_line - 1);

            fprintf(fp, "%s:%zu:%zu\n", entry->original_filename, original_line, pp_lc.col);

            /* Print "In file included from..." stack */
            source_location_tracker_t * tracker = collection->loc_tracker;
            for (size_t i = 0; i < tracker->stack_top; i++)
            {
                fprintf(
                    fp,
                    "In file included from %s:%zu:\n",
                    tracker->include_stack[i].filename,
                    tracker->include_stack[i].line
                );
            }

            /* Print the line from preprocessed source */
            char * line_at_offset = epc_get_line_at_offset(collection->parse_ctx, node->source_data.offset);
            if (line_at_offset != NULL)
            {
                fprintf(fp, "%s\n", line_at_offset);
                fprintf(fp, "%*s^", (int)pp_lc.col - 1, "");

                size_t len = node->source_data.len;
                size_t line_len = strlen(line_at_offset);
                if (len > line_len)
                {
                    len = line_len;
                }
                for (size_t i = 0; i < len - 1; i++)
                {
                    fprintf(fp, "~");
                }
                fprintf(fp, "\n");
                free(line_at_offset);
            }
            return;
        }
        else
        {
            fprintf(stderr, "no tracker entry found\n");
        }
    }

    /* Fallback: no tracker or no mapping found */
    fprintf(fp, "%s:%zu:%zu\n", collection->module_name, pp_lc.line, pp_lc.col);

    char * line_at_offset = epc_get_line_at_offset(collection->parse_ctx, node->source_data.offset);
    if (line_at_offset == NULL)
    {
        return;
    }

    fprintf(fp, "%s\n", line_at_offset);
    fprintf(fp, "%*s^", (int)pp_lc.col - 1, "");
    size_t len = node->source_data.len;
    size_t line_len = strlen(line_at_offset);
    if (len > line_len)
    {
        len = line_len;
    }
    for (size_t i = 0; i < len - 1; i++)
    {
        fprintf(fp, "~");
    }
    fprintf(fp, "\n");

    free(line_at_offset);
}

bool
ir_gen_error(ir_gen_error_collection_t * collection, c_grammar_node_t const * node, char const * fmt, ...)
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

    print_error_location(stderr, collection, node);

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
            ir_gen_error_t * error = &collection->errors[collection->count];
            error->node = node;
            va_start(args, fmt);
            vsnprintf(message, len + 1, fmt, args);
            va_end(args);
            error->message = message;
            error->node = node;
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
ir_gen_warning(ir_gen_error_collection_t * collection, c_grammar_node_t const * node, char const * fmt, ...)
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

    print_error_location(stderr, collection, node);
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
