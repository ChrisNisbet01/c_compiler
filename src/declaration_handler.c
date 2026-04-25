#include "declaration_handler.h"

#include "debug.h"

#include <stdio.h>

static void
dump_type_specifier(TypeSpecifier spec)
{
    fprintf(
        stderr,
        "Specifiers: unsigned: %d, signed: %d, long: %u, int %d, void %d, bool %d, short %d, char %d, float %d, double "
        "%d\n",
        spec.is_unsigned,
        spec.is_signed,
        spec.long_count,
        spec.is_int,
        spec.is_void,
        spec.is_bool,
        spec.is_short,
        spec.is_char,
        spec.is_float,
        spec.is_double
    );
}

static bool
type_specifier_is_valid(TypeSpecifier const spec)
{
    if (spec.long_count > 2)
    {
        return false;
    }
    if (spec.is_signed && spec.is_unsigned)
    {
        return false;
    }
    if (spec.is_double)
    {
        if (spec.is_signed || spec.is_unsigned || spec.is_void || spec.is_float || spec.is_bool || spec.is_char
            || spec.is_short || spec.is_int || spec.is_float)
        {
            return false;
        }
        if (spec.long_count > 1)
        {
            return false;
        }
    }
    if (spec.is_float)
    {
        if (spec.long_count > 0 || spec.is_signed || spec.is_unsigned || spec.is_void || spec.is_float || spec.is_bool
            || spec.is_char || spec.is_short || spec.is_int)
        {
            return false;
        }
    }
    if (spec.is_char)
    {
        if (spec.long_count > 0 || spec.is_short || spec.is_int || spec.is_float || spec.is_double || spec.is_void
            || spec.is_bool)
        {
            return false;
        }
    }
    if (spec.is_short)
    {
        if (spec.long_count > 0 || spec.is_char || spec.is_int || spec.is_float || spec.is_double || spec.is_void
            || spec.is_bool)
        {
            return false;
        }
    }

    return true;
}

static void
type_specifier_process_specifier(TypeSpecifier * spec, char const * specifier)
{
    if (specifier == NULL)
    {
        return;
    }

    if (strcmp(specifier, "unsigned") == 0)
    {
        spec->is_unsigned = true;
        spec->is_int = true;
    }
    else if (strcmp(specifier, "int") == 0)
    {
        spec->is_int = true;
    }
    else if (strcmp(specifier, "long") == 0)
    {
        spec->long_count++;
    }
    else if (strcmp(specifier, "short") == 0)
    {
        spec->is_short = true;
    }
    else if (strcmp(specifier, "char") == 0)
    {
        spec->is_char = true;
    }
    else if (strcmp(specifier, "float") == 0)
    {
        spec->is_float = true;
    }
    else if (strcmp(specifier, "double") == 0)
    {
        spec->is_double = true;
    }
    else if (strcmp(specifier, "bool") == 0 || strcmp(specifier, "_Bool") == 0)
    {
        spec->is_bool = true;
    }
    else if (strcmp(specifier, "void") == 0)
    {
        spec->is_void = true;
    }
}

static TypeSpecifier
build_type_specifiers(c_grammar_node_t const * spec_list)
{
    TypeSpecifier spec = {0};

    debug_info("%s count %u", __func__, spec_list->list.count);
    for (size_t i = 0; i < spec_list->list.count; ++i)
    {
        c_grammar_node_t * child = spec_list->list.children[i];
        type_specifier_process_name(&spec, child->text);
    }

    debug_info("%s", __func__);
    dump_type_specifier(spec);

    return spec;
}

static TypeQualifier
build_type_qualifiers(c_grammar_node_t const * qual_list)
{
    TypeQualifier quals = {0};

    if (qual_list == NULL)
    {
        return quals;
    }

    for (size_t i = 0; i < qual_list->list.count; ++i)
    {
        c_grammar_node_t * child = qual_list->list.children[i];
        if (child->text == NULL)
        {
            continue;
        }

        if (strcmp(child->text, "const") == 0)
        {
            quals.is_const = true;
        }
        else if (strcmp(child->text, "volatile") == 0)
        {
            quals.is_volatile = true;
        }
    }

    return quals;
}

