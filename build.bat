@echo off
setlocal

set PRESET=%~1
if "%PRESET%"=="" set PRESET=x64-release

cmake --preset "%PRESET%"
if errorlevel 1 exit /b 1

cmake --build --preset "%PRESET%" --parallel
if errorlevel 1 exit /b 1

echo Built out\build\%PRESET%\bin\sqlite-mcp.exe
if exist "out\build\%PRESET%\bin\sqlite-mcp-tool-tester.exe" (
  echo Built out\build\%PRESET%\bin\sqlite-mcp-tool-tester.exe
)
