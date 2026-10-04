#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
    int error;
} JParser;

static JsonValue *alloc_value(JsonType type) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    if (v) v->type = type;
    return v;
}

void json_free(JsonValue *v) {
    if (v == NULL) return;
    if (v->type == J_STRING) {
        free(v->u.string);
    } else if (v->type == J_ARRAY) {
        for (int i = 0; i < v->u.array.count; i++) json_free(v->u.array.items[i]);
        free(v->u.array.items);
    } else if (v->type == J_OBJECT) {
        for (int i = 0; i < v->u.object.count; i++) {
            free(v->u.object.members[i].key);
            json_free(v->u.object.members[i].value);
        }
        free(v->u.object.members);
    }
    free(v);
}

static void skip_ws(JParser *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ps->p++;
        } else {
            break;
        }
    }
}

static JsonValue *parse_value(JParser *ps);

static char *parse_string_raw(JParser *ps) {
    if (ps->p >= ps->end || *ps->p != '"') {
        ps->error = 1;
        return NULL;
    }
    ps->p++;
    size_t cap = 16, len = 0;
    char *out = malloc(cap);
    if (!out) { ps->error = 1; return NULL; }
    while (ps->p < ps->end && *ps->p != '"') {
        char c = *ps->p++;
        if (c == '\\') {
            if (ps->p >= ps->end) { ps->error = 1; break; }
            char e = *ps->p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'u': {
                    if (ps->end - ps->p < 4) { ps->error = 1; break; }
                    unsigned code = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *ps->p++;
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                        else ps->error = 1;
                    }
                    /* UTF-8 编码 */
                    char u[5];
                    int n = 0;
                    if (code < 0x80) {
                        u[0] = (char)code; n = 1;
                    } else if (code < 0x800) {
                        u[0] = (char)(0xC0 | (code >> 6));
                        u[1] = (char)(0x80 | (code & 0x3F));
                        n = 2;
                    } else {
                        u[0] = (char)(0xE0 | (code >> 12));
                        u[1] = (char)(0x80 | ((code >> 6) & 0x3F));
                        u[2] = (char)(0x80 | (code & 0x3F));
                        n = 3;
                    }
                    while (n--) {
                        if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                        out[len++] = u[n];
                    }
                    continue;
                }
                default:
                    ps->error = 1;
                    c = e;
                    break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        out[len++] = c;
    }
    if (ps->p >= ps->end || *ps->p != '"') {
        free(out);
        ps->error = 1;
        return NULL;
    }
    ps->p++;
    out[len] = '\0';
    return out;
}

static JsonValue *parse_string(JParser *ps) {
    char *s = parse_string_raw(ps);
    if (s == NULL) return NULL;
    JsonValue *v = alloc_value(J_STRING);
    v->u.string = s;
    return v;
}

static JsonValue *parse_number(JParser *ps) {
    char *endp = NULL;
    double d = strtod(ps->p, &endp);
    if (endp == ps->p) { ps->error = 1; return NULL; }
    ps->p = endp;
    JsonValue *v = alloc_value(J_NUMBER);
    v->u.number = d;
    return v;
}

static JsonValue *parse_literal(JParser *ps, const char *lit, JsonType type) {
    size_t n = strlen(lit);
    if ((size_t)(ps->end - ps->p) < n || strncmp(ps->p, lit, n) != 0) {
        ps->error = 1;
        return NULL;
    }
    ps->p += n;
    JsonValue *v = alloc_value(type);
    if (type == J_BOOL) v->u.boolean = (lit[0] == 't');
    return v;
}

static JsonValue *parse_array(JParser *ps) {
    ps->p++; /* [ */
    JsonValue *v = alloc_value(J_ARRAY);
    int cap = 0;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return v; }
    for (;;) {
        skip_ws(ps);
        JsonValue *item = parse_value(ps);
        if (!item) { json_free(v); return NULL; }
        if (v->u.array.count == cap) {
            cap = cap ? cap * 2 : 4;
            v->u.array.items = realloc(v->u.array.items,
                                       (size_t)cap * sizeof(JsonValue *));
        }
        v->u.array.items[v->u.array.count++] = item;
        skip_ws(ps);
        if (ps->p >= ps->end) { ps->error = 1; json_free(v); return NULL; }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; break; }
        ps->error = 1; json_free(v); return NULL;
    }
    return v;
}

