#pragma once

#include <easy_pc/easy_pc.h>
#include <stdbool.h>

// Forward declarations for generated code
bool is_typedef_name(epc_cpt_node_t * token, epc_parser_ctx_t * parse_ctx, void * parser_data);

extern epc_wrap_callbacks_t typedef_capture_callbacks;
extern epc_wrap_callbacks_t typedef_commit_callbacks;
