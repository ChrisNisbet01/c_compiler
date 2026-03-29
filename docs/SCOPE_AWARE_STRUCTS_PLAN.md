# Scope-Aware Struct Registration Plan

## GCC/Clang Behavior

In GCC/Clang, structs declared in a block scope are **local types**:
- They can only be used within that block
- They shadow any outer structs with the same name
- When the block exits, the struct type is no longer accessible

Example:
```c
void foo() {
    struct Point { int x; };  // Local to foo()
    struct Point p;
    p.x = 1;
}  // Point is no longer accessible here

void bar() {
    struct Point { int y; };  // Different from foo's Point
    struct Point q;
}
```

---

## Implementation Plan

### 1. Create scope_local_types_t structure (`llvm_ir_generator.h`)

```c
// Struct and union types declared in a particular scope
typedef struct scope_local_types
{
    struct_info_t * structs;
    size_t count;
    size_t capacity;
} scope_local_types_t;
```

### 2. Update `scope_t` structure (`llvm_ir_generator.h`)

```c
typedef struct scope
{
    symbol_t * symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    
    scope_local_types_t local_types;  // ADD: structs/unions in this scope
    
    struct scope * parent; // Chain to outer scope (NULL for global)
} scope_t;
```

### 3. Update `scope_create` (`llvm_ir_generator.c`)

Initialize the new local_types fields:
```c
scope->local_types.capacity = 4;
scope->local_types.structs = calloc(scope->local_types.capacity, sizeof(*scope->local_types.structs));
scope->local_types.count = 0;
```

### 4. Update `scope_free` (`llvm_ir_generator.c`)

Free struct names and array:
```c
// Free all local types in this scope
for (size_t i = 0; i < scope->local_types.count; ++i)
{
    free(scope->local_types.structs[i].name);
    // Free fields if needed
}
free(scope->local_types.structs);
```

### 5. Add helper functions (`llvm_ir_generator.c`)

```c
static void scope_add_struct(ir_generator_ctx_t * ctx, struct_info_t info)
{
    scope_t * scope = ctx->current_scope;
    if (!scope) return;
    
    // Expand if needed
    if (scope->local_types.count >= scope->local_types.capacity)
    {
        scope->local_types.capacity *= 2;
        scope->local_types.structs = realloc(...);
    }
    
    scope->local_types.structs[scope->local_types.count++] = info;
}

static struct_info_t * scope_find_struct(ir_generator_ctx_t * ctx, char const * name)
{
    for (scope_t * scope = ctx->current_scope; scope != NULL; scope = scope->parent)
    {
        for (size_t i = 0; i < scope->local_types.count; ++i)
        {
            if (scope->local_types.structs[i].name 
                && strcmp(scope->local_types.structs[i].name, name) == 0)
            {
                return &scope->local_types.structs[i];
            }
        }
    }
    return NULL;
}
```

### 6. Update `register_struct_definition_with_name`

Replace global struct registration with scope-based:
- Change: `ctx->structs[ctx->struct_count++] = info;`
- To: `scope_add_struct(ctx, info);`

### 7. Update `find_struct_info`

Replace global search with scope-based:
- Change: linear search through `ctx->structs`
- To: `scope_find_struct(ctx, name)`

### 8. Clean up ir_generator_ctx_t (`llvm_ir_generator.h`)

Remove the old global struct tracking:
```c
// Remove:
// struct_info_t * structs;
// size_t struct_count;
// size_t struct_capacity;
```

---

## Order of Implementation

1. Add `scope_local_types_t` struct to `llvm_ir_generator.h`
2. Update `scope_t` to include `local_types`
3. Update `scope_create` to initialize `local_types`
4. Update `scope_free` to free `local_types`
5. Add `scope_add_struct` and `scope_find_struct` functions
6. Update `register_struct_definition_with_name` to use scope functions
7. Update `find_struct_info` to use scope search
8. Remove old global struct fields from `ir_generator_ctx_t`
9. Test

---

## Labels - Separate Task

Labels have function scope in C (not block scope). They are accessible anywhere within the function they're declared. This is different from regular block-scoped symbols and will require a separate implementation.
