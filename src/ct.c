#include "ct.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

struct ct_state _ct_state;

static int get_int(const char* s)
{
    int result = strtol(optarg, NULL, 10);
    if (result == 0 && errno == EINVAL) {
        perror("Invalid 'repeat' value");
        exit(EXIT_FAILURE);
    }

    return result;
}

static void print_help()
{
    printf(
        "C Unit testing framework\n"
        "Usage <test> [-s] [-r count] [-h]\n"
        "  -r count   repeat tests count times\n"
        "  -s         random shuffle tests\n"
        "  -e         exit on fail\n"
        "  -f         filter (regular expression)\n"
        "  -h         print this help\n");

    exit(EXIT_SUCCESS);
}

int ct_initialize(int argc, char *argv[])
{
    int ch;

    memset(&_ct_state, 0, sizeof(_ct_state));
    while ((ch = getopt(argc, argv, "hr:sef:")) != -1) {
        switch (ch) {
        case 'r':
            _ct_state.repeat = get_int(optarg);
            break;
        case 's':
            _ct_state.shuffle = 1;
            break;
        case 'e':
            _ct_state.exit_on_fail = 1;
            break;
        case 'f':
            if (regcomp(&_ct_state.filter_regex, optarg, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
                fprintf(stderr, "Invalid filter string\n");
                exit(EXIT_FAILURE);
            }
            _ct_state.use_filter = 1;
            break;
        case '?':
        case ':':
        case 'h':
            print_help();
            break;
        }
    }

    if (_ct_state.repeat <= 0)
        _ct_state.repeat = 0;

    return 0;
}

static void report_failure(const char *fmt, ...)
{
    va_list ap;
    char buf[256];

    va_start(ap, fmt);
    snprintf(buf, sizeof(buf), ANSI_COLOR_RED);
    snprintf(buf + strlen(ANSI_COLOR_RED), sizeof(buf) - strlen(ANSI_COLOR_RED),
        ">>>> FAILURE: %s\n", fmt);
    vprintf(buf, ap);
    va_end(ap);

    printf(ANSI_COLOR_RESET);
    _ct_state.failed = 1;
    if (_ct_state.exit_on_fail)
        exit(EXIT_FAILURE);
}

static void randomize(int *indexes, int count)
{
    struct timeval tv;
    int i, j, tmp;

    gettimeofday(&tv, NULL);
    srand(tv.tv_sec * 1000 * 1000 + tv.tv_usec);

    for (i = 0; i < count; ++i)
        indexes[i] = i;

    if (!_ct_state.shuffle)
        return;

    for (i = 0; i < count; ++i) {
        j = i + rand() % (count - i);
        (tmp) = indexes[i];
        indexes[i] = indexes[j];
        indexes[j] = tmp;
    }
}

int _ct_run_tests(const char *suite_name, struct ct_ut *tests, int count,
    int (*setup)(void **), int (*teardown)(void **))
{
    int i, r, success_count = 0, fail_count = 0, index;
    struct timeval iter_start, iter_end, start, end;
    char buf[256];
    int indexes[count];
    int (* aux_func)(void **) = NULL;

    printf(ANSI_COLOR_RESET);
    if (!tests)
        return 0;

    memset(buf, 0, sizeof(buf));
    if (_ct_state.repeat > 0) {
        snprintf(buf, sizeof(buf), ". %d iterations.", _ct_state.repeat);
    }

    if (_ct_state.shuffle == 1) {
        snprintf(buf, sizeof(buf) - strlen(buf), ". Random shuffle enabled.");
    }

    gettimeofday(&start, NULL);
    printf("\nRunning test suite %s%s\n", suite_name, buf);
    for (r = 0; r < (_ct_state.repeat == 0 ? 1 : _ct_state.repeat); ++r) {
        if (_ct_state.repeat != 0)
            printf(ANSI_COLOR_RESET "\nIteration %d\n", r + 1);

        randomize(indexes, count);
        for (i = 0; i < count; ++i) {
            index = indexes[i];
            if (_ct_state.use_filter &&
                    regexec(&_ct_state.filter_regex, tests[index].test_name, 0, NULL, 0)) {
                continue;
            }
            printf(ANSI_COLOR_RESET);
            _ct_state.failed = 0;
            printf(ANSI_COLOR_GREEN "[RUN...] %s\n", tests[index].test_name);
            aux_func = tests[index].setup_func ? tests[index].setup_func : setup;
            gettimeofday(&iter_start, NULL);

            if (aux_func && aux_func(&tests[index].ctx) != 0) {
                report_failure("setup() failed for %s", tests[index].test_name);
            } else {
                tests[index].test_func(&tests[index].ctx);
                aux_func = tests[index].teardown_func ? tests[index].teardown_func : teardown;
                if (aux_func && aux_func(&tests[index].ctx) != 0) {
                    report_failure("teardown() failed for %s", tests[index].test_name);
                }
            }

            gettimeofday(&iter_end, NULL);
            if (_ct_state.failed) {
                fail_count++;
                printf(ANSI_COLOR_RED "[..FAIL] %s\n", tests[index].test_name);
            } else {
                success_count++;
                printf(ANSI_COLOR_GREEN "[....OK] %s - %ld ms\n", tests[index].test_name,
                    (iter_end.tv_sec - iter_start.tv_sec) * 1000 + (iter_end.tv_usec - iter_start.tv_usec) / 1000);
            }
        }
    }
    gettimeofday(&end, NULL);
    printf(ANSI_COLOR_RESET "Test suite %s finished. %d succeded, %d failed%s\nElapsed: %ld ms\n", suite_name,
        success_count, fail_count, buf,
        (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000);

    return success_count + fail_count;
}
