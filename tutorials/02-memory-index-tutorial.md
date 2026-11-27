# 02 · Put the engine's memory index at byte zero

Extend the existing C `memory.lib` and add Google Test cases in `memory_tests_gtest`. You won't edit or run `memorymigration/main.c`.

## Before you start

Open the test solution from [the library setup](02a-memory-library-tutorial.md) in Visual Studio. Select **Debug | x64** and confirm that the [initialization tests](02b-google-test-tutorial.md) pass before changing the layout.

## 1. Give each region a fixed slot

The block starts with `Memory`. Each slot contains a byte offset **from the block base**, not a pointer. `REGION_GAME` is a slot number, not a byte offset.

```text
base + 0
  [ Memory: cursors and index ] [padding] [ Engine ] [ Game ] [ Editor ] [free space]
```

In `memorymigration/memory/memory_block.h`, replace the `Memory` definition with the following types, keeping them inside the existing header guard. Add the `memory_find_region` declaration beside `memory_init`, inside the C linkage guards.

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

In `memorymigration/memory/memory_block.c`, replace `memory_init` and add the lookup below it. Keep the `memory_block.h` include.

```c
int memory_init(void *base, size_t capacity)
{
    if (!base || capacity < sizeof(Memory))
        return 0;

    size_t cursor = sizeof(Memory);
    const size_t stride = sizeof(Marker);
    const size_t padding = (stride - cursor % stride) % stride;
    if (padding > capacity - cursor ||
        REGION_COUNT > (capacity - cursor - padding) / stride)
        return 0;
    cursor += padding;

    Memory *index = base;
    unsigned char *bytes = base;
    for (size_t i = 0; i < REGION_COUNT; ++i) {
        index->regions[i] = (Region){cursor, stride};
        *(Marker *)(bytes + cursor) = (Marker){10 * (1 + (int)i)};
        cursor += stride;
    }
    index->front = cursor;
    index->back = capacity;
    return 1;
}

const Region *memory_find_region(const Memory *index, RegionType type)
{
    if (!index || (unsigned)type >= REGION_COUNT)
        return NULL;
    return &index->regions[type];
}
```

The fit check happens before writing anything. `malloc` supplies a suitably aligned base for this fixture; a multiple of `sizeof(Marker)` keeps the markers on valid array-style strides without C11 `_Alignof`. For heterogeneous regions, align each region separately. Lookup checks the slot, not the entry's byte range; use it on a successfully initialized block.

## 3. Look up `Game` in a unit test

In `memorymigration/memory_tests_gtest/test.cpp`, update `MemoryInit.SetsCursors`: replace `EXPECT_EQ(sizeof(Memory), index->front);` with the following. Change the capacity in `MemoryInit.RejectsUndersizedBlock` to `sizeof(storage)` so it proves that a block with room for the index alone is rejected.

```cpp
const auto* editor = memory_find_region(index, REGION_EDITOR);
ASSERT_NE(nullptr, editor);
EXPECT_EQ(editor->offset + editor->size, index->front);
```

Append these cases to the same file. The fatal range assertions stop the test before it forms an invalid payload pointer.

```cpp
TEST(MemoryIndex, FindsGameInFixedSlot)
{
    constexpr size_t capacity = 256;
    std::unique_ptr<void, decltype(&std::free)> block(
        std::malloc(capacity), &std::free);
    ASSERT_NE(nullptr, block.get());
    ASSERT_TRUE(memory_init(block.get(), capacity));

    const auto* index = static_cast<const Memory*>(block.get());
    const auto* game = memory_find_region(index, REGION_GAME);
    ASSERT_EQ(&index->regions[REGION_GAME], game);
    ASSERT_GE(game->offset, sizeof(Memory));
    ASSERT_LE(game->offset, capacity);
    ASSERT_EQ(0u, game->offset % sizeof(Marker));
    ASSERT_EQ(sizeof(Marker), game->size);
    ASSERT_LE(game->size, capacity - game->offset);

    const auto* bytes = static_cast<const unsigned char*>(block.get());
    const auto* marker = reinterpret_cast<const Marker*>(bytes + game->offset);
    EXPECT_EQ(20, marker->value);
}

TEST(MemoryIndex, RejectsInvalidSlot)
{
    const Memory index{};
    EXPECT_EQ(nullptr, memory_find_region(&index, REGION_COUNT));
}
```

Build `memory_tests_gtest`, then run the `MemoryInit` and `MemoryIndex` groups in **Test → Test Explorer**. Expect five passing cases across both groups. If `SetsCursors` expects `front` at the end of the header, update it to check the end of `Editor`. If the marker is wrong, check that `Game` uses slot 1 and that the lookup adds the offset to the block base.

**Try it:** Temporarily change the lookup in `FindsGameInFixedSlot` to `REGION_ENGINE`. Expect the slot assertion to fail. Restore `REGION_GAME` before continuing.

## Review: the index is not migration metadata

The index says **where a region begins**. A separate generated field table says **where a field begins within that region**. If `Game` starts at byte 128 and its `hp` is at byte 4, the field is at byte 132 after validating both ranges. See [Step 05](05-c-codegen-tutorial.md#5-emit-the-tables-and-shared-api) for the field table.

Byte zero is a location contract, not a layout contract. The old DLL describes its own block and the new DLL describes its own block. The host matches regions by stable names, not by enum ordinal, and copies payload fields only. Copying the old index would overwrite the offsets computed by the new DLL.

## Optional follow-up: strings and sub-arenas

Keep these exercises in the same two projects. Put the C helpers in `memory_block.c`, their declarations inside the C linkage guards in `memory_block.h`, and each behavior check in a separate `TEST` in `test.cpp`.

- **Strings:** Store references as `{offset, length}`, not pointers. For a downward-growing pool, require `front <= back <= capacity` and `length < back - front` before reserving `length + 1` bytes for text and its terminator. With capacity 256, appending `door` then `open` should produce offsets 251 and 246. Add a separate case where the text would exactly fill the gap without its terminator; expect rejection and unchanged cursors. Resolve references only within the occupied pool and check that the terminator is inside the block. Migration must copy the string bytes and install a new reference, not copy the old offset.
- **Sub-arenas:** Reserve non-overlapping, suitably aligned ranges by advancing the root `front`. Store each range's block-relative start, capacity, and local cursor. Test that allocations in one range don't move another's cursor or the root cursor. Persistent references must not point into scratch space that can be reset.

Next: [03 · Arena allocation](03-arena-tutorial.md). Revisit sub-arenas before [07 · Array migration](07-array-migration-tutorial.md).
