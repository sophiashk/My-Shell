#!/bin/bash

# Test script for mysh
# This script runs various test cases and reports results

echo "=== My Shell Test Suite ==="
echo ""

# Create test files
echo "hello world" > test_input.txt
echo "line1" > test_file1.txt
echo "line2" >> test_file1.txt

# Test counter
TESTS_RUN=0
TESTS_PASSED=0

# Function to run a test
run_test() {
    local test_name="$1"
    local script="$2"
    local expected="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "Test $TESTS_RUN: $test_name ... "
    
    result=$(echo "$script" | ./mysh 2>&1 | grep -v "Welcome" | grep -v "mysh>" | grep -v "exiting")
    
    if [ "$result" = "$expected" ]; then
        echo "PASS"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo "FAIL"
        echo "  Expected: '$expected'"
        echo "  Got: '$result'"
    fi
}

# Basic command tests
echo "--- Basic Command Tests ---"
run_test "Simple echo" "echo hello" "hello"
run_test "Echo with multiple args" "echo hello world" "hello world"
run_test "pwd command" "cd /tmp
pwd" "/tmp"

# Built-in tests
echo ""
echo "--- Built-in Command Tests ---"
run_test "which ls" "which ls" "/bin/ls"
run_test "which with built-in fails" "which cd
echo ok" "ok"

# Redirection tests
echo ""
echo "--- I/O Redirection Tests ---"
run_test "Output redirection" "echo test > test_out.txt
cat test_out.txt" "test"
run_test "Input redirection" "cat < test_input.txt" "hello world"

# Pipeline tests
echo ""
echo "--- Pipeline Tests ---"
run_test "Simple pipeline" "echo hello | cat" "hello"
run_test "Multiple pipes" "echo test | cat | cat" "test"

# Conditional tests
echo ""
echo "--- Conditional Tests ---"
run_test "and after true" "true
and echo success" "success"
run_test "and after false" "false
and echo fail" ""
run_test "or after true" "true
or echo fail" ""
run_test "or after false" "false
or echo success" "success"

# Comment tests
echo ""
echo "--- Comment Tests ---"
run_test "Comment line" "# this is a comment
echo ok" "ok"
run_test "Trailing comment" "echo hello # comment" "hello"

# Cleanup
rm -f test_input.txt test_file1.txt test_out.txt

echo ""
echo "=== Test Summary ==="
echo "Tests run: $TESTS_RUN"
echo "Tests passed: $TESTS_PASSED"
echo "Tests failed: $((TESTS_RUN - TESTS_PASSED))"

if [ $TESTS_RUN -eq $TESTS_PASSED ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi
