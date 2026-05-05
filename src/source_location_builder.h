#pragma once
#include "c_grammar_ast.h"
#include "source_location.h"

#include <easy_pc/easy_pc.h>

/* Build the location tracker after it's created.
 * This must be called before IR generation.
 *
 * - tracker: the tracker to populate
 * - input_filename: the original input filename (for the initial entry)
 */
void build_location_tracker_from_ast(source_location_tracker_t * tracker, char const * input_filename);
