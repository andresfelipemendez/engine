# 00b · Run the memory unit tests in Test Explorer

Complete [the static library and Google Test setup](00a-memory-library-tutorial.md) first. These cases check the existing C API; Google Test provides the runner, so don't add a `main`.

## Write three tests

In the `memory_tests` project's `test.cpp` (`memorymigration/memory_test/test.cpp` in the checkout), replace the template test with the following. Keep `pch.h` first. Google Test requires compiling the test file as C++, but the test bodies use C-style declarations, casts, `malloc`, and `free`. Use `ASSERT_TRUE` when continuing would be unsafe, and `EXPECT_EQ` when we can report more than one mismatch. A fatal assertion returns from the current function, not the entire call stack ([GoogleTest assertions reference](https://google.github.io/googletest/reference/assertions.html)). Put the checks in `CheckCursors` so the calling test still frees the block if initialization fails.

```cpp
#include "pch.h"
#include "memory.h"
#include <stdlib.h>

static void CheckCursors(MemoryBlock block)
{
    /* Do not read the header unless initialization succeeded. */
    ASSERT_TRUE(memory_init(block));
    const Memory *index = (const Memory *)block.base;
    EXPECT_EQ(sizeof(Memory), index->front);
    EXPECT_EQ(block.capacity, index->back);
}

TEST(MemoryInit, SetsCursors)
{
    const size_t capacity = 256;
    MemoryBlock block = {(unsigned char *)malloc(capacity), capacity};
    ASSERT_TRUE(block.base != NULL);
    CheckCursors(block);
    free(block.base);
}

TEST(MemoryInit, RejectsUndersizedBlock)
{
    Memory storage = {0};
    /* Give initialization one byte less than it needs for the header. */
    MemoryBlock block = {(unsigned char *)&storage, sizeof(storage) - 1};
    EXPECT_FALSE(memory_init(block));
}

TEST(MemoryInit, RejectsNullBlock)
{
    MemoryBlock block = {NULL, 256};
    EXPECT_FALSE(memory_init(block));
}
```

## Build and run

Build `memory_tests` in **Debug | x64**. In **Test → Test Explorer**, run the `MemoryInit` group. Expect three passing cases. To prove the test is checking the library result, temporarily expect `block.capacity - 1` for `back`, rebuild and rerun `SetsCursors`. Expect that case to fail; restore `block.capacity`.

| If you see… | Check… |
| --- | --- |
| Cannot open `memory.h` (or wrong `Memory` definition) | Add `$(ProjectDir)..\memory` to the test project's include directories. |
| Unresolved `memory_init` | Check the project reference, that `memory.c` builds in `memory`, and that the header has C linkage guards. |
| No tests discovered | Build the test project successfully, then check the Google Test adapter and package restore. |

Next: [01 · First memory migration test](01-first-memory-migration-test.md). Add tests alongside these rather than starting a new project.
