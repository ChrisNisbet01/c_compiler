# ncc Executable Linking Plan

## Goal
Add support for producing executables by linking ncc's object file output via the system `cc`.

## CLI Design

| Command | Behavior |
|---------|----------|
| `ncc foo.c` | Produce executable `a.out` |
| `ncc -o foo foo.c` | Produce executable `foo` |
| `ncc -o foo foo.c -lm` | Produce executable, link libm |
| `ncc -o foo foo.c -L/usr/local/lib -lmylib` | Custom lib path |
| `ncc -c foo.c` | Object file only (existing) |
| `ncc -S foo.c` | Assembly only (existing) |

## Implementation

### 1. Add `-l` and `-L` flags (`src/main.c`)
- [x] Add arrays to collect library names and search paths
- [x] Add cases to getopt_long for `-l` and `-L`
- [x] Store values for passing to linker

### 2. Add `link_to_executable()` function (`src/main.c`)
- [x] Compile to temp `.o` file via `emit_to_file`
- [x] Build `cc -no-pie <temp.o> [-l flags] [-L flags] -o <output>` command
- [x] Invoke via `system()`
- [x] Clean up temp file on success
- [x] Handle errors

### 3. Update `generate_output` (`src/main.c`)
- [x] Default mode (no `-S` or `-c`) calls `link_to_executable`

### 4. Update `print_usage` (`src/main.c`)
- [x] Add `-l` and `-L` documentation

### 5. Update test script (`tests/run_tests.sh`)
- [x] Add executable linking test step
- [x] Keep existing `-S -emit-llvm` and `-c` tests

### 6. Run all tests
- [x] All 16 tests pass (`.ll`, `-c`, and linking paths all verified)
