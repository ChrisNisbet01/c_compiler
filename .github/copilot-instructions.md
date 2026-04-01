# C Coding Standards:

## No implicit boolean conversions: Always use explicit comparisons for pointers and integers in conditionals.

## Pointer checks: Use if (ptr != NULL) instead of if (ptr).

## Integer checks: Use if (count > 0) or if (val != 0) instead of if (val).

# Coding style

## Prefer calloc() over malloc() when creating new objects, to ensure memory is zero-initialized.
This removes the need to explicityly zero out fields after allocation, and prevents bugs from uninitialized memory.
