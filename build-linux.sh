#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-linux-release}"

cmake --preset "$PRESET"
cmake --build --preset "$PRESET" --parallel

echo "Built out/build/$PRESET/bin/sqlite-mcp"
if [ -f "out/build/$PRESET/bin/sqlite-mcp-tool-tester" ]; then
  echo "Built out/build/$PRESET/bin/sqlite-mcp-tool-tester"
fi
