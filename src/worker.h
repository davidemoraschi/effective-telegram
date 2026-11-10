#ifndef SQLITE_MCP_WORKER_H
#define SQLITE_MCP_WORKER_H

#include "json.h"

typedef void (*worker_handler_t)(const json_request_t *req, void *ctx);

typedef struct worker_pool worker_pool_t;

worker_pool_t *worker_pool_create(int thread_count, worker_handler_t handler, void *ctx);
void worker_pool_submit(worker_pool_t *pool, const json_request_t *req);
void worker_pool_shutdown(worker_pool_t *pool);

#endif
