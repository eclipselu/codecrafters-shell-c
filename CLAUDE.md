# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a POSIX-compliant shell implementation in C for the CodeCrafters "Build Your Own Shell" challenge. The shell supports command execution, builtin commands, piping, I/O redirection, and readline-based tab completion and history.

## Build and Run

```sh
# Build and run (requires cmake and vcpkg)
./your_program.sh

# Or manually:
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build ./build
./build/shell
```

Requires the `readline` library (linked via CMakeLists.txt).

## Architecture

**Memory Management**: Uses a custom arena allocator (`arena.h`) with temporary memory scopes via `TempArenaMemory` for per-command allocations that reset after each command execution.

**String Handling**: Custom `String` type (pointer + size, not null-terminated). Uses `StringArray` (dynamic array) for command arguments and `StringList` (linked list) for sequential-only usages like tokenization. See `base_string.h` for operations like `str_equal_cstr`, `str_split`, `str_concat`, `str_array_push`.

**Code Style Conventions** (from `base.h`):
- `global` = file-scoped static
- `internal` = static function
- `local_persist` = function-scoped static

**Command Processing Pipeline**:
1. `tokenize_command()` - Parses input handling quotes and escapes
2. `parse_command()` - Builds `PipedShellCommandList` with redirect info
3. `run_piped_shell_command()` - Executes with pipe/fork management

**Builtin Commands**: `echo`, `pwd`, `cd`, `type`, `exit`, `history` - handled by `run_builtin()` without fork.

## CodeCrafters Submission

```sh
git commit -am "message"
git push origin master
```

## TODO: Code Quality Improvements

1. **Exit code propagation** - `exit 0` or `exit 1` from the user isn't honored; shell always returns 0. Child process exit codes aren't tracked either.

2. **Input redirection** - Only output redirection (`>`, `>>`, `2>`) is implemented. Need to add `<` for stdin redirection.

3. **SIGTSTP handling** - Ctrl-Z is ignored. Full job control (fg, bg, jobs, process groups) would be needed to properly support suspending processes.
