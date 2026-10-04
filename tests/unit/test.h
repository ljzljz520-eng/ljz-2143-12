#ifndef TEST_H
#define TEST_H
#include <math.h>
#include <stdio.h>
extern int tests_run, tests_failed;
#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { tests_failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)
#define CHECK_NEAR(a,b,eps) do { \
    tests_run++; \
    double _a=(a),_b=(b); \
    if (fabs(_a-_b)>(eps)) { tests_failed++; \
      printf("FAIL %s:%d  %s ~= %s : %.6f vs %.6f\n",__FILE__,__LINE__,#a,#b,_a,_b);} \
} while (0)
#define RUN(fn) do { printf("- %s\n", #fn); fn(); } while(0)
int report_tests(void);
#endif
