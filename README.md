NetIDs:
- vk411
- ses390

# My-Shell

## Test Plan
We created an automated test script (test.sh) that runs a series of representative commands through mysh, captures the shell’s output, removes interactive-only messages, and compares the result to an expected value. The script counts how many tests pass or fail and prints a final summary. This makes it easy to detect regressions after modifying the shell.

## Test Coverage
- Basic Commands:
Verifies correct handling of simple external commands (e.g., echoing text, changing directories, printing the working directory). Ensures argument parsing and basic execution function properly.

- Built-in Commands:
Tests all built-ins, including both successful cases and expected failures (e.g., invalid directories for cd, built-in names for which). Confirms correct status codes and behavior.

- Redirection:
Checks input and output redirection using temporary files. Confirms that redirected content matches expectations and that error cases (like missing input files) fail cleanly without crashing the shell.

- Pipelines:
Validates single and multi-stage pipelines. Ensures correct data flow between commands and proper success/failure reporting based on the last command in the pipeline.

- Conditionals (and, or):
Confirms correct control flow based on previous command success or failure, including both cases where the conditional should run and cases where it should be skipped.

- Comments:
Verifies that both full-line comments and trailing comments are ignored and do not affect command execution.

## Cleanup
The script removes all test files it creates, ensuring that repeated runs of the test suite remain clean, isolated, and reproducible.
