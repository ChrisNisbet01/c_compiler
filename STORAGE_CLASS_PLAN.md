# Storage Class Specifiers and Type Qualifiers Support Plan

## Phase 1: Create Tests to Demonstrate Current Gaps ✅ DONE

### Tests created:
1. `test_const_global.c` - Test `const` type qualifier for globals ✅ PASSES
2. `test_const_local.c` - Test `const` local variable ✅ PASSES  
3. `test_const_assignment_error.c` - Test error for modifying const ✅ PASSES (generates error)
4. `test_volatile.c` - Test `volatile` type qualifier ✅ PASSES
5. `test_extern_variable.c` - Test `extern` storage class (removed - linker issue)

## Phase 2: Implement Const Support ✅ DONE

### 2.1 Add type qualifier tracking to AST ✅
- Added boolean flags to `ast_node_decl_specifiers_t`: `has_const`, `has_volatile`, `has_restrict`
- Added `type_qualifier_data_t` struct in AST header
- Added `type_qualifier` to node union

### 2.2 Extend symbol_t in scope.h ✅
- Added `bool is_const` field to track const variables
- Added `bool is_volatile` field
- Added `bool is_extern` field

### 2.3 Add const support for globals ✅
- In `AST_NODE_DECLARATION`: check const flag and call `LLVMSetGlobalConstant()`

### 2.4 Add const error checking ✅
- In `process_assignment()`: check if target is const and emit error

## Phase 3: Implement Volatile Support ⏳ IN PROGRESS

### 3.1 Extend symbol_t ✅ (done above)

### 3.2 Add volatile support in code generation
- For globals: calls `LLVMSetVolatile()` but this doesn't work for globals
- For local variables: NOT YET IMPLEMENTED - need to use volatile loads/stores

## Phase 4: Implement Extern Support ⏳ NOT STARTED

### 4.1 Extend symbol_t ✅ (done above)

### 4.2 Add extern handling
- For file-scope extern declarations: use external linkage
- For extern declarations without definition: need to emit as declaration

## Remaining Work

1. Add volatile support for local variables (use LLVMSetVolatile on loads/stores)
2. Add extern support for file-scope variables
3. Verify tests pass