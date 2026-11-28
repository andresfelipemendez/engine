# 02 · Put the engine's memory index at byte zero

Extend the existing C `memory.lib` and add Google Test cases in `memory_tests`. You won't edit or run `memorymigration/main.c`.

## Before you start

Open the test solution from [the library setup](00a-memory-library-tutorial.md) in Visual Studio. Select **Debug | x64** and confirm that the [initialization tests](00b-google-test-tutorial.md) pass before changing the layout.

## 1. Give each region a fixed slot

The block starts with `Memory`. Each slot contains a byte offset **from the block base**, not a pointer. `REGION_GAME` is a slot number, not a byte offset.

```text
base + 0
  [ Memory: cursors and index ] [padding] [ Engine ] [ Game ] [ Editor ] [free space]
```

In `memorymigration/memory/memory.h`, replace the `Memory` definition with the following types, keeping them inside the existing header guard. Add the `memory_find_region` declaration beside `memory_init`, inside the C linkage guards.

```c
typedef enum {
    REGION_ENGINE,
    REGION_GAME,
    REGION_EDITOR,
    REGION_COUNT
} RegionType;

typedef struct {
    size_t offset;
    size_t size;
} Region;

typedef struct {
    size_t front;
    size_t back;
    Region regions[REGION_COUNT];
} Memory;

typedef struct { int value; } Marker;

const Region *memory_find_region(const Memory *index, RegionType type);
```

`Marker` stands in for each region's real payload so this fixture needs just one placement rule.

## 2. Place the regions in the C library

In `memorymigration/memory/memory.c`, replace `memory_init` and add the lookup below it. Keep `#include "memory.h"`.

```c
bool memory_init(MemoryBlock block)
{
    if (!block.base || block.capacity < sizeof(Memory))
        return false;

    size_t cursor = sizeof(Memory);
    const size_t stride = sizeof(Marker);
    const size_t padding = (stride - cursor % stride) % stride;
    /* Check the whole layout before writing even the first region. */
    if (padding > block.capacity - cursor ||
        REGION_COUNT > (block.capacity - cursor - padding) / stride)
        return false;
    cursor += padding;

    Memory *index = (Memory *)block.base;
    unsigned char *bytes = block.base;
    for (size_t i = 0; i < REGION_COUNT; ++i) {
        index->regions[i] = (Region){cursor, stride};
        /* Distinct values expose a lookup that lands in the wrong slot. */
        *(Marker *)(bytes + cursor) = (Marker){10 * (1 + (int)i)};
        cursor += stride;
    }
    index->front = cursor;
    index->back = block.capacity;
    return true;
}

const Region *memory_find_region(const Memory *index, RegionType type)
{
    if (!index || (unsigned)type >= REGION_COUNT)
        return NULL;
    return &index->regions[type];
}
```

The fit check happens before writing anything. [`malloc` supplies a suitably aligned base](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) (C99 §7.20.3) for this fixture; a multiple of `sizeof(Marker)` keeps the markers on valid array-style strides without C11 `_Alignof`. For heterogeneous regions, align each region separately. Lookup checks the slot, not the entry's byte range; use it on a successfully initialized block.

## 3. Look up `Game` in a unit test

In the `memory_tests` project's `test.cpp` (`memorymigration/memory_test/test.cpp` in the checkout), update `CheckCursors`: replace `EXPECT_EQ(sizeof(Memory), index->front);` with the following. Change the `MemoryBlock` initializer's capacity in `MemoryInit.RejectsUndersizedBlock` to `sizeof(storage)` so it proves that a block with room for the index alone is rejected.

```cpp
const Region *editor = memory_find_region(index, REGION_EDITOR);
ASSERT_TRUE(editor != NULL);
EXPECT_EQ(editor->offset + editor->size, index->front);
```

Append this helper and these cases to the same file. Fatal assertions stop the helper before it forms an invalid payload pointer; the calling test still frees the block.

```cpp
static void CheckGameSlot(MemoryBlock block)
{
    ASSERT_TRUE(memory_init(block));

    const Memory *index = (const Memory *)block.base;
    const Region *game = memory_find_region(index, REGION_GAME);
    ASSERT_EQ(&index->regions[REGION_GAME], game);
    ASSERT_GE(game->offset, sizeof(Memory));
    ASSERT_LE(game->offset, block.capacity);
    ASSERT_EQ(0u, game->offset % sizeof(Marker));
    ASSERT_EQ(sizeof(Marker), game->size);
    ASSERT_LE(game->size, block.capacity - game->offset);

    /* Add the offset to a byte pointer, not to the Memory pointer. */
    const unsigned char *bytes = block.base;
    const Marker *marker = (const Marker *)(bytes + game->offset);
    EXPECT_EQ(20, marker->value);
}

TEST(MemoryIndex, FindsGameInFixedSlot)
{
    const size_t capacity = 256;
    MemoryBlock block = {(unsigned char *)malloc(capacity), capacity};
    ASSERT_TRUE(block.base != NULL);
    CheckGameSlot(block);
    free(block.base);
}

TEST(MemoryIndex, RejectsInvalidSlot)
{
    const Memory index = {0};
    EXPECT_TRUE(memory_find_region(&index, REGION_COUNT) == NULL);
}
```

Build `memory_tests`, then run the `MemoryInit` and `MemoryIndex` groups in **Test → Test Explorer**. Expect five passing cases across both groups. If `SetsCursors` expects `front` at the end of the header, update it to check the end of `Editor`. If the marker is wrong, check that `Game` uses slot 1 and that the lookup adds the offset to the block base.

**Try it:** Temporarily change the lookup in `CheckGameSlot` to `REGION_ENGINE`. Expect the slot assertion to fail. Restore `REGION_GAME` before continuing.

## Review: the index is not migration metadata

The index says **where a region begins**. A separate generated field table says **where a field begins within that region**. If `Game` starts at byte 128 and its `hp` is at byte 4, the field is at byte 132 after validating both ranges. See [Step 05](05-c-codegen-tutorial.md#5-emit-the-tables-and-shared-api) for the field table.

Byte zero is a location contract, not a layout contract. The old DLL describes its own block and the new DLL describes its own block. The host matches regions by stable names, not by enum ordinal, and copies payload fields only. Copying the old index would overwrite the offsets computed by the new DLL.

## Optional follow-up: strings and sub-arenas

Keep these exercises in the same two projects. Put the C helpers in `memory.c`, their declarations inside the C linkage guards in `memory.h`, and each behavior check in a separate `TEST` in `test.cpp`.

- **Strings:** Store references as `{offset, length}`, not pointers. For a downward-growing pool, require `front <= back <= capacity` and `length < back - front` before reserving `length + 1` bytes for text and its terminator. With capacity 256, appending `door` then `open` should produce offsets 251 and 246. Add a separate case where the text would exactly fill the gap without its terminator; expect rejection and unchanged cursors. Resolve references only within the occupied pool and check that the terminator is inside the block. Migration must copy the string bytes and install a new reference, not copy the old offset.
- **Sub-arenas:** Reserve non-overlapping, suitably aligned ranges by advancing the root `front`. Store each range's block-relative start, capacity, and local cursor. Test that allocations in one range don't move another's cursor or the root cursor. Persistent references must not point into scratch space that can be reset.

Next: [02a · Describe a live block without exposing its index](02a-live-layout-tutorial.md). Revisit sub-arenas before [07 · Array migration](07-array-migration-tutorial.md).
