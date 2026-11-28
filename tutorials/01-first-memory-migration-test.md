# 01 · First memory migration test: add `ammo` to `Player`

Start with one Google Test case: the old player has **37 HP**, and the new player has a default of **12 ammo**. After copying `hp`, expect the new player to keep its default ammo and the old player to remain unchanged.

Start here once the [00b · Initialization tests](00b-google-test-tutorial.md) pass. No DLLs, generated headers, or console app are needed. Leave `memorymigration/main.c` untouched.

## 1. Add the byte-copy helper to `memory.lib`

In `memorymigration/memory/memory.h`, add this type and declaration inside the existing C linkage guards:

```c
typedef struct {
    size_t old_offset;
    size_t new_offset;
    size_t size;
} CopyOp;

bool copy_field(const MemoryBlock *src, const MemoryBlock *dest, CopyOp op);
```

In `memorymigration/memory/memory.c`, add `#include <string.h>` beside the existing include, then append this function below `memory_init`. The caller supplies valid descriptors for distinct, non-overlapping buffers. `const MemoryBlock *` prevents changing either descriptor, but `dest->base` still points to writable bytes.

```c
bool copy_field(const MemoryBlock *src, const MemoryBlock *dest, CopyOp op)
{
    if (op.old_offset > src->capacity ||
        op.size > src->capacity - op.old_offset ||
        op.new_offset > dest->capacity ||
        op.size > dest->capacity - op.new_offset)
        return false;

    memcpy(dest->base + op.new_offset, src->base + op.old_offset, op.size);
    return true;
}
```

Check the starting offset first, then subtract it from the capacity to find the available bytes. For example, at offset 6 in an 8-byte buffer, only 2 bytes remain, so a 4-byte copy fails. Because `||` short-circuits, the subtraction never runs for an offset past the end, where `size_t` subtraction would wrap.

## 2. Add and run the unit test

Append this test to the `memory_tests` project's `test.cpp` (`memorymigration/memory_test/test.cpp` in the checkout). The test fixture knows both layouts; the C function only sees byte ranges. `offsetof` measures each layout independently.

```cpp
typedef struct { int hp; } PlayerV1;
typedef struct { int hp; int ammo; } PlayerV2;

TEST(ScalarMigration, CopiesHpAndKeepsAmmoDefault)
{
    PlayerV1 old = {37};
    PlayerV2 next = {0};
    next.ammo = 12;
    const CopyOp copy_hp = {
        offsetof(PlayerV1, hp), offsetof(PlayerV2, hp), sizeof old.hp
    };

    const MemoryBlock src = {(unsigned char *)&old, sizeof old};
    const MemoryBlock dest = {(unsigned char *)&next, sizeof next};

    ASSERT_TRUE(copy_field(&src, &dest, copy_hp));
    EXPECT_EQ(37, old.hp);
    EXPECT_EQ(37, next.hp);
    EXPECT_EQ(12, next.ammo);
}
```

Build `memory_tests` in **Debug | x64** and run `ScalarMigration.CopiesHpAndKeepsAmmoDefault` in **Test → Test Explorer**. Expect one pass. If `copy_field` doesn't link, check that its declaration is inside the C linkage guards and that the test project references `memory`.

## 3. Check that the copy follows the field

Change only `PlayerV2` to `typedef struct { int ammo; int hp; } PlayerV2;`, rebuild, and rerun. The case should still pass: the copy follows the new `offsetof(PlayerV2, hp)`. A whole-object copy would instead overwrite `ammo` and leave `hp` at zero.

For a separate rejection case, give `CopyOp` a valid source offset but set `new_offset` to `sizeof next` with a nonzero `size`. Expect `copy_field` to return false and the destination values to remain unchanged.

Next: [02 · Put the engine's memory index at byte zero](02-memory-index-tutorial.md).
