#include "ct.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

struct ct_state _ct_state;

struct MatchPair {
    int patternPos;
    int stringPos;
};

static struct MatchPair *MatchPair_create()
{
    struct MatchPair *result = malloc(sizeof(struct MatchPair));
    memset(result, 0, sizeof(*result));
    return result;
}

static void MatchPair_destroy(struct MatchPair *pair)
{
    free(pair);
}

struct MatchPairStack {
    struct MatchPair **stack;
    int pos;
    int capacity;
};

static void MatchPairStack_init(struct MatchPairStack *stack)
{
    stack->stack = NULL;
    stack->pos = -1;
    stack->capacity = 0;
}

static void MatchPairStack_deinit(struct MatchPairStack *stack)
{
    free(stack->stack);
}

static struct MatchPair *MatchPairStack_pop(struct MatchPairStack *stack)
{
    if (stack->pos < 0)
        return NULL;

    return stack->stack[stack->pos--];
}

static void MatchPairStack_push(struct MatchPairStack *stack, struct MatchPair *matchPair)
{
    if (stack->pos + 1 == stack->capacity) {
        if (stack->capacity == 0) {
            stack->capacity = 1;
            stack->stack = malloc(sizeof(void *));
        } else {
            stack->capacity *= 2;
            stack->stack = realloc(stack->stack, sizeof(void *) * stack->capacity);
        }
    }

    stack->stack[++stack->pos] = matchPair;
}

static int onlyWildcardsLeft(const char *pattern, int len, int start)
{
    for (int i = start; i < len; ++i) {
        if (pattern[i] != '*')
            return 0;
    }

    return 1;
}

static int match(const char *pattern, const char *string)
{
    struct MatchPairStack stack;
    MatchPairStack_init(&stack);

    MatchPairStack_push(&stack, MatchPair_create());

    int result = 0;
    struct MatchPair *currentPair;
    int patternLen = strlen(pattern);
    int stringLen = strlen(string);

    while ((currentPair = MatchPairStack_pop(&stack)) != NULL) {
        if (result == 1)
            break;

        while (1) {
            if (currentPair->patternPos == patternLen) {
                if (currentPair->stringPos == stringLen) {
                    result = 1;
                    break;
                } else {
                    result = 0;
                    break;
                }
            }

            if (currentPair->stringPos == stringLen) {
                if (onlyWildcardsLeft(pattern, patternLen, currentPair->patternPos)) {
                    result = 1;
                    break;
                } else {
                    result = 0;
                    break;
                }
            }

            if (pattern[currentPair->patternPos] == '*') {
                if (onlyWildcardsLeft(pattern, patternLen, currentPair->patternPos)) {
                    result = 1;
                    break;
                } else {
                    for (; currentPair->stringPos < stringLen; ++currentPair->stringPos) {
                        struct MatchPair *pair = MatchPair_create();
                        pair->patternPos = currentPair->patternPos + 1;
                        pair->stringPos = currentPair->stringPos;
                        MatchPairStack_push(&stack, pair);
                    }
                    break;
                }
            } else {
                if (pattern[currentPair->patternPos] == string[currentPair->stringPos]) {
                    currentPair->patternPos++;
                    currentPair->stringPos++;
                } else {
                    result = 0;
                    break;
                }
            }
        }

        MatchPair_destroy(currentPair);
    }

    MatchPairStack_deinit(&stack);
    return result;
}

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
        "  -e         sigtrap on failure\n"
        "  -f         filter (regular expression)\n"
        "  -h         print this help\n");

    exit(EXIT_SUCCESS);
}

static long time_range_ms(const struct timeval* start, const struct timeval* end)
{
    return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_usec - start->tv_usec) / 1000;
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
            _ct_state.filter = optarg;
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
        tmp = indexes[i];
        indexes[i] = indexes[j];
        indexes[j] = tmp;
    }
}

int _ct_run_tests(const char *suite_name, struct ct_ut *tests, int count,
    int (*setup)(void **), int (*teardown)(void **))
{
    int i, r, success_count = 0, fail_count = 0, index, result_code = 0, have_match = 0;
    struct timeval iter_start, iter_end, start, end;
    char buf[256];
    int indexes[count];
    int (* aux_func)(void **) = NULL;

    printf(ANSI_COLOR_RESET);
    if (!tests)
        return 0;

    if (_ct_state.filter != NULL) {
        for (i = 0; i < count; ++i) {
            if (match(_ct_state.filter, tests[i].test_name)) {
                have_match = 1;
                break;
            }
        }
        if (!have_match)
            return 0;
    }

    memset(buf, 0, sizeof(buf));
    if (_ct_state.repeat > 0) {
        snprintf(buf, sizeof(buf), ". %d iterations.", _ct_state.repeat);
    }

    if (_ct_state.shuffle == 1) {
        snprintf(buf, sizeof(buf) - strlen(buf), ". Random shuffle enabled.");
    }

    gettimeofday(&start, NULL);
    fprintf(stdout, "========== %s%s =========\n", suite_name, buf);
    fflush(stdout);
    for (r = 0; r < (_ct_state.repeat == 0 ? 1 : _ct_state.repeat); ++r) {
        if (_ct_state.repeat != 0)
            printf(ANSI_COLOR_RESET "Iteration %d\n", r + 1);

        randomize(indexes, count);
        for (i = 0; i < count; ++i) {
            index = indexes[i];
            if (_ct_state.filter != NULL && !match(_ct_state.filter, tests[index].test_name)) {
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
                result_code = -1;
                fail_count++;
                printf(ANSI_COLOR_RED "[..FAIL] %s\n", tests[index].test_name);
            } else {
                success_count++;
                printf(ANSI_COLOR_GREEN "[....OK] %s - %ld ms\n", tests[index].test_name,
                    time_range_ms(&iter_start, &iter_end));
            }
        }
    }
    gettimeofday(&end, NULL);
    printf(ANSI_COLOR_RESET "Test suite %s finished. %d succeded, %d failed%s\nElapsed: %ld ms\n",
        suite_name, success_count, fail_count, buf, time_range_ms(&start, &end));

    return result_code;
}
