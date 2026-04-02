# NCC - An Exploratory C Compiler

## Overview

NCC is a functional C compiler prototype built to explore the capabilities of the **[easy_pc](https://github.com/ChrisNisbet01/easy_pc)** parser/combinator library. 

**Important Note:** This project is not intended to be a "serious," production-ready C compiler. Its primary purpose is to serve as a real-world test case for `easy_pc`, highlighting the library's strengths and identifying areas for improvement when parsing complex, context-sensitive languages like C.

## Architecture

NCC leverages established industrial tools to focus its development on the parsing and IR generation stages:

1.  **Preprocessing:** NCC does not implement its own preprocessor. It leverages `clang -E` to handle macros, includes, and conditional compilation.
2.  **Parsing:** The core of the project. It uses `easy_pc` and a GDL (Grammar Description Language) definition (`src/c_grammar.gdl`) to generate a Concrete Parse Tree (CPT), which is then transformed into an Abstract Syntax Tree (AST) via semantic actions.
3.  **Intermediate Representation (IR):** NCC uses the LLVM C API to generate LLVM IR directly from the AST.
4.  **Backend & Linking:** Final machine code generation and linking are handled by `clang` and the standard system linker, consuming the LLVM IR or object files produced by NCC.

## Current Capabilities

Despite its exploratory nature, NCC supports a substantial subset of the C language:

*   **Type System:** 
    *   Basic types: `void`, `char`, `int`, `float`, `double`, `_Bool`.
    *   Extended types: `long`, `long long`, and `long double` (mapped to `x86_fp80`).
    *   Derived types: Multiple levels of pointers, multi-dimensional arrays, and `typedef` aliases.
    *   Aggregates: `struct` and `union` (including nested definitions).
    *   Enums: Named and anonymous enumerations.
    *   Function Pointers: Full support for declarations, assignments, and indirect calls.
*   **Expressions:** Standard arithmetic, bitwise, logical, relational, and assignment operators, including compound assignments.
*   **Control Flow:** `if-else`, `while`, `do-while`, `for` loops, `switch-case` statements, and `goto`.
*   **Declarations:** Support for multiple declarators in a single statement (e.g., `int x, *y, z = 10;`).

## Shortcomings and Limitations

As an experimental tool, NCC has several known limitations:

*   **AST Fragility:** The current AST implementation relies on positional indexing of child nodes. Changes to the grammar that add or remove semantic actions can shift indices, requiring manual updates to the IR generator.
*   **Limited Standard Library:** There is no dedicated standard library. NCC relies on "auto-declaring" external functions like `printf` to interact with the system.
*   **Type Promotion & Checking:** While basic type promotion exists, the semantic analysis phase is not as rigorous as a production compiler (e.g., `const` qualifiers are parsed but not yet fully enforced).
*   **GDL-to-AST Mapping:** The transformation from the parser's output to the AST is manually managed through action callbacks, which can be complex to maintain as the grammar grows.

## Getting Started

### Prerequisites
*   LLVM 15+ (with headers and libraries)
*   Clang (for preprocessing and linking)
*   CMake 3.10+
*   The `easy_pc` library

### Building
```bash
mkdir build
cd build
cmake ..
make
```

### Running Tests
NCC includes a test suite in the `tests/` directory. To run all tests:
```bash
./tests/run_tests.sh ./build/src/ncc
```

## License
Refer to the `LICENSE` file for details.
