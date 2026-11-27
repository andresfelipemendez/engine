# Setup check · Run the memory unit tests in Test Explorer

Complete [the static library and Google Test setup](02a-memory-library-tutorial.md) first. These cases check the existing C API; Google Test provides the runner, so don't add a `main`.

## Write three tests

In `memorymigration/memory_tests_gtest/test.cpp`, replace the template test with the following. Keep `pch.h` first; if your template uses a different precompiled header name, use that name instead. `malloc` supplies aligned storage and the smart pointer frees it even when a fatal assertion stops the test early.

```cpp
#include "pch.h"
#include "memory_block.h"
#include <cstdlib>
#include <memory>

TEST(MemoryInit, SetsCursors)
{
    constexpr size_t capacity = 256;
    std::unique_ptr<void, decltype(&std::free)> block(
        std::malloc(capacity), &std::free);
    ASSERT_NE(nullptr, block.get());
    ASSERT_TRUE(memory_init(block.get(), capacity));
    const auto* index = static_cast<const Memory*>(block.get());
    EXPECT_EQ(sizeof(Memory), index->front);
    EXPECT_EQ(capacity, index->back);
}

TEST(MemoryInit, RejectsUndersizedBlock)
{
    Memory storage{};
    EXPECT_FALSE(memory_init(&storage, sizeof(storage) - 1));
}

TEST(MemoryInit, RejectsNullBlock)
{
    EXPECT_FALSE(memory_init(nullptr, 256));
}
```

## Build and run

Build `memory_tests_gtest` in **Debug | x64**. In **Test → Test Explorer**, run the `MemoryInit` group. Expect three passing cases. To prove the test is checking the library result, temporarily expect `capacity - 1` for `back`, rebuild and rerun `SetsCursors`. Expect that case to fail; restore `capacity`.

| If you see… | Check… |
| --- | --- |
| Cannot open `memory_block.h` | Add `$(ProjectDir)..\memory` to the test project's include directories. |
| Unresolved `memory_init` | Check the project reference, that `memory_block.c` builds in `memory`, and that the header has C linkage guards. |
| No tests discovered | Build the test project successfully, then check the Google Test adapter and package restore. |

Next: [01 · First memory migration test](01-first-memory-migration-test.md). Add tests alongside these rather than starting a new project.
