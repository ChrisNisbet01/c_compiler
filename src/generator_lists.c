#include "generator_lists.h"

#include "scope.h"

#include <string.h>

// --- Scope push/pop ---

void
generator_scope_push(ir_generator_ctx_t * ctx)
{
    if (!ctx)
        return;

    scope_t * new_scope = scope_create(ctx->current_scope, ctx->context, ctx->builder);
    if (new_scope)
    {
        ctx->current_scope = new_scope;
    }
}

void
generator_scope_pop(ir_generator_ctx_t * ctx)
{
    if (ctx == NULL || ctx->current_scope == NULL)
    {
        return;
    }

    scope_t * old_scope = ctx->current_scope;
    ctx->current_scope = old_scope->parent;
    old_scope->parent = NULL; // Detach old scope from context before freeing
    scope_free(old_scope);
}

void
generator_add_typedef_forward_decl(
    ir_generator_ctx_t * ctx, char const * typedef_name, char const * tag, type_kind_t kind
)
{
    debug_info("%s: name: %s, tag: %s", __func__, typedef_name, tag);
    scope_typedef_entry_t entry = {
        .name = strdup(typedef_name),
        .tag = strdup(tag),
        .kind = kind,
    };

    /* FIXME: Need to add an 'incomplete' type into the registry, and fill it in once the typed type is defined. */
    switch (kind)
    {
    case TYPE_KIND_STRUCT:
    case TYPE_KIND_UNION:
        debug_error("TODO: Support required for struct/union typedef forward declarations");
        break;

    case TYPE_KIND_ENUM:
    {
        TypeDescriptor const * enum_desc = type_descriptor_get_int32_type(ctx->type_descriptors, false);
        entry.type_desc = enum_desc;
        type_info_t info = {
            .tag = strdup(tag),
            .type_desc = enum_desc,
            .kind = kind,
        };
        scope_add_tagged_type(ctx->current_scope, info);
        break;
    }
    case TYPE_KIND_UNTAGGED_STRUCT:
    case TYPE_KIND_UNTAGGED_UNION:
    case TYPE_KIND_UNTAGGED_ENUM:
    case TYPE_KIND_BUILTIN:
        break;
    }

    scope_add_typedef_entry(ctx->current_scope, entry);
}

void
generator_add_tagged_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value, char const * tag)
{
    if (ctx == NULL)
    {
        return;
    }

    scope_add_symbol_with_tag(ctx->current_scope, name, value, tag);
}

void
generator_add_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value)
{
    debug_info(
        "generator_add_symbol: '%s' storing type_info=%p, const=%d",
        name,
        (void *)value.type_info,
        value.type_info->qualifiers.is_const
    );
    generator_add_tagged_symbol(ctx, name, value, NULL);
}

symbol_t const *
generator_lookup_symbol_entry(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_find_symbol_entry(ctx->current_scope, name);
}

bool
generator_lookup_symbol_value(ir_generator_ctx_t * ctx, char const * name, TypedValue * out_symbol)
{
    // TODO: Refactor so that it returns TypedValue directly.
    if (ctx == NULL)
    {
        if (out_symbol != NULL)
        {
            *out_symbol = NullTypedValue;
        }
        return false;
    }

    symbol_t const * symbol = generator_lookup_symbol_entry(ctx, name);

    if (symbol == NULL)
    {
        if (out_symbol != NULL)
        {
            *out_symbol = NullTypedValue;
        }
        return false;
    }

    if (out_symbol != NULL)
    {
        *out_symbol = symbol->value;
    }

    return true;
}

char const *
generator_lookup_symbol_tag_name(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    symbol_t const * symbol = generator_lookup_symbol_entry(ctx, name);

    if (symbol == NULL)
    {
        return NULL;
    }

    return symbol->tag_name;
}

LLVMBasicBlockRef
generator_get_or_create_label(ir_generator_ctx_t * ctx, char const * label_name)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_get_or_create_label(ctx->current_scope, label_name);
}

scope_typedef_entry_t *
generator_lookup_typedef_entry_by_name(ir_generator_ctx_t * ctx, char const * name)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_lookup_typedef_entry_by_name(ctx->current_scope, name);
}

type_info_t const *
generator_add_tagged_type(ir_generator_ctx_t * ctx, type_info_t info)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_add_tagged_type(ctx->current_scope, info);
}

type_info_t const *
generator_add_untagged_type(ir_generator_ctx_t * ctx, type_info_t info, int * untagged_index)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_add_untagged_type(ctx->current_scope, info, untagged_index);
}

scope_t *
generator_scope_create(ir_generator_ctx_t * ctx)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_create(NULL, ctx->context, ctx->builder);
}

void
generator_add_typedef_entry(ir_generator_ctx_t * ctx, scope_typedef_entry_t entry)
{
    if (ctx == NULL)
    {
        return;
    }

    scope_add_typedef_entry(ctx->current_scope, entry);
}

type_kind_t
generator_lookup_kind_by_type_descriptor(ir_generator_ctx_t * ctx, TypeDescriptor const * type_desc)
{
    /* FIXME: Something about needing this function smells. */
    if (ctx == NULL)
    {
        return TYPE_KIND_BUILTIN;
    }

    return scope_lookup_kind_by_type_descriptor(ctx->current_scope, type_desc);
}

TypeDescriptor const *
generator_find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name)
{
    TypeDescriptor const * result = scope_find_typedef_type_descriptor(ctx->current_scope, name);
    return result;
}

TypeDescriptor const *
generator_find_type_descriptor_by_tag(ir_generator_ctx_t * ctx, char const * name)
{
    type_info_t * info = scope_find_tagged_struct(ctx->current_scope, name);
    if (info == NULL)
    {
        info = scope_find_tagged_union(ctx->current_scope, name);
    }
    if (info == NULL)
    {
        info = scope_find_tagged_enum(ctx->current_scope, name);
    }
    debug_info("%s: found: %p", __func__, info);
    if (info != NULL)
    {
        dump_type_descriptor(__func__, info->type_desc, DEBUG_LEVEL_INFO);
    }
    return info ? info->type_desc : NULL;
}

type_info_t *
generator_lookup_tagged_entry_by_tag_and_kind(ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind)
{
    debug_info("%s: tag: %s", __func__, tag);

    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_lookup_tagged_entry_by_tag_and_kind(ctx->current_scope, tag, kind);
}
