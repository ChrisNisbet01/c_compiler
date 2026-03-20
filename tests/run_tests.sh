#!/bin/bash

# --- Configuration ---

# Default compiler path
DEFAULT_NCC_COMPILER="./build/src/ncc"

# Usage: run_tests.sh [compiler] [test_file]
# If test_file is specified, only that test is run.
NCC_COMPILER="${1:-$DEFAULT_NCC_COMPILER}"
SPECIFIC_TEST="$2"

LLVM_COMPILER="clang"

# Get the directory where the script is located
SCRIPT_DIR=$(dirname "$0")
TEST_DIR="$SCRIPT_DIR"
OUTPUT_DIR="$TEST_DIR/output"

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

# Flag to indicate if any test failed
TEST_FAILED=false

run_test() {
    local c_file="$1"
    local base_name=$(basename "$c_file" .c)
    local ll_file="$OUTPUT_DIR/${base_name}.ll"
    local o_file="$OUTPUT_DIR/${base_name}.o"
    local exe_file="$OUTPUT_DIR/${base_name}"
    local err_file="$OUTPUT_DIR/${base_name}.err"
    local out_file="$OUTPUT_DIR/${base_name}.out"

    echo ""
    echo "--- Testing: $c_file ---"

    # Clean up previous output
    rm -f "$ll_file" "$o_file" "$exe_file" "$err_file"

    # 1. Compile C file to LLVM IR using ncc (-S -emit-llvm)
    echo "  [NCC] Compiling $c_file -> $ll_file"
    if ! "$NCC_COMPILER" -S --emit-llvm -o "$ll_file" "$c_file" 1> "$out_file" 2> "$err_file"; then
        echo "  ERROR: ncc compilation failed for $c_file. Check $err_file"
        TEST_FAILED=true
        return
    fi
    if [ -s "$err_file" ]; then
        echo "  WARNING: ncc produced output/warnings for $c_file. Check $err_file"
    fi

    # 2. Compile LLVM IR to an executable using clang
    echo "  [CLANG] Compiling $ll_file -> $exe_file"
    if ! "$LLVM_COMPILER" "$ll_file" -o "$exe_file" 2> "$err_file"; then
        echo "  ERROR: clang compilation failed for $ll_file. Check $err_file"
        TEST_FAILED=true
        return
    fi
    if [ -s "$err_file" ]; then
        echo "  WARNING: clang produced warnings for $ll_file. Check $err_file"
    fi

    # 3. Run the executable and capture output and exit code
    echo "  [RUN] Executing $exe_file"
    local exec_output_file="$OUTPUT_DIR/${base_name}_exec.log"
    "$exe_file" > "$exec_output_file" 2>&1
    local exit_code=$?

    if [ "$exit_code" -ne 0 ] ; then
        echo "  FAILURE: $c_file execution failed with exit code $exit_code."
        TEST_FAILED=true
    else
        echo "  SUCCESS: $c_file executed successfully with exit code $exit_code."
        if [ -s "$exec_output_file" ]; then
            echo "  Output:"
            cat "$exec_output_file"
        fi
    fi

    # 4. Verify -c flag produces object file
    echo "  [NCC -c] Compiling $c_file -> $o_file"
    if ! "$NCC_COMPILER" -c -o "$o_file" "$c_file" 1> /dev/null 2> "$err_file"; then
        echo "  ERROR: ncc -c compilation failed for $c_file. Check $err_file"
        TEST_FAILED=true
    elif [ ! -f "$o_file" ]; then
        echo "  ERROR: ncc -c did not produce object file $o_file"
        TEST_FAILED=true
    else
        echo "  SUCCESS: ncc -c produced object file $o_file."
    fi

    # 5. Verify default mode produces and links executable
    local link_exe="$OUTPUT_DIR/${base_name}_link"
    echo "  [NCC] Linking $c_file -> $link_exe"
    if ! "$NCC_COMPILER" -o "$link_exe" "$c_file" 1> /dev/null 2> "$err_file"; then
        echo "  ERROR: ncc linking failed for $c_file. Check $err_file"
        TEST_FAILED=true
    elif [ ! -f "$link_exe" ]; then
        echo "  ERROR: ncc did not produce executable $link_exe"
        TEST_FAILED=true
    else
        echo "  [RUN] Executing $link_exe"
        local link_exec_output="$OUTPUT_DIR/${base_name}_link_exec.log"
        "$link_exe" > "$link_exec_output" 2>&1
        local link_exit_code=$?
        if [ "$link_exit_code" -ne 0 ]; then
            echo "  FAILURE: linked executable failed with exit code $link_exit_code."
            TEST_FAILED=true
        else
            echo "  SUCCESS: linked executable ran successfully."
        fi
    fi
}

echo "--- Starting Compiler Test Suite ---"
echo "Compiler: $NCC_COMPILER"
echo "Test directory: $TEST_DIR"
echo "Output directory: $OUTPUT_DIR"

if [ -n "$SPECIFIC_TEST" ]; then
    # Run a single specific test
    if [ ! -f "$SPECIFIC_TEST" ] && [ -f "$TEST_DIR/$SPECIFIC_TEST" ]; then
        SPECIFIC_TEST="$TEST_DIR/$SPECIFIC_TEST"
    fi
    if [ ! -f "$SPECIFIC_TEST" ]; then
        echo "Error: Test file not found: $SPECIFIC_TEST"
        exit 1
    fi
    run_test "$SPECIFIC_TEST"
else
    # Run all tests
    while IFS= read -r -d $'\0' c_file; do
        run_test "$c_file"
    done < <(find "$TEST_DIR" -maxdepth 1 -name "*.c" -print0)
fi

echo ""
echo "--- Test Suite Summary ---"
if [ "$TEST_FAILED" = "true" ]; then
    echo "Some tests failed."
    exit 1
else
    echo "All tests passed!"
    exit 0
fi
