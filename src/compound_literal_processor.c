#include "compound_literal_processor.h"

#include "generator_lists.h"
#include "type_utils.h"

TypedValue
process_compound_literal(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    // CompoundLiteral: (type){initializer-list}
    // e.g., (struct Pos){.x = 1, .y = 2} or (union Data){.x = 1}
    // Structure: TypeName + InitializerList
    // First child is TypeName, second is InitializerList
    c_grammar_node_t const * type_name_node = node->compound_literal.type_name;
    c_grammar_node_t const * init_list_node = node->compound_literal.initializer_list;

    /* Extract type name - check struct/union keyword first, then typedef */
    char const * type_name = NULL;
    bool is_typedef = false;

    if (type_name_node->type == AST_NODE_TYPE_NAME)
    {
        c_grammar_node_t const * qualifier_list = type_name_node->type_name.specifier_qualifier_list;

        for (size_t i = 0; i < qualifier_list->list.count && !type_name; ++i)
        {
            c_grammar_node_t const * child = qualifier_list->list.children[i];

            if (child->type == AST_NODE_TYPEDEF_SPECIFIER)
            {
                /* Try typedef */
                type_name = extract_typedef_name(child);
                if (type_name != NULL)
                {
                    is_typedef = true;
                }
            }
            else if (child->type == AST_NODE_TYPEDEF_SPECIFIER_QUALIFIER)
            {
                c_grammar_node_t const * inner = child->typedef_specifier_qualifier.typedef_specifier;
                type_name = extract_typedef_name(inner);
                if (type_name != NULL)
                {
                    is_typedef = true;
                }
            }
            else
            {
                /* Try struct/union keyword */
                type_name = extract_struct_or_union_or_enum_tag(child);
            }
        }
    }

    if (type_name == NULL)
    {
        debug_error("Could not extract type name from compound literal");
        return NullTypedValue;
    }

    /* Look up the type - struct list or typedef list */
    TypeDescriptor const * compound_type_desc = is_typedef ? generator_find_typedef_type_descriptor(ctx, type_name)
                                                           : generator_find_type_descriptor_by_tag(ctx, type_name);

    debug_info("%s: compound_type_desc: %p", __func__, compound_type_desc);
    if (compound_type_desc == NULL || compound_type_desc->llvm_type == NULL)
    {
        debug_error("Unknown type '%s' in compound literal", type_name);
        return NullTypedValue;
    }

    // Create a temporary local variable (alloca) for the compound literal
    LLVMValueRef alloca_inst = LLVMBuildAlloca(ctx->builder, compound_type_desc->llvm_type, "compound_literal_tmp");
    if (alloca_inst == NULL)
    {
        debug_error("Failed to allocate compound literal");
        return NullTypedValue;
    }

    // Initialize using the initializer list
    if (init_list_node->type == AST_NODE_INITIALIZER_LIST)
    {
        process_initializer_list(ctx, alloca_inst, compound_type_desc, init_list_node, NULL);
    }

    return create_typed_value(alloca_inst, compound_type_desc, true);
}
