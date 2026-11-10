#include "mcp.h"

#include "json.h"
#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#define JSONRPC "2.0"

enum { PROTO_LEGACY, PROTO_MODERN };

typedef struct cancel_id {
  char id[128];
  struct cancel_id *next;
} cancel_id_t;

struct mcp_server {
  sqlite_ops_t *ops;
  worker_pool_t *pool;
#ifdef _WIN32
  CRITICAL_SECTION write_mu;
  CRITICAL_SECTION cancel_mu;
#else
  pthread_mutex_t write_mu;
  pthread_mutex_t cancel_mu;
#endif
  cancel_id_t *canceled;
};

static const char *tools_json =
    "[{\"name\":\"sqlite_list_tables\",\"description\":\"List tables and views\"},"
    "{\"name\":\"sqlite_describe_table\",\"description\":\"Describe columns for a table\"},"
    "{\"name\":\"sqlite_query\",\"description\":\"Run read-only SQL\"},"
    "{\"name\":\"sqlite_execute\",\"description\":\"Run INSERT/UPDATE/DELETE SQL\"},"
    "{\"name\":\"sqlite_get_indexes\",\"description\":\"List indexes for a table\"},"
    "{\"name\":\"sqlite_get_foreign_keys\",\"description\":\"List foreign keys for a table\"},"
    "{\"name\":\"sqlite_database_info\",\"description\":\"Show SQLite metadata\"},"
    "{\"name\":\"sqlite_explain_query\",\"description\":\"Run EXPLAIN QUERY PLAN\"}]";

static char *dup_text(const char *s) {
  size_t n = strlen(s);
  char *d = (char *)malloc(n + 1);
  if (!d) {
    return NULL;
  }
  memcpy(d, s, n + 1);
  return d;
}

static void lock_write(mcp_server_t *s) {
#ifdef _WIN32
  EnterCriticalSection(&s->write_mu);
#else
  pthread_mutex_lock(&s->write_mu);
#endif
}
static void unlock_write(mcp_server_t *s) {
#ifdef _WIN32
  LeaveCriticalSection(&s->write_mu);
#else
  pthread_mutex_unlock(&s->write_mu);
#endif
}

static void lock_cancel(mcp_server_t *s) {
#ifdef _WIN32
  EnterCriticalSection(&s->cancel_mu);
#else
  pthread_mutex_lock(&s->cancel_mu);
#endif
}
static void unlock_cancel(mcp_server_t *s) {
#ifdef _WIN32
  LeaveCriticalSection(&s->cancel_mu);
#else
  pthread_mutex_unlock(&s->cancel_mu);
#endif
}

static int detect_generation(const json_request_t *req) {
  if (strcmp(req->method, "initialize") == 0 || strcmp(req->method, "initialized") == 0) {
    return PROTO_LEGACY;
  }
  if (strcmp(req->protocol_version, "2026-07-28") == 0) {
    return PROTO_MODERN;
  }
  if (strcmp(req->protocol_version, "2025-11-25") == 0 || strcmp(req->protocol_version, "2025-06-18") == 0 ||
      strcmp(req->protocol_version, "2025-03-26") == 0 || strcmp(req->protocol_version, "2024-11-05") == 0) {
    return PROTO_LEGACY;
  }
  return PROTO_MODERN;
}

static void send_result(mcp_server_t *s, const char *id, const char *result_json) {
  lock_write(s);
  fprintf(stdout, "{\"jsonrpc\":\"%s\",\"id\":%s,\"result\":%s}\n", JSONRPC, id, result_json);
  fflush(stdout);
  unlock_write(s);
}

static void send_error(mcp_server_t *s, const char *id, int code, const char *msg) {
  char *esc = json_escape(msg);
  if (!esc) {
    return;
  }
  lock_write(s);
  fprintf(stdout,
          "{\"jsonrpc\":\"%s\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
          JSONRPC, id, code, esc);
  fflush(stdout);
  unlock_write(s);
  free(esc);
}

static int is_canceled(mcp_server_t *s, const char *id) {
  int found = 0;
  lock_cancel(s);
  cancel_id_t **cur = &s->canceled;
  while (*cur) {
    if (strcmp((*cur)->id, id) == 0) {
      cancel_id_t *done = *cur;
      *cur = done->next;
      free(done);
      found = 1;
      break;
    }
    cur = &(*cur)->next;
  }
  unlock_cancel(s);
  return found;
}

