# Expression Operator Refactoring Plan

**Goal**: Shift complexity from IR generator to AST building code by replacing strcmp-based operator detection with pre-computed enum values.

**Status**: ✅ ALL PHASES COMPLETE

---

## Background

The IR generator (`llvm_ir_generator.c`) has 39 strcmp() calls for operator text comparison. This is fragile (no compile-time checking) and repetitive. The bitwise operators already use enums - this plan extends that pattern to all operators.

---

## Phase 1: Shift Operators

**Target**: `c_grammar.gdl` lines 230-231, `llvm_ir_generator.c` lines 3167-3169

**Changes**:
- [x] Grammar: Add `@AST_ACTION_OPERATOR` to `ShiftOp`
- [x] Grammar: Add `@AST_ACTION_SHIFT_EXPRESSION` to `ShiftExpression`
- [x] AST Header: Add `shift_operator_type_t` enum with `SHIFT_OP_LL`, `SHIFT_OP_AR` (>>)
- [x] AST Header: Add `shift_op` field to `c_grammar_node_t` union
- [x] AST Actions: Add `handle_shift_expression()` to extract operator and set enum
- [x] IR Generator: Replace strcmp with enum switch in `build_binary_operation()`

**Status**: ✅ COMPLETE (33/33 tests pass)

**Files modified**:
- `src/c_grammar.gdl` - Added semantic actions to ShiftOp and ShiftExpression
- `src/c_grammar_ast.h` - Added `shift_operator_type_t` enum and `shift_op` union field
- `src/c_grammar_ast_actions.c` - Added `handle_shift_expression()` with validation for exactly 3 children
- `src/llvm_ir_generator.c` - Added `AST_NODE_SHIFT_EXPRESSION` case with enum switch
- `src/main.c` - Added ShiftExpression to node type names

