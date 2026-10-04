/* json.h - 最小 JSON 解析器（零依赖，仅满足本项目配置结构） */
#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    J_NULL,
    J_BOOL,
    J_NUMBER,
    J_STRING,
    J_ARRAY,
    J_OBJECT,
} JsonType;

typedef struct JsonValue JsonValue;
typedef struct {
    char *key;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        double number;
        char *string;
        struct {
            JsonValue **items;
            int count;
        } array;
        struct {
            JsonMember *members;
            int count;
        } object;
    } u;
};

JsonValue *json_parse(const char *text);
JsonValue *json_parse_len(const char *text, size_t len);
void json_free(JsonValue *v);

const JsonValue *json_obj_get(const JsonValue *obj, const char *key);
const char *json_as_string(const JsonValue *v, const char *fallback);
double json_as_number(const JsonValue *v, double fallback);
bool json_as_bool(const JsonValue *v, bool fallback);

/* 构造器：用于序列化上报/保存请求 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} JsonBuf;

void jb_init(JsonBuf *b);
void jb_free(JsonBuf *b);
void jb_raw(JsonBuf *b, const char *s);
void jb_string(JsonBuf *b, const char *s);
void jb_key(JsonBuf *b, const char *key);

#endif