static JsonValue *parse_object(JParser *ps) {
    ps->p++; /* { */
    JsonValue *v = alloc_value(J_OBJECT);
    int cap = 0;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return v; }
    for (;;) {
        skip_ws(ps);
        char *key = parse_string_raw(ps);
        if (!key) { json_free(v); return NULL; }
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            free(key); ps->error = 1; json_free(v); return NULL;
        }
        ps->p++;
        skip_ws(ps);
        JsonValue *val = parse_value(ps);
        if (!val) { free(key); json_free(v); return NULL; }
        if (v->u.object.count == cap) {
            cap = cap ? cap * 2 : 4;
            v->u.object.members = realloc(v->u.object.members,
                                          (size_t)cap * sizeof(JsonMember));
        }
        v->u.object.members[v->u.object.count].key = key;
        v->u.object.members[v->u.object.count].value = val;
        v->u.object.count++;
        skip_ws(ps);
        if (ps->p >= ps->end) { ps->error = 1; json_free(v); return NULL; }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; break; }
        ps->error = 1; json_free(v); return NULL;
    }
    return v;
}

static JsonValue *parse_value(JParser *ps) {
    skip_ws(ps);
    if (ps->p >= ps->end) { ps->error = 1; return NULL; }
    char c = *ps->p;
    if (c == '"') return parse_string(ps);
    if (c == '{') return parse_object(ps);
    if (c == '[') return parse_array(ps);
    if (c == 't') return parse_literal(ps, "true", J_BOOL);
    if (c == 'f') return parse_literal(ps, "false", J_BOOL);
    if (c == 'n') return parse_literal(ps, "null", J_NULL);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(ps);
    ps->error = 1;
    return NULL;
}

JsonValue *json_parse_len(const char *text, size_t len) {
    JParser ps = {text, text + len, 0};
    JsonValue *v = parse_value(&ps);
    if (ps.error) {
        json_free(v);
        return NULL;
    }
    return v;
}

JsonValue *json_parse(const char *text) {
    return json_parse_len(text, strlen(text));
}

const JsonValue *json_obj_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != J_OBJECT) return NULL;
    for (int i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

const char *json_as_string(const JsonValue *v, const char *fallback) {
    return (v && v->type == J_STRING) ? v->u.string : fallback;
}

double json_as_number(const JsonValue *v, double fallback) {
    return (v && v->type == J_NUMBER) ? v->u.number : fallback;
}

bool json_as_bool(const JsonValue *v, bool fallback) {
    return (v && v->type == J_BOOL) ? v->u.boolean : fallback;
}

/* ---------- 序列化 ---------- */

static void ensure(JsonBuf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 128;
        b->data = realloc(b->data, b->cap);
    }
}

void jb_init(JsonBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void jb_free(JsonBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void jb_raw(JsonBuf *b, const char *s) {
    size_t n = strlen(s);
    ensure(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void jb_string(JsonBuf *b, const char *s) {
    ensure(b, 2);
    b->data[b->len++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[8];
        const char *e = NULL;
        switch (*p) {
            case '"': e = "\\\""; break;
            case '\\': e = "\\\\"; break;
            case '\n': e = "\\n"; break;
            case '\r': e = "\\r"; break;
            case '\t': e = "\\t"; break;
            default: break;
        }
        if (e) {
            ensure(b, 2);
            memcpy(b->data + b->len, e, 2);
            b->len += 2;
        } else if (*p < 0x20) {
            int n = snprintf(esc, sizeof(esc), "\\u%04x", *p);
            ensure(b, (size_t)n);
            memcpy(b->data + b->len, esc, (size_t)n);
            b->len += (size_t)n;
        } else {
            ensure(b, 1);
            b->data[b->len++] = (char)*p;
        }
    }
    ensure(b, 1);
    b->data[b->len++] = '"';
    b->data[b->len] = '\0';
}

void jb_key(JsonBuf *b, const char *key) {
    jb_string(b, key);
    jb_raw(b, ":");
}
