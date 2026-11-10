#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p) {
  while (p && *p && isspace((unsigned char)*p)) {
    p++;
  }
  return p;
}

static const char *find_field(const char *json, const char *field) {
  char pat[160];
  snprintf(pat, sizeof(pat), "\"%s\"", field);
  const char *p = strstr(json, pat);
  if (!p) {
    return NULL;
  }
  p += strlen(pat);
  p = skip_ws(p);
  if (!p || *p != ':') {
    return NULL;
  }
  return skip_ws(p + 1);
}

static char *dup_slice(const char *start, const char *end) {
  if (!start || !end || end < start) {
    return NULL;
  }
  size_t n = (size_t)(end - start);
  char *out = (char *)malloc(n + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, start, n);
  out[n] = '\0';
  return out;
}

static const char *scan_json_value_end(const char *p) {
  int depth_obj = 0;
  int depth_arr = 0;
  int in_str = 0;
  int esc = 0;
  for (; *p; ++p) {
    char c = *p;
    if (in_str) {
      if (esc) {
        esc = 0;
      } else if (c == '\\') {
        esc = 1;
      } else if (c == '"') {
        in_str = 0;
      }
      continue;
    }

    if (c == '"') {
      in_str = 1;
      continue;
    }
    if (c == '{') {
      depth_obj++;
      continue;
    }
    if (c == '}') {
      if (depth_obj == 0 && depth_arr == 0) {
        break;
      }
      depth_obj--;
      if (depth_obj < 0) {
        break;
      }
      continue;
    }
    if (c == '[') {
      depth_arr++;
      continue;
    }
    if (c == ']') {
      depth_arr--;
      if (depth_arr < 0) {
        break;
      }
      continue;
    }
    if ((c == ',' || c == '\n' || c == '\r') && depth_obj == 0 && depth_arr == 0) {
      break;
    }
  }
  return p;
}

int json_get_string_field(const char *json, const char *field, char *out, size_t out_size) {
  const char *p = find_field(json, field);
  if (!p || *p != '"') {
    return 0;
  }
  p++;
  size_t idx = 0;
  int esc = 0;
  while (*p) {
    char c = *p++;
    if (esc) {
      if (idx + 1 < out_size) {
        out[idx++] = c;
      }
      esc = 0;
      continue;
    }
    if (c == '\\') {
      esc = 1;
      continue;
    }
    if (c == '"') {
      break;
    }
    if (idx + 1 < out_size) {
      out[idx++] = c;
    }
  }
  if (out_size > 0) {
    out[idx] = '\0';
  }
  return 1;
}

char *json_get_field_json(const char *json, const char *field) {
  const char *p = find_field(json, field);
  if (!p) {
    return NULL;
  }
  const char *end = scan_json_value_end(p);
  return dup_slice(p, end);
}

int json_parse_request(const char *line, json_request_t *req) {
  memset(req, 0, sizeof(*req));
  req->is_notification = 1;

  if (!json_get_string_field(line, "method", req->method, sizeof(req->method))) {
    return 0;
  }

  char *id_raw = json_get_field_json(line, "id");
  if (id_raw) {
    size_t n = strlen(id_raw);
    while (n > 0 && isspace((unsigned char)id_raw[n - 1])) {
      id_raw[--n] = '\0';
    }
    if (n > 0 && strcmp(id_raw, "null") != 0) {
      snprintf(req->id, sizeof(req->id), "%s", id_raw);
      req->has_id = 1;
      req->is_notification = 0;
    }
    free(id_raw);
  }

  req->params_json = json_get_field_json(line, "params");

  if (req->params_json) {
    if (!json_get_string_field(req->params_json, "protocolVersion", req->protocol_version,
                               sizeof(req->protocol_version))) {
      json_get_string_field(req->params_json, "mcpProtocolVersion", req->protocol_version,
                            sizeof(req->protocol_version));
    }
  }

  if (req->protocol_version[0] == '\0') {
    json_get_string_field(line, "protocolVersion", req->protocol_version,
                          sizeof(req->protocol_version));
  }
  return 1;
}

void json_free_request(json_request_t *req) {
  if (req && req->params_json) {
    free(req->params_json);
    req->params_json = NULL;
  }
}

char *json_escape(const char *text) {
  if (!text) {
    char *empty = (char *)malloc(1);
    if (empty) {
      empty[0] = '\0';
    }
    return empty;
  }

  size_t n = strlen(text);
  size_t cap = n * 2 + 1;
  char *out = (char *)malloc(cap);
  if (!out) {
    return NULL;
  }

  size_t w = 0;
  for (size_t i = 0; i < n; i++) {
    char c = text[i];
    if (c == '"' || c == '\\') {
      out[w++] = '\\';
      out[w++] = c;
    } else if (c == '\n') {
      out[w++] = '\\';
      out[w++] = 'n';
    } else if (c == '\r') {
      out[w++] = '\\';
      out[w++] = 'r';
    } else if (c == '\t') {
      out[w++] = '\\';
      out[w++] = 't';
    } else {
      out[w++] = c;
    }
  }
  out[w] = '\0';
  return out;
}
