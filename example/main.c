#include "ct.h"

static void test1(void **ctx)
{
    (void) ctx;
    const char *s1 = "hello";
    const char *s2 = "hella";
    
    ASSERT_TRUE(0 == 0);
    ASSERT_EQ_INT(2, 2);
    ASSERT_LE_INT(1, 2);
    ASSERT_LT_INT(1, 2);
    ASSERT_EQ_MEM(s1, s2, strlen(s1));
    ASSERT_EQ_PTR(s1, s1);
    ASSERT_EQ_PTR(s1, s2);
}

static void test2(void **ctx)
{
    printf("%d\n", *((int *) *ctx));
    ASSERT_TRUE(0 == 0);
}

static int test2_setup(void **ctx)
{
    int *i = (int *) malloc(sizeof(int));
    *i =42;
    *ctx = i;
    
    return 1; // this will cause failure
}

static int test2_teardown(void **ctx)
{
    free(*ctx);
    return 0;
}

int main(int argc, char *argv[])
{
    ct_initialize(argc, argv);
    struct ct_ut testing_tests[] = {
        TEST(test1),
        TEST_SETUP_TEARDOWN(test2, test2_setup, test2_teardown)
    };
    RUN_TESTS(testing_tests, NULL, NULL);
}
