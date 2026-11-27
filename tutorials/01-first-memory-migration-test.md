# 01 · First memory migration test: add `ammo` to `Player`

Start with one Google Test case: the old player has **37 HP**, and the new player has a default of **12 ammo**. After copying `hp`, expect the new player to keep its default ammo and the old player to remain unchanged.

Complete [the C library setup](02a-memory-library-tutorial.md) and [the initialization tests](02b-google-test-tutorial.md) first. No DLLs, generated headers, or console app are needed. Leave `memorymigration/main.c` untouched.

## 1. Add the byte-copy helper to `memory.lib`

In `memorymigration/memory/memory_block.h`, add this type and declaration inside the existing C linkage guards:

```c
typedef struct {
    size_t old_offset;
    size_t new_offset;
    size_t size;
} CopyOp;

int copy_field(void *dst, size_t dst_size,
               const void *src, size_t src_size, CopyOp op);
```

In `memorymigration/memory/memory_block.c`, add `#include <string.h>` beside the existing include, then append this function below `memory_init`. The caller supplies distinct, non-overlapping buffers.

```c
int copy_field(void *dst, size_t dst_size,
               const void *src, size_t src_size, CopyOp op)
{
    if (op.old_offset > src_size || op.size > src_size - op.old_offset ||
        op.new_offset > dst_size || op.size > dst_size - op.new_offset)
        return 0;

    memcpy((unsigned char *)dst + op.new_offset,
           (const unsigned char *)src + op.old_offset, op.size);
    return 1;
}
```

Check the starting offset first, then subtract it from the capacity to find the available bytes. For example, at offset 6 in an 8-byte buffer, only 2 bytes remain, so a 4-byte copy fails. Because `||` short-circuits, the subtraction never runs for an offset past the end, where `size_t` subtraction would wrap.

## 2. Add and run the unit test

Append this test to `memorymigration/memory_tests_gtest/test.cpp`. The C++ fixture knows both layouts; the C function only sees byte ranges. `offsetof` measures each layout independently.

```cpp
struct PlayerV1 { int hp; };
struct PlayerV2 { int hp; int ammo; };

TEST(ScalarMigration, CopiesHpAndKeepsAmmoDefault)
{
    const PlayerV1 old{37};
    PlayerV2 next{};
    next.ammo = 12;
    const CopyOp copy_hp{offsetof(PlayerV1, hp), offsetof(PlayerV2, hp),
                         sizeof old.hp};

    ASSERT_TRUE(copy_field(&next, sizeof next, &old, sizeof old, copy_hp));
    EXPECT_EQ(37, old.hp);
    EXPECT_EQ(37, next.hp);
    EXPECT_EQ(12, next.ammo);
}
```

Build `memory_tests_gtest` in **Debug | x64** and run `ScalarMigration.CopiesHpAndKeepsAmmoDefault` in **Test → Test Explorer**. Expect one pass. If `copy_field` doesn't link, check that its declaration is inside the C linkage guards and that the test project references `memory`.

## 3. Check that the copy follows the field

Change only `PlayerV2` to `struct PlayerV2 { int ammo; int hp; };`, rebuild, and rerun. The case should still pass: the copy follows the new `offsetof(PlayerV2, hp)`. A whole-object copy would instead overwrite `ammo` and leave `hp` at zero.

For a separate rejection case, give `CopyOp` a valid source offset but set `new_offset` to `sizeof next` with a nonzero `size`. Expect `copy_field` to return false and the destination values to remain unchanged.

Next: [02 · Put the engine's memory index at byte zero](02-memory-index-tutorial.md).
