# effective-telegram

Native C MCP server for SQLite over stdio (JSON-RPC 2.0, newline-delimited transport).

## Scope

- Cross-platform build target for Windows and Linux (`sqlite-mcp` / `sqlite-mcp.exe`)
- SQLite engine statically embedded via upstream amalgamation (`vendor/sqlite3.c` + `vendor/sqlite3.h`)
- MCP protocol compatibility for:
  - `2026-07-28` (stateless protocol / discovery-first)
  - `2025-11-25`, `2025-06-18`, `2025-03-26`, `2024-11-05` (legacy initialize flow)

## Build prerequisites

1. CMake 3.20+
2. A C17-capable compiler
3. `vendor/sqlite3.c` and `vendor/sqlite3.h` from official SQLite amalgamation

## Build (Windows)

```bat
build-windows.bat
```

Or with explicit preset:

```bat
build-windows.bat x64-release
```

`build.bat` remains available as a compatibility alias to the same CMake preset flow.

## Build (Linux)

```bash
./build-linux.sh
```

Or with explicit preset:

```bash
./build-linux.sh linux-release
```

## CI (Linux compile check)

GitHub Actions validates Linux compilation on every PR and push to `main`:

- Workflow: `.github/workflows/linux-compile.yml`
- Matrix: `gcc` and `clang`
- Presets: `linux-debug` and `linux-release`

## Output location

Visual Studio and batch/script builds share the same build-root convention via CMake presets:

- `out/build/<preset>/bin/sqlite-mcp[.exe]`
- `out/build/<preset>/bin/sqlite-mcp-tool-tester[.exe]`

Examples:
- Windows release preset: `out/build/x64-release/bin/...`
- Linux release preset: `out/build/linux-release/bin/...`

## Run

Windows:

```powershell
out\build\x64-release\bin\sqlite-mcp.exe "C:\Databases\production.db"
```

Linux:

```bash
./out/build/linux-release/bin/sqlite-mcp /path/to/production.db
```

`stdout` is reserved strictly for MCP responses. Logs/diagnostics are written to `stderr`.
