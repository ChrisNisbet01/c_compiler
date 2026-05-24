#include "ir_gen_error.h"

#include "debug.h"
#include "source_location.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Error and Warning Collection Implementation ---

void
ir_gen_error_collection_init(
    ir_gen_error_collection_t * collection,
    size_t max_errors,
    epc_parser_ctx_t * parse_ctx,
    char const * module_name,
    source_location_tracker_t * loc_tracker
)
{
    if (collection == NULL)
    {
        return;
    }

    collection->loc_tracker = loc_tracker;
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
    if (collection == NULL)
    {
        return;
    }
    if (collection->parse_ctx == NULL)
    {
        return;
    }
    if (collection->loc_tracker == NULL)
    {
        return;
    }
    /* Look up the mapping for this preprocessed line */
    epc_parser_input_view_t const * view = &node->source_data.view;
    source_location_entry_t const * entry = source_location_tracker_find(collection->loc_tracker, view->offset);

    if (entry == NULL)
    {
        return;
    }

    /* Get the preprocessed line and column */
    epc_parser_input_view_t pp_entry_view = entry->preprocessed_view;
    /* Calculate original line: entry's original_line + offset from marker */
    size_t original_line = entry->original_line + view->line_number - pp_entry_view.line_number - 1;

    fprintf(fp, "%s:%zu:%zu\n", entry->original_filename, original_line, view->column_number);

    /* Print "In file included from..." stack */
    source_location_tracker_t * tracker = collection->loc_tracker;
    if (tracker->stack_top > 1)
    {
        for (size_t i = 0; i < tracker->stack_top - 1; i++)
        {
            fprintf(
                fp,
                "In file included from %s:%zu:\n",
                tracker->include_stack[i].filename,
                tracker->include_stack[i].line
            );
        }
    }

    /* Print the line from preprocessed source */
    char * line_at_offset = epc_get_line_at_offset(collection->parse_ctx, view->offset);
    if (line_at_offset != NULL)
    {
        fprintf(fp, "%s\n", line_at_offset);
        fprintf(fp, "%*s^", (int)view->column_number - 1, "");

        size_t len = view->len;
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
