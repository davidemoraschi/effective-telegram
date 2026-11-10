#include "sqlite_ops.h"

#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_text(const char *s) {
  size_t n = strlen(s);
  char *d = (char *)malloc(n + 1);
  if (!d) {
    return NULL;
  }
  memcpy(d, s, n + 1);
  return d;
}

static int looks_read_only(const char *sql) {
  while (*sql && isspace((unsigned char)*sql)) {
    sql++;
  }
  char token[16] = {0};
  size_t i = 0;
  while (sql[i] && !isspace((unsigned char)sql[i]) && i + 1 < sizeof(token)) {
    token[i] = (char)toupper((unsigned char)sql[i]);
    i++;
  }
  token[i] = '\0';
  return strcmp(token, "SELECT") == 0 || strcmp(token, "WITH") == 0 || strcmp(token, "PRAGMA") == 0 ||
         strcmp(token, "EXPLAIN") == 0;
}

static char *error_json(const char *message) {
  char *esc = json_escape(message ? message : "Unknown SQLite error");
  if (!esc) {
    return NULL;
  }
  size_t n = strlen(esc) + 32;
  char *out = (char *)malloc(n);
  if (!out) {
    free(esc);
    return NULL;
  }
  snprintf(out, n, "{\"error\":\"%s\"}", esc);
  free(esc);
  return out;
}

static char *run_select(sqlite_ops_t *ops, const char *sql, int limit) {
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(ops->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return error_json(sqlite3_errmsg(ops->db));
  }

  int cols = sqlite3_column_count(stmt);
  size_t cap = 4096;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    sqlite3_finalize(stmt);
    return NULL;
  }
  size_t w = 0;
#define APPEND_FMT(...)                                                                               \
  do {                                                                                                \
    for (;;) {                                                                                        \
      int needed = snprintf(buf + w, cap - w, __VA_ARGS__);                                          \
      if (needed < 0) {                                                                               \
        free(buf);                                                                                     \
        sqlite3_finalize(stmt);                                                                       \
        return NULL;                                                                                   \
      }                                                                                               \
      if ((size_t)needed < cap - w) {                                                                 \
        w += (size_t)needed;                                                                          \
        break;                                                                                        \
      }                                                                                               \
      cap *= 2;                                                                                       \
      char *grown = (char *)realloc(buf, cap);                                                        \
      if (!grown) {                                                                                    \
        free(buf);                                                                                     \
        sqlite3_finalize(stmt);                                                                       \
        return NULL;                                                                                   \
      }                                                                                               \
      buf = grown;                                                                                     \
    }                                                                                                 \
  } while (0)

  APPEND_FMT("{\"columns\":[");
  for (int i = 0; i < cols; i++) {
    char *esc = json_escape(sqlite3_column_name(stmt, i));
    APPEND_FMT("%s\"%s\"", i ? "," : "", esc ? esc : "");
    free(esc);
  }
  APPEND_FMT("],\"rows\":[");

  int count = 0;
  int truncated = 0;
  int first_row = 1;
  for (;;) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      char *err = error_json(sqlite3_errmsg(ops->db));
      free(buf);
      sqlite3_finalize(stmt);
      return err;
    }
    if (count >= limit) {
      truncated = 1;
      break;
    }

    APPEND_FMT("%s[", first_row ? "" : ",");
    first_row = 0;
    for (int c = 0; c < cols; c++) {
      const unsigned char *txt = sqlite3_column_text(stmt, c);
      char *esc = json_escape((const char *)(txt ? txt : (const unsigned char *)""));
      APPEND_FMT("%s\"%s\"", c ? "," : "", esc ? esc : "");
      free(esc);
    }
    APPEND_FMT("]");
    count++;
  }

  APPEND_FMT("],\"rowCount\":%d,\"truncated\":%s}", count, truncated ? "true" : "false");
  sqlite3_finalize(stmt);
  return buf;
}

int sqlite_ops_open(sqlite_ops_t *ops, const char *db_path, int busy_timeout_ms, int max_rows) {
  memset(ops, 0, sizeof(*ops));
  ops->max_rows = max_rows;
  if (sqlite3_open_v2(db_path, &ops->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
    sqlite_ops_close(ops);
    return 0;
  }
  sqlite3_busy_timeout(ops->db, busy_timeout_ms);
  return 1;
}

void sqlite_ops_close(sqlite_ops_t *ops) {
  if (ops && ops->db) {
    sqlite3_close(ops->db);
    ops->db = NULL;
  }
}

char *sqlite_ops_list_tables(sqlite_ops_t *ops) {
  return run_select(ops,
                    "SELECT name, type FROM sqlite_master WHERE type IN ('table','view') AND name NOT LIKE "
                    "'sqlite_%' ORDER BY name",
                    ops->max_rows);
}

char *sqlite_ops_describe_table(sqlite_ops_t *ops, const char *table) {
  char sql[512];
  snprintf(sql, sizeof(sql), "PRAGMA table_info(\"%s\")", table);
  return run_select(ops, sql, ops->max_rows);
}

char *sqlite_ops_query(sqlite_ops_t *ops, const char *sql) {
  if (!looks_read_only(sql)) {
    return error_json("sqlite_query only accepts read-only SQL (SELECT/WITH/PRAGMA/EXPLAIN)");
  }
  return run_select(ops, sql, ops->max_rows);
}

char *sqlite_ops_execute(sqlite_ops_t *ops, const char *sql) {
  if (looks_read_only(sql)) {
    return error_json("sqlite_execute is reserved for write operations");
  }
  char *errmsg = NULL;
  if (sqlite3_exec(ops->db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
    char *out = error_json(errmsg ? errmsg : sqlite3_errmsg(ops->db));
    sqlite3_free(errmsg);
    return out;
  }
  return dup_text("{\"ok\":true}");
}

char *sqlite_ops_get_indexes(sqlite_ops_t *ops, const char *table) {
  char sql[512];
  snprintf(sql, sizeof(sql), "PRAGMA index_list(\"%s\")", table);
  return run_select(ops, sql, ops->max_rows);
}

char *sqlite_ops_get_foreign_keys(sqlite_ops_t *ops, const char *table) {
  char sql[512];
  snprintf(sql, sizeof(sql), "PRAGMA foreign_key_list(\"%s\")", table);
  return run_select(ops, sql, ops->max_rows);
}

char *sqlite_ops_database_info(sqlite_ops_t *ops) {
  (void)ops;
  char sql[] = "SELECT sqlite_version() AS sqlite_version, datetime('now') AS server_time_utc";
  return run_select(ops, sql, 1);
}

char *sqlite_ops_explain_query(sqlite_ops_t *ops, const char *sql) {
  char wrapped[2048];
  snprintf(wrapped, sizeof(wrapped), "EXPLAIN QUERY PLAN %s", sql);
  return run_select(ops, wrapped, ops->max_rows);
}
