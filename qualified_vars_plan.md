# Plan: Fix Pointer Qualifier Handling

## Problem
The pointer qualifier handling in `src/declaration_handler.c` (lines 475-494) is incorrect.
1. Loop iterates RIGHT-TO-LEFT through the pointer list, which builds nested pointer types in the wrong order.
2. Qualifiers applied to pointee instead of pointer: `int * const p` is parsed as `pointer to const int` instead of `const pointer to int`.

## Root Cause
In the current code:
```c
for (size_t i = pointer_list->list.count; i > 0; i--)  // RIGHT-TO-LEFT
{
    ...
    if (ptr_quals.is_const || ptr_quals.is_volatile)
    {
        current = get_or_create_qualified_type(...);  // Applies to pointee
        ptr_quals = (TypeQualifier){0};
    }
    current = get_or_create_pointer_type(..., ptr_quals);
}
```

## Fix
1. Change loop from RIGHT-TO-LEFT to LEFT-TO-RIGHT.
2. Remove code that applies `ptr_quals` to pointee (`current`).
3. Pass `ptr_quals` directly to `get_or_create_pointer_type()`.

## Verification
- Create reproduction tests in `new_tests/`.
- Ensure `int * const p` is handled as a const pointer to int.
- Ensure `const int * p` is handled as a pointer to const int.
- Ensure `int const * const p` is handled as a const pointer to const int.
- Ensure `int * const * p` is handled as a pointer to const pointer to int.
- Run all tests in `tests/` and `tests/expected_to_fail/`.