**Key findings from AST analysis**:
- `chainl1` always produces nodes with exactly 3 children for each operator
- For chained shifts like `a << b << c`, inner node is `[a, <<, b]`, outer is `[(inner), <<, c]`
- Validation added: `count != 3` triggers error (grammar guarantees this shouldn't happen)

**Test file**: `tests/test_shifts.c` - Tests single and chained shift operations

---

## Phase 2: Arithmetic Operators

**Target**: `c_grammar.gdl` lines 224-228, `llvm_ir_generator.c` lines 3125-3137

**Changes**:
- [x] AST Header: Add `arithmetic_operator_type_t` enum (`ADD`, `SUB`, `MUL`, `DIV`, `MOD`)
- [x] AST Header: Add `arith_op` field to union
- [x] AST Header: Add `AST_NODE_ARITHMETIC_EXPRESSION` node type
- [x] Grammar: Add `@AST_ACTION_ARITHMETIC_EXPRESSION` to `MultiplicativeExpression` and `AdditiveExpression`
- [x] AST Actions: Add `handle_arithmetic_expression()` with validation for exactly 3 children
- [x] IR Generator: Replace arithmetic strcmp calls with enum switch
- [x] IR Generator: Remove `AST_NODE_BINARY_OP` case (replaced by `AST_NODE_ARITHMETIC_EXPRESSION`)
- [x] IR Generator: Remove `handle_binary_op()` function and registration

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar.gdl` - Changed from `@AST_ACTION_BINARY_OP` to `@AST_ACTION_ARITHMETIC_EXPRESSION`
- `src/c_grammar_ast.h` - Added `arithmetic_operator_type_t` enum, `arithmetic_operator_data_t` struct, `arith_op` union field, `AST_NODE_ARITHMETIC_EXPRESSION`
- `src/c_grammar_ast_actions.c` - Added `handle_arithmetic_expression()`, removed `handle_binary_op()`
- `src/llvm_ir_generator.c` - Added `AST_NODE_ARITHMETIC_EXPRESSION` case with enum switch
- `src/main.c` - Added `ArithmeticExpression` to node type names

---

## Phase 3: Relational Operators

**Target**: `c_grammar.gdl` lines 233-234, `llvm_ir_generator.c` lines 3147-3156

**Changes**:
- [x] AST Header: Add `relational_operator_type_t` enum (`LT`, `GT`, `LE`, `GE`)
- [x] AST Header: Add `rel_op` field to union
- [x] AST Actions: Update `handle_relational_expression()` with validation for exactly 3 children
- [x] IR Generator: Replace 4 strcmp calls with enum switch

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar_ast.h` - Added `relational_operator_type_t` enum, `relational_operator_data_t` struct, `rel_op` union field
- `src/c_grammar_ast_actions.c` - Updated `handle_relational_expression()` to extract operator and set enum
- `src/llvm_ir_generator.c` - Added enum-based switch for relational operations

---

## Phase 4: Equality Operators

**Target**: `c_grammar.gdl` lines 236-237, `llvm_ir_generator.c` lines 3141-3144

**Changes**:
- [x] AST Header: Add `equality_operator_type_t` enum (`EQ`, `NE`)
- [x] AST Header: Add `eq_op` field to union
- [x] AST Actions: Update `handle_equality_expression()` with validation for exactly 3 children
- [x] IR Generator: Replace 2 strcmp calls with enum switch

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar_ast.h` - Added `equality_operator_type_t` enum, `equality_operator_data_t` struct, `eq_op` union field
- `src/c_grammar_ast_actions.c` - Updated `handle_equality_expression()` to extract operator and set enum
- `src/llvm_ir_generator.c` - Added enum-based switch for equality operations

---

## Phase 5: Logical AND/OR

**Target**: `c_grammar.gdl` lines 245, 247, `llvm_ir_generator.c` lines 3178-3190

**Changes**:
- [x] AST Header: Add `logical_operator_type_t` enum (`AND`, `OR`)
- [x] AST Header: Add `logical_op` field to union
- [x] AST Actions: Update `handle_logical_and_expression()` and `handle_logical_or_expression()` with validation
- [x] IR Generator: Update to use `node->logical_op.op` instead of node type check

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar_ast.h` - Added `logical_operator_type_t` enum, `logical_operator_data_t` struct, `logical_op` union field
- `src/c_grammar_ast_actions.c` - Updated logical expression handlers with validation
- `src/llvm_ir_generator.c` - Updated to use `logical_op.op` enum

---

## Phase 6: Unary Operators

**Target**: `c_grammar.gdl` lines 195-207, `llvm_ir_generator.c` lines 3238-3313

**Changes**:
- [x] AST Header: Add `unary_operator_type_t` enum (`PLUS`, `MINUS`, `NOT`, `BITNOT`, `ADDR`, `DEREF`, `INC`, `DEC`, `SIZEOF`, `ALIGNOF`)
- [x] AST Header: Add `unary_op` field to union
- [x] AST Actions: Update `handle_unary_op()` to extract operator and set enum
- [x] IR Generator: Replace strcmp calls with enum switch for all unary operators

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar_ast.h` - Added `unary_operator_type_t` enum, `unary_operator_data_t` struct, `unary_op` union field
- `src/c_grammar_ast_actions.c` - Updated `handle_unary_op()` to extract operator and set enum
- `src/llvm_ir_generator.c` - Refactored entire unary op handling to use enum switch

---

## Phase 7: Postfix Increment/Decrement

**Target**: `c_grammar.gdl` lines 185-193, `llvm_ir_generator.c` lines 2699-2720

**Changes**:
- [x] Grammar: Add `PostfixIncOp` and `PostfixDecOp` rules with `@AST_ACTION_POSTFIX_OPERATOR`
- [x] AST Header: Add `postfix_operator_type_t` enum (`INC`, `DEC`)
- [x] AST Header: Add `postfix_op` field to union
- [x] AST Header: Add `AST_NODE_POSTFIX_OPERATOR` node type
- [x] AST Actions: Add `handle_postfix_operator()` function
- [x] IR Generator: Update to check `AST_NODE_POSTFIX_OPERATOR` instead of `AST_NODE_OPERATOR` with strcmp

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar.gdl` - Added `PostfixIncOp` and `PostfixDecOp` rules
- `src/c_grammar_ast.h` - Added `postfix_operator_type_t` enum, `postfix_operator_data_t` struct, `postfix_op` union field, `AST_NODE_POSTFIX_OPERATOR`
- `src/c_grammar_ast_actions.c` - Added `handle_postfix_operator()` function
- `src/llvm_ir_generator.c` - Updated to use `AST_NODE_POSTFIX_OPERATOR` and `postfix_op.op` enum
- `src/main.c` - Added `PostfixOperator` to node type names

---

## Phase 8: Assignment Operators (Including Compound)

**Target**: `c_grammar.gdl` lines 251-256, `llvm_ir_generator.c` lines 2954-3008

**Changes**:
- [x] Grammar: Add `@AST_ACTION_ASSIGNMENT_OPERATOR` to `AssignmentOp` rule
- [x] AST Header: Add `assignment_operator_type_t` enum (`SIMPLE`, `SHL`, `SHR`, `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `AND`, `XOR`, `OR`)
- [x] AST Header: Add `assign_op` field to union
- [x] AST Header: Add `AST_NODE_ASSIGNMENT_OPERATOR` node type
- [x] AST Actions: Add `handle_assignment_operator()` to set enum
- [x] IR Generator: Replace strcmp calls with enum switch for all compound assignment operators

**Status**: ✅ COMPLETE (34/34 tests pass)

**Files modified**:
- `src/c_grammar.gdl` - Changed `AssignmentOp` to use `@AST_ACTION_ASSIGNMENT_OPERATOR`
- `src/c_grammar_ast.h` - Added `assignment_operator_type_t` enum, `assignment_operator_data_t` struct, `assign_op` union field, `AST_NODE_ASSIGNMENT_OPERATOR`
- `src/c_grammar_ast_actions.c` - Added `handle_assignment_operator()` function
- `src/llvm_ir_generator.c` - Refactored compound assignment handling to use enum switch
- `src/main.c` - Added `AssignmentOperator` to node type names

---

## Phase 9: Helper Accessor Functions

**Target**: New file `src/ast_helpers.h`

**Changes** (optional, based on remaining complexity):
- [ ] Add accessor functions for binary ops: `ast_binary_get_lhs()`, `ast_binary_get_rhs()`, `ast_binary_get_op()`
- [ ] Add accessor functions for unary ops: `ast_unary_get_operand()`, `ast_unary_get_op()`
- [ ] Add accessor functions for assignments: `ast_assignment_get_lhs()`, `ast_assignment_get_rhs()`, `ast_assignment_get_op()`
- [ ] Add accessor functions for statements (if, while, for, etc.)
- [ ] Update IR generator to use accessors instead of direct child indexing

**Tests**: Run test suite after completion

---

## Completion Criteria

- [x] All 34 tests pass after each phase
- [x] Zero strcmp() calls for operator comparison in IR generator (except bitwise which already used enums)
- [x] All operator types represented by enums in AST nodes

---

## Files Modified

- `src/c_grammar.gdl` - Grammar rules (Phase 1, 8)
- `src/c_grammar_ast.h` - New enum types, union fields
- `src/c_grammar_ast_actions.c` - Operator extraction handlers
- `src/llvm_ir_generator.c` - Replace strcmp with enum switch
- `src/ast_helpers.h` (new) - Accessor functions (Phase 9)

---

## Notes

- Per-phase validation required (run `tests/run_tests.sh` after each phase)
- Keep old strcmp as comment initially for reference during implementation
- Follow existing pattern established by `bitwise_operator_type_t`
