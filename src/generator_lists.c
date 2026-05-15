#include "generator_lists.h"

#include "llvm_ir_generator.h"
#include "scope.h"

#include <stdlib.h>
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
generator_add_symbol(ir_generator_ctx_t * ctx, char const * name, TypedValue value)
{
    if (ctx == NULL)
    {
        return;
    }

    debug_info(
        "generator_add_symbol: '%s' storing type_info=%p, const=%d",
        name,
        (void *)value.type_info,
        value.type_info->qualifiers.is_const
    );

    scope_add_symbol(ctx->current_scope, name, value);
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
generator_add_type_info(ir_generator_ctx_t * ctx, type_info_t info)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return scope_add_type_info(ctx->current_scope, info);
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

TypeDescriptor const *
generator_find_typedef_type_descriptor(ir_generator_ctx_t * ctx, char const * name)
{
    TypeDescriptor const * result = scope_find_typedef_type_descriptor(ctx->current_scope, name);
    return result;
}

TypeDescriptor const *
generator_find_type_descriptor_by_tag_and_kind(ir_generator_ctx_t * ctx, char const * tag, type_kind_t kind)
{
    debug_info("%s: tag: %s", __func__, tag);

    type_info_t * info = generator_lookup_tagged_entry_by_tag_and_kind(ctx, tag, kind);

    if (info == NULL)
    {
        return NULL;
    }

    return info->type_desc;
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
