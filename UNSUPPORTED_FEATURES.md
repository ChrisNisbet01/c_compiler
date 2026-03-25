# Unsupported C Features

This document lists C features that are not yet supported by the ncc compiler.

## Type System

- **Function pointers** - e.g., `int (*fp)(int, int)`
- **Bit-fields in structs** - e.g., `int x : 3`
- **Variable-length arrays (VLAs)** - e.g., `int arr[n];`
- **Flexible array members** - e.g., `int data[];` as last struct member
- **Type qualifiers on variables** - const, volatile, restrict, _Atomic
- **`_Bool` / `bool` type** - from stdbool.h
- **`long double`** - may map to double or be incomplete
- **Complex number types** - `_Complex`

## Expressions

- **Compound literals** - e.g., `(struct Point){1, 2}`
- **Designated initializers** - e.g., `.x = 1, .y = 2`
- **Statement expressions** - GCC extension `({ int x = 5; x; })`
- **Generic selection** - `_Generic` (C11)
- **Atomic operations** - `_Atomic`, `_Atomic()` (C11)

## Declarations

- **Storage class specifiers** - static, extern, register, auto, inline
- **Complex declarators** - function pointer declarations
- **Multiple declarators** - `int x, y, z;` may have limited support
- **Static array parameters** - `void f(int arr[static 10])`

## Preprocessor

- **Macro definitions** - `#define`, `#undef`
- **Conditional compilation** - `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`
- **Macro functions** - `#define MAX(a,b) ((a)>(b)?(a):(b))`
- **Include guards** - pragma once
- **Stringification** - `#` operator
- **Token pasting** - `##` operator
- **Variadic macros** - `__VA_ARGS__`

## Control Flow

- **`do-while` loops** - partially implemented, may need testing
- **`_Noreturn` functions** - C11
- **`_Thread_local`** - C11 thread-local storage

## Standard Library

- **Full variadic function support** - va_list, va_start, va_arg, va_end
- **Most standard headers** - stdio.h is partially supported (printf auto-declared)
- **Dynamic memory** - malloc, free, etc. (may work if auto-declared)
- **String functions** - strlen, strcpy, etc. (may work if auto-declared)

## Other

- **`__attribute__`** - GCC extensions
- **`#pragma`** - Pragma directives
- **Inline assembly** - `asm()` / `__asm__()`
- **Labels as values** - `&&label` GCC extension
- **Computed gotos** - GCC extension
- **Nested functions** - GCC extension
- **Decimal floating point** - `_Decimal32`, `_Decimal64`
- **Fixed-point types** - `_Fract`, `_Accum`

## Notes

- Some features like auto-declaration of external functions (printf) may partially work
- The grammar may parse some constructs but IR generation is incomplete
- Test files in `tests/` directory show what IS supported