static void cancel_request(mcp_server_t *s, const char *id) {
  cancel_id_t *n = (cancel_id_t *)calloc(1, sizeof(*n));
  if (!n) {
    return;
  }
  snprintf(n->id, sizeof(n->id), "%s", id);
  lock_cancel(s);
  n->next = s->canceled;
  s->canceled = n;
  unlock_cancel(s);
}

static char *extract_arg(const char *params_json, const char *arg_name) {
  char *args = json_get_field_json(params_json, "arguments");
  if (!args) {
    return NULL;
  }
  char value[1024];
  if (json_get_string_field(args, arg_name, value, sizeof(value))) {
    free(args);
    return dup_text(value);
  }
  free(args);
  return NULL;
}

static void handle_tools_call(mcp_server_t *s, const json_request_t *req) {
  char name[128] = {0};
  if (!req->params_json || !json_get_string_field(req->params_json, "name", name, sizeof(name))) {
    send_error(s, req->id, -32602, "Missing tool name");
    return;
  }

  char *payload = NULL;
  if (strcmp(name, "sqlite_list_tables") == 0) {
    payload = sqlite_ops_list_tables(s->ops);
  } else if (strcmp(name, "sqlite_describe_table") == 0) {
    char *table = extract_arg(req->params_json, "table");
    if (!table) {
      send_error(s, req->id, -32602, "Missing argument: table");
      return;
    }
    payload = sqlite_ops_describe_table(s->ops, table);
    free(table);
  } else if (strcmp(name, "sqlite_query") == 0) {
    char *sql = extract_arg(req->params_json, "sql");
    if (!sql) {
      send_error(s, req->id, -32602, "Missing argument: sql");
      return;
    }
    payload = sqlite_ops_query(s->ops, sql);
    free(sql);
  } else if (strcmp(name, "sqlite_execute") == 0) {
    char *sql = extract_arg(req->params_json, "sql");
    if (!sql) {
      send_error(s, req->id, -32602, "Missing argument: sql");
      return;
    }
    payload = sqlite_ops_execute(s->ops, sql);
    free(sql);
  } else if (strcmp(name, "sqlite_get_indexes") == 0) {
    char *table = extract_arg(req->params_json, "table");
    if (!table) {
      send_error(s, req->id, -32602, "Missing argument: table");
      return;
    }
    payload = sqlite_ops_get_indexes(s->ops, table);
    free(table);
  } else if (strcmp(name, "sqlite_get_foreign_keys") == 0) {
    char *table = extract_arg(req->params_json, "table");
    if (!table) {
      send_error(s, req->id, -32602, "Missing argument: table");
      return;
    }
    payload = sqlite_ops_get_foreign_keys(s->ops, table);
    free(table);
  } else if (strcmp(name, "sqlite_database_info") == 0) {
    payload = sqlite_ops_database_info(s->ops);
  } else if (strcmp(name, "sqlite_explain_query") == 0) {
    char *sql = extract_arg(req->params_json, "sql");
    if (!sql) {
      send_error(s, req->id, -32602, "Missing argument: sql");
      return;
    }
    payload = sqlite_ops_explain_query(s->ops, sql);
    free(sql);
  } else {
    send_error(s, req->id, -32601, "Unknown tool");
    return;
  }

  if (!payload) {
    send_error(s, req->id, -32000, "Operation failed");
    return;
  }

  char *esc = json_escape(payload);
  if (!esc) {
    free(payload);
    send_error(s, req->id, -32000, "Out of memory");
    return;
  }

  size_t n = strlen(esc) + 128;
  char *result = (char *)malloc(n);
  if (!result) {
    free(esc);
    free(payload);
    send_error(s, req->id, -32000, "Out of memory");
    return;
  }
  snprintf(result, n, "{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}]}", esc);
  send_result(s, req->id, result);
  free(result);
  free(esc);
  free(payload);
}

