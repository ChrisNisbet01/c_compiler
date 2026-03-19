# Compiler Improvement Plan

This plan outlines the steps to enhance the `ncc` compiler to support more complex C code, focusing on literals, control flow, and basic type system improvements.

## 0. Project Maintenance

- [x] Save this plan to `IMPROVEMENT_PLAN.md` in the project root to track progress.

## 1. String Literal Support

**Goal:** Enable parsing and code generation for string literals (e.g., `"Hello, World!"`).

- [x] Add `AST_NODE_STRING_LITERAL` to `c_grammar_node_type_t` in `src/c_grammar_ast.h`.
- [x] Update `src/main.c` `node_type_names` and `print_ast` to handle `AST_NODE_STRING_LITERAL`.
- [x] Modify `src/c_grammar.gdl` to attach `@AST_ACTION_STRING_LITERAL` to the `StringLiteral` rule.
- [x] Implement `handle_string_literal` in `src/c_grammar_ast_actions.c` to create the AST node.
- [x] Update `process_expression` in `src/llvm_ir_generator.c` to handle `AST_NODE_STRING_LITERAL`.
- [ ] TODO: Handle escape sequences in string literals (e.g., `\n`, `\t`).

## 2. Improved Integer & Float Literals (with Suffixes)

**Goal:** Support literal suffixes (e.g., `U`, `L`, `LL`, `f`) to determine correct LLVM types.

- [x] Add `AST_NODE_LITERAL_SUFFIX`, `AST_NODE_INTEGER_BASE`, `AST_NODE_FLOAT_BASE`, `AST_NODE_INTEGER_VALUE`, `AST_NODE_FLOAT_VALUE` to `src/c_grammar_ast.h`.
- [x] Update `src/main.c` to support printing the new nodes.
- [x] In `src/c_grammar.gdl`, add `@AST_ACTION_LITERAL_SUFFIX`, `@AST_ACTION_INTEGER_BASE`, etc.
- [x] Implement handlers in `src/c_grammar_ast_actions.c` to build the new two-child structure.
- [x] Update `process_expression` in `src/llvm_ir_generator.c`:
    - [x] Use `strtoull` for `AST_NODE_INTEGER_VALUE` to support hex/octal and preserve precision.
    - [x] Map suffixes to LLVM types:
        - Integers: `L` -> `i64`, `LL` -> `i64`, `U` -> unsigned `i32`, `UL` -> unsigned `i64`.
        - Floats: `f` -> `float`, no suffix -> `double`, `L` -> `long double` (`x86_fp80`).

## 3. Control Flow

**Goal:** Implement basic control flow structures.

### 3.1 If Statements
- [x] Implement `AST_NODE_IF_STATEMENT` in `src/llvm_ir_generator.c` (`process_ast_node`).
    - [x] Generate basic blocks for `then`, `else` (if present), and `merge`.
    - [x] Use `LLVMBuildCondBr`.

### 3.2 Loops
- [x] Implement `AST_NODE_WHILE_STATEMENT` in `src/llvm_ir_generator.c`.
    - [x] Generate blocks for `condition`, `body`, and `after`.
- [x] Implement `AST_NODE_FOR_STATEMENT`.

## 4. Functions

**Goal:** Support function calls and parameters.

### 4.1 Function Definition
- [x] Update `AST_NODE_FUNCTION_DEFINITION` in `src/llvm_ir_generator.c` to handle parameters.
    - [x] Add parameters to the symbol table so they can be referenced in the body.
    - [x] Correctly handle pointer parameters (e.g., `char ** argv`).

### 4.2 Function Calls
- [x] Implement `AST_NODE_FUNCTION_CALL` in `src/llvm_ir_generator.c` (`process_expression`).
    - [x] Resolve the function name.
    - [x] Process arguments.
    - [x] Generate `LLVMBuildCall2`.

## 5. Type System (Basic)

**Goal:** Move away from hardcoded `int` types.

- [x] Implement `map_type` helper to map C AST type specifiers (e.g., `int`, `float`, `char`) to `LLVMTypeRef`.
- [x] Support **Pointers** in the type mapping logic (recursively handling `*`).
- [x] Implement explicit **Casts** (`AST_NODE_CAST_EXPRESSION`) in `process_expression`.
    - [x] Support `fptosi`, `sitofp`, `fpext`, `fptrunc`.
- [x] Implement **Implicit Casting** in variable declarations (`AST_NODE_DECLARATION`).

## 6. Operators

**Goal:** Expand binary/unary operator support.

- [ ] Add support for bitwise operators (`&`, `|`, `^`, `<<`, `>>`) in `process_expression`.
- [ ] Add support for logical operators (`&&`, `||`) with short-circuiting logic.

## 7. Structs and Arrays

**Goal:** Support composite data types.

### 7.1 Arrays
- [ ] Support array declarations (e.g., `int arr[10];`).
- [ ] Support array indexing (e.g., `arr[i]`).
- [ ] Update `map_type` to handle array types.

### 7.2 Structs
- [ ] Support struct definitions (`struct S { int x; float y; };`).
- [ ] Support struct variable declarations.
- [ ] Support member access via dot (`s.x`) and arrow (`p->x`).
- [ ] Implement struct type registration in the LLVM context.
