#include "ct.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

#if defined (__unix__)
    #include <sys/time.h>
#endif

struct ct_state _ct_state;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Simple glob-like string matcher. Supported symbols are: '*' - any value repeated any times,
// ':' - OR separator.

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

static int matchImpl(const char *pattern, int patternLen, const char *string)
{
    struct MatchPairStack stack;
    MatchPairStack_init(&stack);

    if (patternLen <= 0)
        return 0;

    MatchPairStack_push(&stack, MatchPair_create());

    int result = 0;
    struct MatchPair *currentPair;
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

static int match(const char *pattern, const char *string)
{
    const char *p;
    for (p = pattern; *p != 0; ++p) {
        if (*p == ':') {
            if (matchImpl(pattern, p - pattern, string))
                return 1;
            pattern = p + 1;
        }
    }


    return matchImpl(pattern, p - pattern, string);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Time functions.

#if defined (__unix__)

static int64_t nowMsUnix()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv->tv_sec * 1000 + tv->tv_usec / 1000;
}

#elif defined (_WIN32)

static int64_t nowMsWin32()
{
    SYSTEMTIME systemTime;
    GetSystemTime(&systemTime);

    FILETIME fileTime;
    SystemTimeToFileTime(&systemTime, &fileTime);

    ULARGE_INTEGER largeInteger;
    largeInteger.LowPart = fileTime.dwLowDateTime;
    largeInteger.HighPart = fileTime.dwHighDateTime;

    return largeInterger.QuadPart / 10000LL;
}

#endif

static int64_t nowMs()
{
    #if defined (__unix__)
        return nowMsUnix();
    #elif defined (_WIN32)
        return nowMsWin32();
    #endif

    return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Options parsing

enum OptionsParseState {
    OptionsParseState_waitingForMinus,
    OptionsParseState_waitingForKey,
    OptionsParseState_waitingForArgument,
    OptionsParseState_done
};

struct OptionsContext {
    const char *argument;
    enum OptionsParseState state;
    int argc;
};

static void OptionsContext_init(struct OptionsContext *optionsContext)
{
    optionsContext->argument = NULL;
    optionsContext->state = OptionsParseState_waitingForMinus;
    optionsContext->argc = 1;
}

enum OptionsKeyPresence {
    OptionsKeyPresence_yes,
    OptionsKeyPresence_yesWithArgument,
    OptionsKeyPresence_no
};

static enum OptionsKeyPresence isKeyPresent(const char *optionString, int key)
{
    for (int i = 0; i < strlen(optionString); ++i) {
        if (optionString[i] == key) {
            if (i < strlen(optionString - 1) && optionString[i + 1] == ':') {
                return OptionsKeyPresence_yesWithArgument;
            }
            return OptionsKeyPresence_yes;
        }
    }

    return OptionsKeyPresence_no;
}

int getOption(
    struct OptionsContext *context, int argc, char *argv[], const char *optionString)
{
    enum OptionsKeyPresence keyPresence;
    while (1) {
        switch (context->state) {
            case OptionsParseState_waitingForMinus:
                if (context->argc >= argc) {
                    context->state = OptionsParseState_done;
                    break;
                }
                if (argv[context->argc][0] != '-') {
                    context->state = OptionsParseState_done;
                    break;
                }
                context->state = OptionsParseState_waitingForKey;
                break;
            case OptionsParseState_waitingForKey:
                if (strlen(argv[context->argc]) == 1 || !isalpha(argv[context->argc][1])) {
                    context->state = OptionsParseState_done;
                    break;
                }
                keyPresence = isKeyPresent(optionString, argv[context->argc][1]);
                switch (keyPresence) {
                    case OptionsKeyPresence_no:
                        context->state = OptionsParseState_waitingForMinus;
                        break;
                    case OptionsKeyPresence_yes:
                        context->argc++;
                        context->state = OptionsParseState_waitingForMinus;
                        context->argument = NULL;
                        return argv[context->argc - 1][1];
                    case OptionsKeyPresence_yesWithArgument:
                        context->state = OptionsParseState_waitingForArgument;
                        break;
                }
                context->argc++;
                break;
            case OptionsParseState_waitingForArgument:
                if (context->argc >= argc) {
                    context->state = OptionsParseState_done;
                    break;
                }
                context->argument = argv[context->argc];
                context->state = OptionsParseState_waitingForMinus;
                return argv[context->argc - 1][1];
            case OptionsParseState_done:
                return -1;
        }
    }
}

static int get_int(const char* s)
{
    int result = strtol(s, NULL, 10);
    if (result == 0 && errno == EINVAL) {
        perror("Invalid 'repeat' value");
        exit(EXIT_FAILURE);
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

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

int ct_initialize(int argc, char *argv[])
{
    int ch;
    struct OptionsContext optionsContext;

    memset(&_ct_state, 0, sizeof(_ct_state));
    OptionsContext_init(&optionsContext);
    while ((ch = getOption(&optionsContext, argc, argv, "hr:sef:")) != -1) {
        switch (ch) {
            case 'r':
                _ct_state.repeat = get_int(optionsContext.argument);
                break;
            case 's':
                _ct_state.shuffle = 1;
                break;
            case 'e':
                _ct_state.exit_on_fail = 1;
                break;
            case 'f':
                _ct_state.filter = optionsContext.argument;
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
    int i, j, tmp;

    srand(nowMs());

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

int _ct_run_tests(
    const char *suite_name, struct ct_ut *tests, int count, int (*setup)(void **),
    int (*teardown)(void **))
{
    int i, r, success_count = 0, fail_count = 0, index, result_code = 0, have_match = 0;
    int64_t iter_start, start;
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

    start = nowMs();
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
            iter_start = nowMs();

            if (aux_func && aux_func(&tests[index].ctx) != 0) {
                report_failure("setup() failed for %s", tests[index].test_name);
            } else {
                tests[index].test_func(&tests[index].ctx);
                aux_func = tests[index].teardown_func ? tests[index].teardown_func : teardown;
                if (aux_func && aux_func(&tests[index].ctx) != 0) {
                    report_failure("teardown() failed for %s", tests[index].test_name);
                }
            }

            if (_ct_state.failed) {
                result_code = -1;
                fail_count++;
                printf(ANSI_COLOR_RED "[..FAIL] %s\n", tests[index].test_name);
            } else {
                success_count++;
                printf(ANSI_COLOR_GREEN "[....OK] %s - %lld ms\n", tests[index].test_name,
                    nowMs() - iter_start);
            }
        }
    }
    printf(ANSI_COLOR_RESET "Test suite %s finished. %d succeded, %d failed%s\nElapsed: %lld ms\n",
        suite_name, success_count, fail_count, buf, nowMs() - start);

    return result_code;
}
