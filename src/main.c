#include "mcp.h"
#include "sqlite_ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSY_TIMEOUT_MS 5000
#define QUERY_MAX_ROWS 1000

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <absolute-path-to-sqlite-db>\n", argv[0]);
    return 1;
  }

  sqlite_ops_t ops;
  if (!sqlite_ops_open(&ops, argv[1], BUSY_TIMEOUT_MS, QUERY_MAX_ROWS)) {
    fprintf(stderr, "Failed to open SQLite database: %s\n", argv[1]);
    return 2;
  }

  mcp_server_t *server = mcp_server_create(&ops);
  if (!server) {
    fprintf(stderr, "Failed to initialize MCP server\n");
    sqlite_ops_close(&ops);
    return 3;
  }

  char line[65536];
  while (fgets(line, sizeof(line), stdin) != NULL) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }
    if (n == 0) {
      continue;
    }
    mcp_server_on_request(server, line);
  }

  mcp_server_destroy(server);
  sqlite_ops_close(&ops);
  return 0;
}
