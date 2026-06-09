#include "ir_gen_error.h"

#include "debug.h"

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
    char const * module_name
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
    collection->external_declarations = NULL;
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
    if (collection->external_declarations == NULL)
    {
        return;
    }

    epc_parser_input_view_t const * view = &node->source_data.view;
    size_t error_offset = view->offset;

    /* Default fallback: use module name and preprocessed line directly */
    char const * original_filename = collection->module_name;
    size_t original_line = view->line_number;

    /* Scan external declarations for the nearest PreprocessorLineMarker before this node */
    c_grammar_node_t const * ext_decls = collection->external_declarations;

    /* Build the include stack on the fly */
    typedef struct
    {
        char const * filename;
        size_t line;
    } inc_entry_t;

    inc_entry_t include_stack[256];
    include_stack[0].filename = collection->module_name;
    include_stack[0].line = 1;
    size_t stack_top = 1;

    c_grammar_node_t const * nearest_marker = NULL;

    for (size_t i = 0; i < ext_decls->list.count; i++)
    {
        c_grammar_node_t const * ext_decl = ext_decls->list.children[i];
        if (ext_decl->type != AST_NODE_EXTERNAL_DECLARATION)
        {
            continue;
        }
        if (ext_decl->external_declaration.preprocessor_directive == NULL)
        {
            continue;
        }

        c_grammar_node_t const * directive = ext_decl->external_declaration.preprocessor_directive;
        if (directive->type != AST_NODE_PREPROCESSOR_LINE_MARKER)
        {
            continue;
        }

        /* Check if this line marker is before our error */
        if (directive->source_data.view.offset > error_offset)
        {
            break;
        }

        /* Record as the nearest marker so far */
        nearest_marker = directive;

        /* Update include stack */
        ast_node_preprocessor_line_marker_t const * marker = &directive->line_marker;
        for (size_t f = 0; f < marker->flags_count; f++)
        {
            if (marker->flags[f] == 1 && stack_top < 256)
            {
                include_stack[stack_top].filename = marker->filename;
                include_stack[stack_top].line = marker->line_number;
                stack_top++;
            }
            else if (marker->flags[f] == 2 && stack_top > 1)
            {
                stack_top--;
            }
        }
    }

    if (nearest_marker != NULL)
    {
        ast_node_preprocessor_line_marker_t const * marker = &nearest_marker->line_marker;
        original_filename = marker->filename;
        original_line = marker->line_number + view->line_number - nearest_marker->source_data.view.line_number - 1;
    }

    fprintf(fp, "%s:%zu:%zu\n", original_filename, original_line, view->column_number);

    /* Print "In file included from..." stack */
    if (stack_top > 1)
    {
        for (size_t i = 0; i < stack_top - 1; i++)
        {
            fprintf(
                fp,
                "In file included from %s:%zu:\n",
                include_stack[i].filename,
                include_stack[i].line
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
