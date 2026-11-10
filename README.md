# effective-telegram

Native C MCP server for SQLite over stdio (JSON-RPC 2.0, newline-delimited transport).

## Scope

- Windows-native executable target (`sqlite-mcp.exe`)
- SQLite engine statically embedded via upstream amalgamation (`vendor/sqlite3.c` + `vendor/sqlite3.h`)
- MCP protocol compatibility for:
  - `2026-07-28` (stateless protocol / discovery-first)
  - `2025-11-25`, `2025-06-18`, `2025-03-26`, `2024-11-05` (legacy initialize flow)

## Build (Windows, MSVC)

1. Replace `vendor/sqlite3.c` and `vendor/sqlite3.h` placeholders with the official SQLite amalgamation files.
2. Build:

```bat
build.bat build
```

Output: `build\sqlite-mcp.exe`

## Run

```powershell
sqlite-mcp.exe "C:\Databases\production.db"
```

`stdout` is reserved strictly for MCP responses. Logs/diagnostics are written to `stderr`.
