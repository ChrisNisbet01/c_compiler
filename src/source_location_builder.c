#include "source_location_builder.h"

#include "source_location.h"

#include <easy_pc/easy_pc.h>

void
build_location_tracker_from_ast(source_location_tracker_t * tracker, char const * input_filename)
{
    /* Add initial entry: preprocessed line 1 maps to input file line 1 */
    source_location_tracker_add_entry(tracker, 0, 1, input_filename);
}
