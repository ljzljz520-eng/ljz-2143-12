#include "json_mini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JsonNode *parse_value(const char **p);

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

JsonNode *json_new(JsonType t) {
    JsonNode *n = calloc(1, sizeof(*n));
    if (n) n->type = t;
    return n;
}

JsonNode *json_obj(void) { return json_new(J_OBJ); }
JsonNode *json_arr(void) { return json_new(J_ARR); }
JsonNode *json_bool(bool v) { JsonNode *n = json_new(J_BOOL); n->boolean = v; return n; }
JsonNode *json_num(double v) { JsonNode *n = json_new(J_NUM); n->num = v; return n; }
JsonNode *json_str(const char *s) {
    JsonNode *n = json_new(J_STR);
    n->str = strdup(s ? s : "");
    return n;
}

static void *grow(void *p, size_t n, size_t sz) {
    return realloc(p, (n + 1) * sz);
}

void arr_push(JsonNode *a, JsonNode *v) {
    a->items = grow(a->items, a->count, sizeof(*a->items));
    a->items[a->count++] = v;
}

void obj_set(JsonNode *o, const char *key, JsonNode *v) {
    for (size_t i = 0; i < o->count; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            json_free(o->items[i]);
            o->items[i] = v;
            return;
        }
    }
    o->keys = grow(o->keys, o->count, sizeof(*o->keys));
    o->items = grow(o->items, o->count, sizeof(*o->items));
    o->keys[o->count] = strdup(key);
    o->items[o->count] = v;
    o->count++;
}

static JsonNode *parse_string(const char **p) {
    if (**p != '"') return NULL;
    (*p)++;
    const char *start = *p;
    char *buf = malloc(strlen(start) + 1);
    size_t bi = 0;
    while (**p && **p != '"') {
        char c = *(*p)++;
        if (c == '\\' && **p) {
            char e = *(*p)++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '/': c = '/'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case 'u': {
                    unsigned int code = 0;
                    for (int i = 0; i < 4 && isxdigit((unsigned char)**p); i++) {
                        char h = *(*p)++;
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= h - '0';
                        else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                        else code |= h - 'A' + 10;
                    }
                    if (code < 0x80) { buf[bi++] = (char)code; }
                    else if (code < 0x800) {
                        buf[bi++] = (char)(0xC0 | (code >> 6));
                        buf[bi++] = (char)(0x80 | (code & 0x3F));
                    } else {
                        buf[bi++] = (char)(0xE0 | (code >> 12));
                        buf[bi++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        buf[bi++] = (char)(0x80 | (code & 0x3F));
                    }
                    continue;
                }
                default: c = e; break;
            }
        }
        buf[bi++] = c;
    }
    buf[bi] = 0;
    if (**p != '"') { free(buf); return NULL; }
    (*p)++;
    (void)start;
    JsonNode *n = json_new(J_STR);
    n->str = buf;
    return n;
}

static JsonNode *parse_number(const char **p) {
    char *end;
    double d = strtod(*p, &end);
    if (end == *p) return NULL;
    *p = end;
    return json_num(d);
}

static JsonNode *parse_array(const char **p) {
    (*p)++; /* [ */
    JsonNode *a = json_arr();
    skip_ws(p);
    if (**p == ']') { (*p)++; return a; }
    for (;;) {
        skip_ws(p);
        JsonNode *v = parse_value(p);
        if (!v) { json_free(a); return NULL; }
        arr_push(a, v);
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == ']') { (*p)++; break; }
        json_free(a); return NULL;
    }
    return a;
}

static JsonNode *parse_object(const char **p) {
    (*p)++; /* { */
    JsonNode *o = json_obj();
    skip_ws(p);
    if (**p == '}') { (*p)++; return o; }
    for (;;) {
        skip_ws(p);
        JsonNode *k = parse_string(p);
        if (!k) { json_free(o); return NULL; }
        skip_ws(p);
        if (**p != ':') { json_free(k); json_free(o); return NULL; }
        (*p)++;
        skip_ws(p);
        JsonNode *v = parse_value(p);
        if (!v) { json_free(k); json_free(o); return NULL; }
        o->keys = grow(o->keys, o->count, sizeof(*o->keys));
        o->items = grow(o->items, o->count, sizeof(*o->items));
        o->keys[o->count] = k->str;
        free(k);
        o->items[o->count] = v;
        o->count++;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; break; }
        json_free(o); return NULL;
    }
    return o;
}