static void worker_handle(const json_request_t *req, void *ctx) {
  mcp_server_t *s = (mcp_server_t *)ctx;
  if (req->has_id && is_canceled(s, req->id)) {
    send_error(s, req->id, -32800, "Request cancelled");
    return;
  }

  int generation = detect_generation(req);

  if (strcmp(req->method, "initialize") == 0) {
    send_result(s, req->id,
                "{\"protocolVersion\":\"2025-11-25\",\"serverInfo\":{\"name\":\"sqlite-mcp\",\"version\":"
                "\"1.0.0\"},\"capabilities\":{\"tools\":{},\"resources\":{},\"prompts\":{}}}");
    return;
  }
  if (strcmp(req->method, "server/discover") == 0) {
    if (generation == PROTO_MODERN) {
      send_result(s, req->id,
                  "{\"protocolVersion\":\"2026-07-28\",\"serverInfo\":{\"name\":\"sqlite-mcp\",\"version\":"
                  "\"1.0.0\"},\"capabilities\":{\"tools\":{},\"resources\":{},\"prompts\":{}},"
                  "\"supportedProtocolVersions\":[\"2026-07-28\",\"2025-11-25\",\"2025-06-18\","
                  "\"2025-03-26\",\"2024-11-05\"]}");
    } else {
      send_result(s, req->id,
                  "{\"protocolVersion\":\"2025-11-25\",\"serverInfo\":{\"name\":\"sqlite-mcp\",\"version\":"
                  "\"1.0.0\"},\"capabilities\":{\"tools\":{},\"resources\":{},\"prompts\":{}}}");
    }
    return;
  }

  if (strcmp(req->method, "tools/list") == 0) {
    size_t n = strlen(tools_json) + 32;
    char *result = (char *)malloc(n);
    snprintf(result, n, "{\"tools\":%s}", tools_json);
    send_result(s, req->id, result);
    free(result);
    return;
  }

  if (strcmp(req->method, "tools/call") == 0) {
    handle_tools_call(s, req);
    return;
  }

  if (strcmp(req->method, "resources/list") == 0) {
    send_result(s, req->id, "{\"resources\":[]}");
    return;
  }
  if (strcmp(req->method, "resources/read") == 0) {
    send_error(s, req->id, -32601, "No resources exposed");
    return;
  }
  if (strcmp(req->method, "prompts/list") == 0) {
    send_result(s, req->id, "{\"prompts\":[]}");
    return;
  }
  if (strcmp(req->method, "prompts/get") == 0) {
    send_error(s, req->id, -32601, "No prompts exposed");
    return;
  }

  if (req->is_notification) {
    return;
  }
  send_error(s, req->id, -32601, "Method not found");
}

mcp_server_t *mcp_server_create(sqlite_ops_t *ops) {
  mcp_server_t *s = (mcp_server_t *)calloc(1, sizeof(*s));
  if (!s) {
    return NULL;
  }
  s->ops = ops;
#ifdef _WIN32
  InitializeCriticalSection(&s->write_mu);
  InitializeCriticalSection(&s->cancel_mu);
#else
  pthread_mutex_init(&s->write_mu, NULL);
  pthread_mutex_init(&s->cancel_mu, NULL);
#endif
  s->pool = worker_pool_create(4, worker_handle, s);
  if (!s->pool) {
    mcp_server_destroy(s);
    return NULL;
  }
  return s;
}

void mcp_server_destroy(mcp_server_t *s) {
  if (!s) {
    return;
  }
  worker_pool_shutdown(s->pool);

  cancel_id_t *cur = s->canceled;
  while (cur) {
    cancel_id_t *next = cur->next;
    free(cur);
    cur = next;
  }
#ifdef _WIN32
  DeleteCriticalSection(&s->write_mu);
  DeleteCriticalSection(&s->cancel_mu);
#else
  pthread_mutex_destroy(&s->write_mu);
  pthread_mutex_destroy(&s->cancel_mu);
#endif
  free(s);
}

void mcp_server_on_request(mcp_server_t *server, const char *line) {
  json_request_t req;
  if (!json_parse_request(line, &req)) {
    fprintf(stderr, "Invalid JSON-RPC message received\n");
    return;
  }

  if (strcmp(req.method, "$/cancelRequest") == 0) {
    char *id = req.params_json ? json_get_field_json(req.params_json, "id") : NULL;
    if (id) {
      cancel_request(server, id);
      free(id);
    }
    json_free_request(&req);
    return;
  }

  if (req.is_notification && strcmp(req.method, "initialized") == 0) {
    json_free_request(&req);
    return;
  }

  if (!req.has_id && strcmp(req.method, "server/discover") != 0) {
    fprintf(stderr, "Notification received: %s\n", req.method);
  }

  worker_pool_submit(server->pool, &req);
  json_free_request(&req);
}
