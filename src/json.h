#ifndef SQLITE_MCP_JSON_H
#define SQLITE_MCP_JSON_H

#include <stddef.h>

typedef struct {
  char method[128];
  char id[128];
  int has_id;
  int is_notification;
  char protocol_version[32];
  char *params_json;
} json_request_t;

int json_parse_request(const char *line, json_request_t *req);
void json_free_request(json_request_t *req);
char *json_escape(const char *text);
int json_get_string_field(const char *json, const char *field, char *out, size_t out_size);
char *json_get_field_json(const char *json, const char *field);

#endif
