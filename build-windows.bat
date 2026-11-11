@echo off
setlocal

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build\windows
set CONFIG=%~2
if "%CONFIG%"=="" set CONFIG=Release

cmake -S . -B "%BUILD_DIR%"
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 exit /b 1

echo Built %BUILD_DIR%\%CONFIG%\sqlite-mcp.exe
if exist "%BUILD_DIR%\%CONFIG%\sqlite-mcp-tool-tester.exe" (
  echo Built %BUILD_DIR%\%CONFIG%\sqlite-mcp-tool-tester.exe
)
