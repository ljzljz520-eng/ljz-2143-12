#include "test.h"
#include "layout.h"
#include <string.h>

static void test_defaults_and_parse(void) {
    LayoutConfig c; layout_set_defaults(&c);
    CHECK(strcmp(c.fit,"cover")==0);
    CHECK(c.version==1 && c.image_rev==1);
    const char *body = "{\"data\":{\"version\":9,\"image_rev\":3,\"fit\":\"contain\","
                       "\"focus\":[0.2,0.8],\"orientation\":90,"
                       "\"texture_strategy\":\"runtime\",\"budget_bytes_mb\":8,"
                       "\"budget_entries\":4}}";
    char err[128]={0};
    CHECK(layout_parse(&c, body, err, sizeof(err)));
    CHECK(c.version==9 && c.image_rev==3);
    CHECK(strcmp(c.fit,"contain")==0);
    CHECK(c.focus_x>0.19 && c.focus_x<0.21 && c.focus_y>0.79 && c.focus_y<0.81);
    CHECK(c.orientation==90);
    CHECK(layout_strategy(&c)==TEX_RUNTIME);
    CHECK(c.budget_bytes_mb==8 && c.budget_entries==4);
}

static void test_invalid_rejected(void) {
    LayoutConfig c; layout_set_defaults(&c);
    char err[128]={0};
    CHECK(!layout_parse(&c,"{\"data\":{\"fit\":\"weird\"}}",err,sizeof(err)));
    err[0]=0;
    CHECK(!layout_parse(&c,"{\"data\":{\"orientation\":45}}",err,sizeof(err)));
    err[0]=0;
    CHECK(!layout_parse(&c,"{\"data\":{\"texture_strategy\":\"gpu\"}}",err,sizeof(err)));
}

void test_layout_all(void) {
    RUN(test_defaults_and_parse);
    RUN(test_invalid_rejected);
}
