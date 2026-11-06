#include "utest.h"
#include "migrate.h"

#include <arena.h>

UTEST_MAIN()

UTEST(foo, bar) {
  ASSERT_TRUE(migrate());
  Arena root = arena_reseverve(GYGABYTES(2));
}