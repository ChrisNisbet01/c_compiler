# ncc CLI Improvements Plan

## Goal
Match gcc/clang command-line conventions for output control.

## Target CLI

| Command | Output |
|---------|--------|
| `ncc foo.c` | Error: "Linking not supported yet" |
| `ncc -S foo.c` | `foo.s` (x86-64 assembly) |
| `ncc -S -emit-llvm foo.c` | `foo.ll` (LLVM IR) |
| `ncc -c foo.c` | `foo.o` (object file) |
| `ncc -c -emit-llvm foo.c` | `foo.ll` (LLVM IR) |
| `-o` flag overrides output filename in all cases | |

## Changes

### 1. Default output naming (`src/main.c`)
- [x] Add `derive_output_filename(input, ext)` helper
- [x] Change default `output_filename` to NULL
- [x] Derive output from input filename when `-o` not given
- [x] Pass input filename to `generate_output`

### 2. New flags (`src/main.c`)
- [x] Add `-S` flag (emit assembly)
- [x] Add `--emit-llvm` flag (emit LLVM IR instead of native)
- [x] Default (no `-S` or `-c`) → print "linking not supported" and exit
- [x] Update `print_usage`

### 3. Output mode logic (`src/main.c` generate_output)
- [x] `-S` + `--emit-llvm` → `write_llvm_ir_to_file` (`.ll`)
- [x] `-S` alone → `emit_to_file(..., LLVMAssemblyFile)` (`.s`)
- [x] `-c` + `--emit-llvm` → `write_llvm_ir_to_file` (`.ll`)
- [x] `-c` alone → `emit_to_file(..., LLVMObjectFile)` (`.o`)
- [x] No flags → error message

### 4. `--march` support (`src/llvm_ir_generator.c`)
- [x] Accept `--march` parameter (already works, keep x86-64 as default)

### 5. Update test script (`tests/run_tests.sh`)
- [x] Use `-S -emit-llvm` to get `.ll` files for debugging
- [x] Compile `.ll` with clang to executable
- [x] Run executable and check exit code
- [x] Add `-c` verification step (compile to `.o`)

### 6. Run all tests
- [x] All 16 tests pass (both `.ll` and `-c` paths verified)
