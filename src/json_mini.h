#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ
} JsonType;

typedef struct JsonNode JsonNode;
struct JsonNode {
    JsonType type;
    char *str;            /* J_STR 文本（已反转义），或对象键 */
    double num;
    bool boolean;
    JsonNode **items;
    size_t count;
    char **keys;          /* J_OBJ: 与 items 平行 */
};

JsonNode *json_parse(const char *text);
JsonNode *json_parse_len(const char *text, size_t len);
void json_free(JsonNode *n);

const JsonNode *json_get(const JsonNode *obj, const char *key);
const char *json_get_str(const JsonNode *obj, const char *key, const char *def);
double json_get_num(const JsonNode *obj, const char *key, double def);
bool json_get_bool(const JsonNode *obj, const char *key, bool def);

/* 构造辅助（用于报告序列化） */
JsonNode *json_new(JsonType t);
JsonNode *json_obj(void);
JsonNode *json_arr(void);
void obj_set(JsonNode *o, const char *key, JsonNode *v); /* 接管 v */
void arr_push(JsonNode *a, JsonNode *v);
JsonNode *json_str(const char *s);
JsonNode *json_num(double v);
JsonNode *json_bool(bool v);

/* 序列化为新 malloc 的字符串 */
char *json_dump(const JsonNode *n);

#endif
