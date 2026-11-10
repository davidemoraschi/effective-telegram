#ifndef SQLITE_MCP_SQLITE_OPS_H
#define SQLITE_MCP_SQLITE_OPS_H

#ifdef SQLITE_MCP_USE_SYSTEM_SQLITE
#include <sqlite3.h>
#else
#include "../vendor/sqlite3.h"
#endif

typedef struct {
  sqlite3 *db;
  int max_rows;
} sqlite_ops_t;

int sqlite_ops_open(sqlite_ops_t *ops, const char *db_path, int busy_timeout_ms, int max_rows);
void sqlite_ops_close(sqlite_ops_t *ops);

char *sqlite_ops_list_tables(sqlite_ops_t *ops);
char *sqlite_ops_describe_table(sqlite_ops_t *ops, const char *table);
char *sqlite_ops_query(sqlite_ops_t *ops, const char *sql);
char *sqlite_ops_execute(sqlite_ops_t *ops, const char *sql);
char *sqlite_ops_get_indexes(sqlite_ops_t *ops, const char *table);
char *sqlite_ops_get_foreign_keys(sqlite_ops_t *ops, const char *table);
char *sqlite_ops_database_info(sqlite_ops_t *ops);
char *sqlite_ops_explain_query(sqlite_ops_t *ops, const char *sql);

#endif
