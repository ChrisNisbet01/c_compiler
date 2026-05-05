#pragma once
#include "c_grammar_ast.h"
#include "source_location.h"
#include <easy_pc/easy_pc.h>

/* Build the location tracker by walking the AST after it's created.
 * This must be called before IR generation.
 * 
 * - tracker: the tracker to populate
 * - ast_root: the root of the AST (should be TRANSLATION_UNIT)
 * - parse_ctx: used to calculate preprocessed_line from node offsets
 * - input_filename: the original input filename (for the initial entry)
 */
void build_location_tracker_from_ast(
    source_location_tracker_t * tracker,
    c_grammar_node_t * ast_root,
    epc_parser_ctx_t * parse_ctx,
    const char * input_filename
);
