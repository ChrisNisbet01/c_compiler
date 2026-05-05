#include "source_location_builder.h"
#include "source_location.h"
#include <easy_pc/easy_pc.h>

static void
walk_node(
    source_location_tracker_t * tracker,
    c_grammar_node_t * node,
    epc_parser_ctx_t * parse_ctx
)
{
    if (node == NULL)
    {
        return;
    }
    
    /* Process line marker nodes */
    if (node->type == AST_NODE_PREPROCESSOR_LINE_MARKER)
    {
        /* Calculate the preprocessed line where this marker appears */
        epc_line_col_t marker_pos = epc_calculate_line_and_column(
            parse_ctx, node->source_data.offset
        );
        
        /* The marker applies to the NEXT line, so effective line is marker_pos.line + 1 */
        size_t effective_preprocessed_line = marker_pos.line + 1;
        size_t original_line = node->line_marker.line_number;
        
        /* Add entry to tracker */
        printf("DEBUG: Adding marker: pp_line=%zu, orig_line=%zu, file=%s\n", 
               effective_preprocessed_line, original_line, node->line_marker.filename);
        source_location_tracker_add_entry(
            tracker,
            effective_preprocessed_line,
            original_line,
            node->line_marker.filename
        );
        
        /* Handle include stack via flags */
        for (size_t i = 0; i < node->line_marker.flags_count; i++)
        {
            size_t flag = node->line_marker.flags[i];
            if (flag == 1)
            {
                /* Entering a new file */
                source_location_tracker_push_include(
                    tracker,
                    node->line_marker.filename,
                    original_line
                );
            }
            else if (flag == 2)
            {
                /* Returning to previous file */
                source_location_tracker_pop_include(tracker);
            }
        }
        
        return;  /* Don't recurse into children of line marker */
    }
    
    /* Recurse into list children */
    for (size_t i = 0; i < node->list.count; i++)
    {
        walk_node(tracker, node->list.children[i], parse_ctx);
    }
}

void
build_location_tracker_from_ast(
    source_location_tracker_t * tracker,
    c_grammar_node_t * ast_root,
    epc_parser_ctx_t * parse_ctx,
    const char * input_filename
)
{
    /* Add initial entry: preprocessed line 1 maps to input file line 1 */
    source_location_tracker_add_entry(tracker, 1, 1, input_filename);
    
    /* Walk the AST to find all line markers */
    walk_node(tracker, ast_root, parse_ctx);
}
