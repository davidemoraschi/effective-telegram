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

Or with explicit build directory/configuration:

```bat
build-windows.bat build\windows Release
```

`build.bat` remains available as a compatibility alias to the same CMake flow.

## Build (Linux)

```bash
./build-linux.sh
```

Or with explicit build directory:

```bash
./build-linux.sh build/linux
```

## Run

Windows:

```powershell
build\windows\Release\sqlite-mcp.exe "C:\Databases\production.db"
```

Linux:

```bash
./build/linux/sqlite-mcp /path/to/production.db
```

`stdout` is reserved strictly for MCP responses. Logs/diagnostics are written to `stderr`.
