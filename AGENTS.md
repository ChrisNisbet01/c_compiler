# AGENTS.md - C Compiler Development Guide

## Overview

This is a C compiler project (NCC) that compiles C code to LLVM IR, which is then compiled to an executable using clang. The compiler uses a GDL-based parser and generates LLVM IR via the C API.

## Build Commands

### Building the Project

```bash
# Configure CMake (first time only)
cmake -S . -B build

# Build the compiler
cmake --build build
```

The compiler executable is at: `build/src/ncc`

### Compiler Usage

```bash
# Compile to LLVM IR
build/src/ncc -S --emit-llvm -o output.ll input.c

# Compile to object file
build/src/ncc -c -o output.o input.c

# Compile to executable (links with clang)
build/src/ncc -o output input.c
```

## Test Commands

### Run All Tests

```bash
tests/run_tests.sh build/src/ncc
```

### Run Single Test

```bash
tests/run_tests.sh build/src/ncc tests/test_file.c
```

The test script compiles each C file with ncc, then with clang, runs the executable, and verifies the exit code is 0.

## Code Style Guidelines

### Formatting (from .clang-format)

- **Indentation:** 4 spaces (no tabs)
- **Line limit:** 120 characters
- **Braces:** Allman style (opening brace on own line)
- **Pointer alignment:** Middle (`LLVMValueRef * ptr`)
- **Qualifier order:** type, const, volatile (east const)

### C Coding Style (from C_CODING_STYLE.md)

Key rules to follow:

1. **East const:** Place `const` to the right of the type it modifies
   ```c
   int const MAX_VALUE = 100;
   char const * name;
   ```

2. **No implicit booleans:** Always make comparisons explicit
   ```c
   if (ptr != NULL) { }
   if (count != 0) { }
   ```

3. **Always use braces:** Even for single-line bodies
   ```c
   if (condition)
   {
       do_something();
   }
   ```

4. **Separate lookup from use:** Don't combine searching with using an item
   ```c
   // Good: separate lookup
   item_t * item = find_item(id);
   if (item != NULL) { use(item); }
   ```

5. **sizeof:** Use variable, not type
   ```c
   int * arr = malloc(count * sizeof(*arr));
   ```

6. **Comments:** Use `/* ... */` block comments, not `//`

7. **Include order:**
   - Corresponding header first
   - Project headers (sorted)
   - External library headers (sorted)
   - System headers (sorted)

### VSCode Configuration

To enable automatic formatting in VSCode, add to settings.json:

```json
{
    "C_Cpp.clang_format_style": "file",
    "editor.formatOnSave": true,
    "editor.defaultFormatter": "ms-vscode.cpptools"
}
```

## Project Structure

- `src/` - Source code
- `src/llvm_ir_generator.c` - LLVM IR generation
- `src/c_grammar.gdl` - Grammar definition
- `src/c_grammar_ast.h` - AST node definitions
- `src/c_grammar_ast_actions.c` - AST building actions
- `tests/` - Test C files
- `build/` - Build output

## Important Patterns

### AST Node Types

The compiler uses enum-based node types (e.g., `AST_NODE_ASSIGNMENT`, `AST_NODE_POSTFIX_EXPRESSION`). Use the enum values instead of string comparisons.

### Error Handling

- Print errors to stderr with `fprintf`
- Return NULL on failure
- Check return values before use

### LLVM IR Generation

- Use the LLVM C API functions (prefixed with `LLVM`)
- Build IR with the builder: `LLVMBuild...()`
- Use GEP for array/struct access: `LLVMBuildInBoundsGEP2()`

## Common Tasks

### Adding a New AST Node Type

1. Add enum to `c_grammar_ast.h`
2. Add handler in `c_grammar_ast_actions.c`
3. Add case in `llvm_ir_generator.c` process_expression() or process_ast_node()
4. Add name to `main.c` get_node_type_name_from_node()

### Adding a New Test

1. Create C file in `tests/` directory
2. Ensure main() returns 0 on success
3. Run: `tests/run_tests.sh build/src/ncc tests/your_test.c`
