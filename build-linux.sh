#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build/linux}"
BUILD_TYPE="${2:-Release}"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --parallel

echo "Built $BUILD_DIR/sqlite-mcp"
if [ -f "$BUILD_DIR/sqlite-mcp-tool-tester" ]; then
  echo "Built $BUILD_DIR/sqlite-mcp-tool-tester"
fi
