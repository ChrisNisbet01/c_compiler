#!/bin/bash

# --- Configuration ---

# Default compiler path
DEFAULT_NCC_COMPILER="./build/src/ncc"
NCC_COMPILER="${1:-$DEFAULT_NCC_COMPILER}" # Use provided argument or default

LLVM_COMPILER="clang"

# Get the directory where the script is located
# This ensures tests are found relative to the script's location.
SCRIPT_DIR=$(dirname "$0")

# Test files are assumed to be in the same directory as the script.
TEST_DIR="$SCRIPT_DIR"

# Output directory is 'output' located below the test files directory.
OUTPUT_DIR="$TEST_DIR/output"

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

# Flag to indicate if any test failed
TEST_FAILED=false

echo "--- Starting Compiler Test Suite ---"
echo "Compiler: $NCC_COMPILER"
echo "Test directory: $TEST_DIR"
echo "Output directory: $OUTPUT_DIR"

# Find all .c files in the test directory (only direct children, not subdirectories).
# Using find ... -print0 and while IFS= read -r -d $'\0' is robust for filenames with spaces or special characters.
while IFS= read -r -d $'\0' c_file; do
    echo ""
    echo "--- Testing: $c_file ---"

    base_name=$(basename "$c_file" .c)
    ll_file="$OUTPUT_DIR/${base_name}.ll"
    o_name="$OUTPUT_DIR/${base_name}"
    exe_file="$OUTPUT_DIR/${base_name}"
    err_file="$OUTPUT_DIR/${base_name}.err"
    out_file="$OUTPUT_DIR/${base_name}.out"


    # Clean up previous output for this test file
    rm -f "$ll_file" "$exe_file" "$err_file"

    # 1. Compile C file to LLVM IR using ncc
    echo "  [NCC] Compiling $c_file -> $ll_file"
    # Redirect stderr to a file for detailed error checking
    if ! "$NCC_COMPILER" -o "$o_name" "$c_file" 1> "$out_file" 2> "$err_file"; then
        echo "  ERROR: ncc compilation failed for $c_file. Check $err_file"
        TEST_FAILED=true
        continue # Move to the next test file
    fi
    # Check if stderr file is not empty (indicates warnings/errors from ncc)
    if [ -s "$err_file" ]; then
        echo "  WARNING: ncc produced output/warnings for $c_file. Check $err_file"
    fi

    # 2. Compile LLVM IR to an executable using clang
    echo "  [CLANG] Compiling $ll_file -> $exe_file"
    # Redirect stderr to a file for detailed error checking
    if ! "$LLVM_COMPILER" "$ll_file" -o "$exe_file" 2> "$err_file"; then
        echo "  ERROR: clang compilation failed for $ll_file. Check $err_file"
        TEST_FAILED=true
        continue
    fi
    # Check if stderr file is not empty
    if [ -s "$err_file" ]; then
        echo "  WARNING: clang produced warnings for $ll_file. Check $err_file"
    fi

    # 3. Run the executable and capture output and exit code
    echo "  [RUN] Executing $exe_file"
    # Redirect stdout and stderr of the executable to a temporary file, then capture exit code
    exec_output_file="$OUTPUT_DIR/${base_name}_exec.log"
    "$exe_file" > "$exec_output_file" 2>&1
    exit_code=$?

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
done < <(find "$TEST_DIR" -maxdepth 1 -name "*.c" -print0)

echo ""
echo "--- Test Suite Summary ---"
if [ "$TEST_FAILED" = "true" ]; then
    echo "Some tests failed."
    exit 1
else
    echo "All tests passed!"
    exit 0
fi
