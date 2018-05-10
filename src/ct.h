#if !defined (CT_H)
#define CT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define TEST(test) \
    (struct ct_ut) { \
        #test, \
        NULL, \
        test, \
        NULL, \
        NULL \
    }

#define TEST_SETUP_TEARDOWN(test, setup, teardown) \
    (struct ct_ut) { \
        #test, \
        NULL, \
        test, \
        setup, \
        teardown \
    }

#define RUN_TESTS(tests, setup, teardown) \
    _ct_run_tests(#tests, tests, sizeof(tests) / sizeof(struct ct_ut), setup, teardown)

#define _CT_FAILURE() \
    _ct_state.failed = 1; \
    if (_ct_state.exit_on_fail) { \
        printf(ANSI_COLOR_RESET); \
        exit(EXIT_FAILURE); \
    } \

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_TRUE(%s) failed! File: %s, line: %d\n", #expr, __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_FALSE(expr) \
    do { \
        if ((expr)) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_FALSE(%s) failed! File: %s, line: %d\n", #expr, __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_EQ_INT(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_EQ_INT(%s (%lld), %s (%lld)) failed! File: %s, line: %d\n", \
                #expected, (long long) (expected), #actual, (long long) (actual), __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_NE_INT(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_NE_INT(%s (%lld), %s (%lld)) failed! File: %s, line: %d\n", \
                #expected, (long long) (expected), #actual, (long long) (actual), __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_LT_INT(expr1, expr2) \
    do { \
        if (!((expr1) < (expr2))) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_LT_INT(%s (%d), %s (%d)) failed! File: %s, line: %d\n", \
                #expr1, (expr1), #expr2, (expr2), __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_LE_INT(expr1, expr2) \
    do { \
        if (!((expr1) <= (expr2))) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_LE_INT(%s (%d), %s (%d)) failed! File: %s, line: %d\n", \
                #expr1, (expr1), #expr2, (expr2), __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_EQ_PTR(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_EQ_PTR(%s (%p), %s (%p)) failed! File: %s, line: %d\n", \
                #expected, (expected), #actual, (actual), __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_NE_PTR(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_NE_PTR(%s (%p), %s (%p)) failed! File: %s, line: %d\n", \
                #expected, (expected), #actual, (actual), __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_EQ_MEM(expected, actual, len) \
    do { \
        if (memcmp((expected), (actual), (len)) != 0) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_EQ_MEM(%s, %s) failed! File: %s, line: %d\n", \
                #expected, #actual, __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

#define ASSERT_NE_MEM(expected, actual, len) \
    do { \
        if (memcmp((expected), (actual), (len)) == 0) { \
            fprintf(stderr, ANSI_COLOR_RED ">>>> ASSERT_NE_MEM(%s, %s) failed! File: %s, line: %d\n", \
                #expected, #actual, __FILE__, __LINE__); \
            _CT_FAILURE() \
        } \
    } while (0)

struct ct_state {
    int repeat;
    int shuffle;
    int exit_on_fail;
    int failed;
    int use_filter;
    regex_t filter_regex;
};

extern struct ct_state _ct_state;

int ct_initialize(int argc, char *argv[]);

struct ct_ut {
    const char* test_name;
    void *ctx;
    void (* test_func)(void **);
    int (* setup_func)(void **);
    int (* teardown_func)(void **);
};

int _ct_run_tests(const char *suite_name, struct ct_ut *tests, int count, int (*setup)(void **),
    int (*teardown)(void **));

#endif // CT_H
