---
name: cross-compilation
description: Standards and required workflow for cross-compiling this repository on Windows and Linux. Use this skill whenever a task involves build system changes, C/C++ compiler settings, platform-specific code paths, CMake presets/scripts, output-path consistency, or release/debug build reproducibility across operating systems.
---

# effective-telegram cross-compilation standard

## Goal
Keep one shared codebase that builds reproducibly on Windows and Linux, with platform-specific behavior isolated behind conditional compilation and common logic kept identical.

## Mandatory requirements
1. **One common code path first**
   - Keep shared business logic in common source files.
   - Isolate OS-specific code to small `#ifdef _WIN32` / `#else` blocks.
   - Do not duplicate full functions unless platform APIs require it.

2. **Consistent build entrypoints**
   - Use CMake presets as the source of truth.
   - Script wrappers (`build.bat`, `build-windows.bat`, `build-linux.sh`) must call `cmake --preset` and `cmake --build --preset`.

3. **Standardized artifact layout**
   - Build outputs must resolve to:
	 - `out/build/<preset>/bin/sqlite-mcp[.exe]`
	 - `out/build/<preset>/bin/sqlite-mcp-tool-tester[.exe]`

4. **Line-ending discipline by script type**
   - `*.sh`, `*.bash` => LF
   - `*.bat`, `*.cmd` => CRLF
   - Enforce via `.gitattributes`.

5. **No generated artifacts in Git**
   - Ignore `out/`, `build/`, `CMakeFiles/`, `CMakeCache.txt`, `cmake_install.cmake`, `compile_commands.json`.

## C/C++ language standard policy
Use the **latest stable standard available on the target OS/toolchain**, with automatic fallback only when unsupported:

- **C targets:** prefer `C23`, fallback to `C17`
- **C++ targets (if introduced):** prefer `C++23`, fallback to `C++20`

Implementation guidance:
- Detect support per compiler/toolchain and set the highest supported standard.
- Keep the selected standard explicit in CMake configure output.
- Do not silently downgrade below these fallbacks.

## Current preset matrix
- Windows: `x64-debug`, `x64-release`
- Linux: `linux-debug`, `linux-release`

## Acceptance checklist for changes
- [ ] Builds succeed for Windows and Linux presets.
- [ ] Executable locations are identical in structure across environments.
- [ ] OS-specific instructions are conditionally compiled only where needed.
- [ ] Shared logic remains unchanged unless functionally required.
- [ ] README/build docs match real paths and commands.
- [ ] `.sh` files use LF and run without shebang errors.
