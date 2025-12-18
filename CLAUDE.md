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

**String Handling**: Custom `String` type (pointer + size, not null-terminated) with linked list `StringList` for tokenized command arguments. See `base_string.h` for operations like `str_equal_cstr`, `str_split`, `str_concat`.

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
