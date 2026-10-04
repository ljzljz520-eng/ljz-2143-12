#include "test.h"
int tests_run=0, tests_failed=0;
void test_geometry_all(void);
void test_events_all(void);
void test_json_all(void);
void test_layout_all(void);
void test_texcache_all(void);
int main(void){
    test_geometry_all();
    test_events_all();
    test_json_all();
    test_layout_all();
    test_texcache_all();
    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
