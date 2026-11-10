#ifndef SQLITE_MCP_MCP_H
#define SQLITE_MCP_MCP_H

#include "sqlite_ops.h"

typedef struct mcp_server mcp_server_t;

mcp_server_t *mcp_server_create(sqlite_ops_t *ops);
void mcp_server_destroy(mcp_server_t *server);
void mcp_server_on_request(mcp_server_t *server, const char *line);

#endif
