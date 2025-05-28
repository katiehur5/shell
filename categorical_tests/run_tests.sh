#!/bin/bash

# Make first
make

# Edit this with your netid
NETID=ksh48

# Path to the directory containing the test input files
TEST_DIR="/home/accts/$NETID/cs323/proj4/categorical_tests"

# Paths to the executables
MY_EXEC="/home/accts/$NETID/cs323/proj4/Bash" # Change this to the path of your executable
REFERENCE_EXEC="/c/cs323/proj4/Bash" # Path to the reference executable

# Output directory for storing test results
OUTPUT_DIR="$TEST_DIR/output"
mkdir -p "$OUTPUT_DIR"

# Log file for all Valgrind errors across tests
VALGRIND_ERROR_LOG="$OUTPUT_DIR/valgrind_errors.log"
> "$VALGRIND_ERROR_LOG"  # Clear previous contents if any

# Initialize category scores and error tracking
declare -A scores
declare -A total_tests
declare -A valgrind_errors
declare -A error_message_failures

# Determine which test cases to run
if [ "$#" -gt 0 ]; then
    # If arguments are provided, use those as specific test cases
    TEST_FILES=()
    for base_name in "$@"; do
        TEST_FILES+=("$TEST_DIR/${base_name}.in")
    done
else
    # Otherwise, run all .in files in the directory
    TEST_FILES=("$TEST_DIR"/*.in)
fi

# Iterate over each specified input file
for input_file in "${TEST_FILES[@]}"; do
    # Get the base filename without the directory or extension
    base_name=$(basename "$input_file" .in)

    # Determine category by extracting the prefix from the base name (e.g., 'and', 'bg')
    category=$(echo "$base_name" | sed 's/[0-9]*$//')
    total_tests["$category"]=$((total_tests["$category"] + 1))

    # Define output file names for both executables
    my_output_file="$OUTPUT_DIR/${base_name}_my.out"
    ref_output_file="$OUTPUT_DIR/${base_name}_ref.out"
    my_error_file="$OUTPUT_DIR/${base_name}_my.err"
    ref_error_file="$OUTPUT_DIR/${base_name}_ref.err"
    valgrind_output_file="$OUTPUT_DIR/${base_name}_valgrind.out"

    # Clear previous content in output and error files before each test
    > "$my_output_file"
    > "$ref_output_file"
    > "$my_error_file"
    > "$ref_error_file"

    # Run both executables with the test input and save the outputs and errors
    "$MY_EXEC" < "$input_file" > "$my_output_file" 2> "$my_error_file"
    my_exec_status=$?

    "$REFERENCE_EXEC" < "$input_file" > "$ref_output_file" 2> "$ref_error_file"
    ref_exec_status=$?

    # Compare standard outputs
    if diff -q "$my_output_file" "$ref_output_file" > /dev/null; then
        echo "Test $base_name (Output): PASS"
        scores["$category"]=$((scores["$category"] + 1))
    else
        echo "Test $base_name (Output): FAIL"
        diff "$my_output_file" "$ref_output_file"
    fi

    # Compare standard error outputs by line count without altering the status
    my_error_line_count=$(wc -l < "$my_error_file")
    ref_error_line_count=$(wc -l < "$ref_error_file")

    if [ "$my_error_line_count" -eq "$ref_error_line_count" ]; then
        echo "Test $base_name (Error Line Count): PASS"
    else
        echo "Test $base_name (Error Line Count): FAIL - Expected $ref_error_line_count lines, got $my_error_line_count"
        error_message_failures["$category"]=$((error_message_failures["$category"] + 1))
    fi

    # Run Valgrind on my executable with the same input and check for memory issues
    valgrind --leak-check=full --error-exitcode=1 --log-file="$valgrind_output_file" "$MY_EXEC" < "$input_file" > /dev/null 2>&1
    valgrind_status=$?
    if [ "$valgrind_status" -eq 0 ]; then
        echo "Valgrind Check for $base_name: PASS"
    else
        echo "Valgrind Check for $base_name: FAIL"
        valgrind_errors["$category"]=$((valgrind_errors["$category"] + 1))

        # Append the Valgrind output to the centralized error log file
        echo -e "\nValgrind Errors for $base_name:" >> "$VALGRIND_ERROR_LOG"
        cat "$valgrind_output_file" >> "$VALGRIND_ERROR_LOG"
    fi
done

# Display the scores for each category
echo -e "\nCategory Scores:"
for category in "${!total_tests[@]}"; do
    passed=${scores["$category"]}
    total=${total_tests["$category"]}
    valgrind_failures=${valgrind_errors["$category"]:-0} # Default to 0 if no Valgrind errors
    error_failures=${error_message_failures["$category"]:-0} # Default to 0 if no error message failures
    echo "$category: $passed/$total (Valgrind Failures: $valgrind_failures, Error Line Count Failures: $error_failures)"
done

echo -e "\nDetailed Valgrind errors have been saved to $VALGRIND_ERROR_LOG"

# Calculate overall summary
total_passed=0
total_tests_run=0
total_valgrind_failures=0
total_error_failures=0

for category in "${!total_tests[@]}"; do
    total_passed=$((total_passed + scores["$category"]))
    total_tests_run=$((total_tests_run + total_tests["$category"]))
    total_valgrind_failures=$((total_valgrind_failures + valgrind_errors["$category"]))
    total_error_failures=$((total_error_failures + error_message_failures["$category"]))
done

# Print summary
echo -e "\nSummary: Passed $total_passed/$total_tests_run tests, with $total_valgrind_failures Valgrind failures and $total_error_failures error line count mismatches."
