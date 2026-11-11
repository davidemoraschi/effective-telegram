@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
  echo Usage: build.bat ^<output-dir^>
  exit /b 1
)

set OUTDIR=%~1
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

if not exist vendor\sqlite3.c (
  echo Missing vendor\sqlite3.c
  echo Download the official SQLite amalgamation and place sqlite3.c and sqlite3.h in vendor\
  exit /b 1
)

cl /nologo /std:c17 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fe:"%OUTDIR%\sqlite-mcp.exe" ^
  src\main.c src\mcp.c src\json.c src\sqlite_ops.c src\worker.c vendor\sqlite3.c ^
  /Ivendor

if errorlevel 1 exit /b 1

cl /nologo /std:c17 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fe:"%OUTDIR%\sqlite-mcp-tool-tester.exe" ^
  src\tool_tester.c

if errorlevel 1 exit /b 1

echo Built %OUTDIR%\sqlite-mcp.exe
echo Built %OUTDIR%\sqlite-mcp-tool-tester.exe
