#include "test.h"
#include "json_mini.h"
#include <string.h>
#include <stdlib.h>

static void test_parse_basics(void) {
    JsonNode *r = json_parse("{\"fit\":\"cover\",\"v\":7,\"ok\":true,\"a\":[1,2,3]}");
    CHECK(r && r->type==J_OBJ);
    CHECK(strcmp(json_get_str(r,"fit","x"),"cover")==0);
    CHECK(json_get_num(r,"v",0)==7);
    CHECK(json_get_bool(r,"ok",false)==true);
    const JsonNode *a = json_get(r,"a");
    CHECK(a && a->type==J_ARR && a->count==3);
    CHECK(a->items[2]->num==3);
    json_free(r);
}

static void test_dump_roundtrip(void) {
    JsonNode *o = json_obj();
    obj_set(o,"name",json_str("屏幕 A"));
    obj_set(o,"n",json_num(42));
    JsonNode *arr=json_arr(); arr_push(arr,json_num(1)); arr_push(arr,json_num(2));
    obj_set(o,"crop",arr);
    char *s = json_dump(o);
    JsonNode *p = json_parse(s);
    CHECK(p && strcmp(json_get_str(p,"name",""),"屏幕 A")==0);
    CHECK(json_get_num(p,"n",0)==42);
    CHECK(json_get(p,"crop")->count==2);
    free(s); json_free(o); json_free(p);
}

static void test_unicode_escape(void) {
    JsonNode *r = json_parse("{\"s\":\"\\u4e2d\\u6587\"}");
    CHECK(r && strcmp(json_get_str(r,"s",""),"中文")==0);
    json_free(r);
}

static void test_bad_input(void) {
    CHECK(json_parse("{")==NULL);
    CHECK(json_parse("not json")==NULL);
    CHECK(json_parse("[1,2")==NULL);
}

void test_json_all(void) {
    RUN(test_parse_basics);
    RUN(test_dump_roundtrip);
    RUN(test_unicode_escape);
    RUN(test_bad_input);
}