TypeDescriptor const *
resolve_type_from_ast(ir_generator_ctx_t * ctx, c_grammar_node_t const * node)
{
    if (node == NULL || node->type != AST_NODE_TYPEDEF_DECLARATION)
    {
        return NULL;
    }

    // unsigned int i, j;
    // ^^^^^^^^^^^^
    c_grammar_node_t const * decl_specs = node->declaration.declaration_specifiers;
    c_grammar_node_t const * type_specifiers = decl_specs->decl_specifiers.type_specifiers;
    c_grammar_node_t const * type_qualifiers = decl_specs->decl_specifiers.type_qualifiers;
    TypeQualifier quals = build_type_qualifiers(type_qualifiers);

    bool is_struct_or_union_or_enum_or_typedef = false;
    bool is_native_type = true;
    if (type_specifiers->list.count == 1)
    {
        c_grammar_node_t const * specifier = type_specifiers->list.children[0];
        if (specifier->list.count == 1)
        {
            c_grammar_node_t const * child = specifier->list.children[0];
            if (child->type == AST_NODE_STRUCT_TYPE_REF || child->type == AST_NODE_UNION_TYPE_REF
                || child->type == AST_NODE_ENUM_TYPE_REF)
            {
                is_struct_or_union_or_enum_or_typedef = true;
            }
        }
    }
    for (size_t i = 0; i < type_specifiers->list.count; ++i)
    {
        c_grammar_node_t const * specifier = type_specifiers->list.children[i];
        if (specifier->text == NULL)
        {
            is_native_type = false;
            break;
        }
    }
    if (!is_struct_or_union_or_enum_or_typedef && !is_native_type)
    {
        ir_gen_error(&ctx->errors, "Neither struct/union/enum/typedef nor native type specified in declaration");
        return NULL;
    }
    TypeDescriptor const * current = NULL;
    if (is_native_type)
    {
        TypeSpecifier specs = build_type_specifiers(type_specifiers);

        if (!type_specifier_is_valid(specs))
        {
            ir_gen_error(&ctx->errors, "Invalid combination of type specifiers in declaration");
            if (debug_get_level() >= DEBUG_LEVEL_ERROR)
            {
                dump_type_specifier(specs);
            }
            return NULL;
        }
        /* Register the type. */
        current = get_or_create_builtin_type(ctx->type_descriptors, specs, quals);
    }
    else
    {
        /* Must be struct/union/enum/typedef. */
        // TODO.
        debug_error("need struct/union/enum/typedef handling in resolve_type_from_ast");
        return NULL;
    }

    if (current == NULL)
    {
        ir_gen_error(&ctx->errors, "Failed to resolve base type from declaration");
        return NULL;
    }

    // unsigned int i, j;
    //              ^^^^
    c_grammar_node_t const * init_decl_list = node->declaration.init_declarator_list;
    if (init_decl_list == NULL)
    {
        return current;
    }
    for (size_t i = 0; i < init_decl_list->list.count; ++i)
    {
        c_grammar_node_t const * init_decl = init_decl_list->list.children[i];
        c_grammar_node_t const * declarator = init_decl->init_declarator.declarator;
        c_grammar_node_t const * pointer_list = declarator->declarator.pointer_list;
        c_grammar_node_t const * direct_declarator = declarator->declarator.direct_declarator;

        // TODO: Declare the variable with the resolved type, and handle pointer levels and direct declarator suffixes
        // (arrays/functions)

        // 3. Handle Pointer levels (if applicable in this node)
        // C handles pointers from right to left in the AST declarator

        for (size_t i = pointer_list->list.count; i > 0; i--)
        {
            c_grammar_node_t const * pointer = pointer_list->list.children[i - 1];
            // If node is 'int * const', extract 'const' for the pointer level
            c_grammar_node_t const * pointer_qual_list = pointer->list.children[0];
            TypeQualifier ptr_quals = build_type_qualifiers(pointer_qual_list);
            current = get_or_create_pointer_type(ctx, current, ptr_quals);
        }
    }

    return current;
}
