#include "utest.h"
#include "migrate.h"

UTEST_MAIN()

UTEST(foo, bar) {
  ASSERT_TRUE(migrate());
}