static JsonNode *parse_value(const char **p) {
    skip_ws(p);
    switch (**p) {
        case '{': return parse_object(p);
        case '[': return parse_array(p);
        case '"': return parse_string(p);
        case 't':
            if (strncmp(*p, "true", 4) == 0) { *p += 4; return json_bool(true); }
            return NULL;
        case 'f':
            if (strncmp(*p, "false", 5) == 0) { *p += 5; return json_bool(false); }
            return NULL;
        case 'n':
            if (strncmp(*p, "null", 4) == 0) { *p += 4; return json_new(J_NULL); }
            return NULL;
        default:
            if (**p == '-' || isdigit((unsigned char)**p)) return parse_number(p);
            return NULL;
    }
}

JsonNode *json_parse_len(const char *text, size_t len) {
    char *copy = malloc(len + 1);
    memcpy(copy, text, len);
    copy[len] = 0;
    const char *p = copy;
    JsonNode *n = parse_value(&p);
    free(copy);
    return n;
}

JsonNode *json_parse(const char *text) {
    return json_parse_len(text, strlen(text));
}

void json_free(JsonNode *n) {
    if (!n) return;
    if (n->type == J_OBJ || n->type == J_ARR) {
        for (size_t i = 0; i < n->count; i++) {
            json_free(n->items[i]);
            if (n->type == J_OBJ) free(n->keys[i]);
        }
        free(n->items);
        free(n->keys);
    }
    free(n->str);
    free(n);
}

const JsonNode *json_get(const JsonNode *obj, const char *key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (size_t i = 0; i < obj->count; i++)
        if (strcmp(obj->keys[i], key) == 0) return obj->items[i];
    return NULL;
}

const char *json_get_str(const JsonNode *obj, const char *key, const char *def) {
    const JsonNode *v = json_get(obj, key);
    return (v && v->type == J_STR) ? v->str : def;
}

double json_get_num(const JsonNode *obj, const char *key, double def) {
    const JsonNode *v = json_get(obj, key);
    return (v && v->type == J_NUM) ? v->num : def;
}

bool json_get_bool(const JsonNode *obj, const char *key, bool def) {
    const JsonNode *v = json_get(obj, key);
    return (v && v->type == J_BOOL) ? v->boolean : def;
}

typedef struct { char *buf; size_t len, cap; } Sb;

static void sb_put(Sb *s, const char *t) {
    size_t n = strlen(t);
    if (s->len + n + 1 > s->cap) {
        while (s->len + n + 1 > s->cap) s->cap = s->cap ? s->cap * 2 : 256;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, t, n + 1);
    s->len += n;
}

static void sb_putc(Sb *s, char c) {
    if (s->len + 2 > s->cap) {
        while (s->len + 2 > s->cap) s->cap = s->cap ? s->cap * 2 : 256;
        s->buf = realloc(s->buf, s->cap);
    }
    s->buf[s->len++] = c;
    s->buf[s->len] = 0;
}

static void emit_quoted(Sb *s, const char *str) {
    sb_putc(s, '"');
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"': sb_put(s, "\\\""); break;
            case '\\': sb_put(s, "\\\\"); break;
            case '\n': sb_put(s, "\\n"); break;
            case '\r': sb_put(s, "\\r"); break;
            case '\t': sb_put(s, "\\t"); break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    sb_put(s, tmp);
                } else {
                    sb_putc(s, (char)c);
                }
        }
    }
    sb_putc(s, '"');
}

static void dump_into(Sb *s, const JsonNode *n) {
    char numbuf[64];
    switch (n->type) {
        case J_NULL: sb_put(s, "null"); break;
        case J_BOOL: sb_put(s, n->boolean ? "true" : "false"); break;
        case J_NUM:
            if (n->num == (long long)n->num)
                snprintf(numbuf, sizeof(numbuf), "%lld", (long long)n->num);
            else
                snprintf(numbuf, sizeof(numbuf), "%.6g", n->num);
            sb_put(s, numbuf);
            break;
        case J_STR: emit_quoted(s, n->str ? n->str : ""); break;
        case J_ARR:
            sb_putc(s, '[');
            for (size_t i = 0; i < n->count; i++) {
                if (i) sb_putc(s, ',');
                dump_into(s, n->items[i]);
            }
            sb_putc(s, ']');
            break;
        case J_OBJ:
            sb_putc(s, '{');
            for (size_t i = 0; i < n->count; i++) {
                if (i) sb_putc(s, ',');
                emit_quoted(s, n->keys[i]);
                sb_putc(s, ':');
                dump_into(s, n->items[i]);
            }
            sb_putc(s, '}');
            break;
    }
}

char *json_dump(const JsonNode *n) {
    Sb s = {0};
    dump_into(&s, n);
    if (!s.buf) { s.buf = strdup(""); }
    return s.buf;
}
