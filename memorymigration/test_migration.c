#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS // Greatest uses standard C string functions.
#endif

#include "migration.h"
#include "vendor/greatest.h"

#include <string.h>

TEST version_one_targets_two(void)
{
    ASSERT_EQ(2, migration_target_version(1));
    PASS();
}

TEST other_versions_stay_unchanged(void)
{
    ASSERT_EQ(5, migration_target_version(5));
    PASS();
}

SUITE(migration)
{
    RUN_TEST(version_one_targets_two);
    RUN_TEST(other_versions_stay_unchanged);
}

TEST coverage_demo_wrong_expectation(void)
{
    int actual = migration_target_version(1);
    ASSERT_EQ_FMT(3, actual, "%d");
    PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char *argv[])
{
    int demo = argc == 2 && strcmp(argv[1], "--demo-fail") == 0;
    if (demo) argc = 1; // Consume our opt-in flag before Greatest parses its options.
    GREATEST_MAIN_BEGIN();
    if (demo) {
        RUN_TEST(coverage_demo_wrong_expectation);
    } else {
        RUN_SUITE(migration);
    }
    GREATEST_MAIN_END();
}
