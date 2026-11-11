@echo off
setlocal

set PRESET=%~1
if "%PRESET%"=="" set PRESET=x64-release
set CONFIG=%~2
if "%CONFIG%"=="" set CONFIG=Release

cmake --preset "%PRESET%"
if errorlevel 1 exit /b 1

cmake --build --preset "%PRESET%" --parallel
if errorlevel 1 exit /b 1

echo Built out\build\%PRESET%\bin\%CONFIG%\sqlite-mcp.exe
if exist "out\build\%PRESET%\bin\%CONFIG%\sqlite-mcp-tool-tester.exe" (
  echo Built out\build\%PRESET%\bin\%CONFIG%\sqlite-mcp-tool-tester.exe
)
