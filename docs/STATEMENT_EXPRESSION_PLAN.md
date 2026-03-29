# Statement Expression Implementation Plan

GCC extension: `({ int x = 5; x; })`

## GCC Behavior

- `({ 5; })` - returns 5
- `({ int x; })` - error: "void value not ignored as it ought to be"
- `({ ; })` - error: "void value not ignored as it ought to be"
- Nesting is supported: `({ int x = ({ 1; }); x; })`

---

## 1. Grammar Update (`src/c_grammar.gdl`)

Add statement expression as an option in `PrimaryExpression`:

```gdl
// Statement expression - GCC extension: ({ statement ... last_expression })
StatementExpression = LParen LBrace (Declaration | Statement)* ExpressionStatement RBrace RParen @AST_ACTION_STATEMENT_EXPRESSION;
```

Add to `PrimaryExpression` (line 169):
```gdl
PrimaryExpression = NumberLiteral
                  | StringLiteral
                  | CharLiteral
                  | CompoundLiteral
                  | Identifier
                  | StatementExpression   // <-- ADD THIS
                  | (LParen Expression RParen);
```

---

## 2. AST Node (`src/c_grammar_ast.h`)

Add enum:
```c
AST_NODE_STATEMENT_EXPRESSION,
```

---

## 3. AST Action (`src/c_grammar_ast_actions.c`)

- Register action: `epc_ast_hook_registry_set_action(registry, AST_ACTION_STATEMENT_EXPRESSION, handle_statement_expression);`
- Add handler (similar to `handle_compound_literal`):

```c
static void
handle_statement_expression(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    c_grammar_node_t * ast_node = handle_list_node(ctx, node, children, count, user_data, AST_NODE_STATEMENT_EXPRESSION);
    if (ast_node == NULL)
    {
        return;
    }

    epc_ast_push(ctx, ast_node);
}
```

---

## 4. IR Generation (`src/llvm_ir_generator.c`)

Handle in `process_expression()` - case `AST_NODE_STATEMENT_EXPRESSION`:

```c
case AST_NODE_STATEMENT_EXPRESSION:
{
    // Create new scope for this block
    scope_push(ctx);

    LLVMValueRef result = NULL;
    LLVMTypeRef result_type = NULL;

    // Iterate through children, processing all but the last
    // Child structure: [Statement*, ExpressionStatement]
    size_t count = node->data.list.count;
    for (size_t i = 0; i < count; ++i)
    {
        c_grammar_node_t * child = node->data.list.children[i];

        if (i == count - 1)
        {
            // Last child is ExpressionStatement - get its expression value
            if (child->type == AST_NODE_EXPRESSION_STATEMENT)
            {
                if (child->data.list.count == 0 || child->data.list.children[0] == NULL)
                {
                    fprintf(stderr, "IRGen Error: Statement expression must have a final expression.\n");
                    scope_pop(ctx);
                    return NULL;
                }
                c_grammar_node_t * expr = child->data.list.children[0];
                result = process_expression(ctx, expr);
                result_type = ctx->current_type;
            }
            else
            {
                fprintf(stderr, "IRGen Error: Statement expression must end with an expression statement.\n");
                scope_pop(ctx);
                return NULL;
            }
        }
        else
        {
            // Process declaration or statement
            process_ast_node(ctx, child);
        }
    }

    scope_pop(ctx);

    // Set the result for the parent expression context
    ctx->current_val = result;
    ctx->current_type = result_type;
    break;
}
```

---

## Implementation Order

1. Add `AST_NODE_STATEMENT_EXPRESSION` to `c_grammar_ast.h`
2. Add handler in `c_grammar_ast_actions.c`
3. Add grammar rule in `c_grammar.gdl`
4. Add IR generation case in `llvm_ir_generator.c`
5. Test with:
   - `({ 5; })`
   - `({ int x = 5; x; })`
   - `({ int x = ({ 1; }); x; })`
   - `({ ; })` - should error
   - `({ int x; })` - should error

---

## Notes

- Each statement expression creates its own scope via `scope_push`/`scope_pop`
- `process_expression` recursively handles nested statement expressions
- The `current_val` and `current_type` are set on the context for parent expression handling
