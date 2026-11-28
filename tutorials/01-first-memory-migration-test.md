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

[`memcpy`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/memcpy-wmemcpy) copies a specified number of bytes from a source address to a destination address. It doesn't know what `Player` is or how large either buffer is. Here, we want to copy only the bytes of `hp`, from its offset in the old layout to its independently measured offset in the new one. Copying the whole struct could overwrite `ammo` or put `hp` in the wrong slot. This exercise uses the same `int` representation on both sides; `memcpy` does not convert types.

```c
bool copy_field(const MemoryBlock *src, const MemoryBlock *dest, CopyOp op)
{
    /* On each side, check the start before subtracting it from capacity. */
    if (op.old_offset > src->capacity ||
        op.size > src->capacity - op.old_offset ||
        op.new_offset > dest->capacity ||
        op.size > dest->capacity - op.new_offset)
        return false;

    /* Both ranges fit. Copy this field, leaving other bytes unchanged. */
    memcpy(dest->base + op.new_offset, src->base + op.old_offset, op.size);
    return true;
}
```

Read the four conditions as two questions per block:

| Check | Why it matters |
| --- | --- |
| `op.old_offset > src->capacity` | The read can't start past the source block. |
| `op.size > src->capacity - op.old_offset` | Even a valid start may leave too few bytes to read. |
| `op.new_offset > dest->capacity` | The write can't start past the destination block. |
| `op.size > dest->capacity - op.new_offset` | Even a valid start may leave too few bytes to write. |

For example, offset 6 in an 8-byte block leaves `8 - 6 = 2` bytes. A four-byte copy would go past the end, so the function returns `false` without copying anything. We check the start before subtracting because `size_t` is unsigned: `8 - 9` would wrap to a large number, not a negative one. `||` evaluates left to right and skips the subtraction on that side if the starting offset is already invalid ([WG14 C99 draft, §§6.2.5, 6.5.14](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf)).

## 2. Add and run the unit test

Append this test to the `memory_tests` project's `test.cpp` (`memorymigration/memory_test/test.cpp` in the checkout). The test fixture knows both layouts; the C function only sees byte ranges. `offsetof` measures each layout independently.

```cpp
typedef struct { int hp; } PlayerV1;
typedef struct { int hp; int ammo; } PlayerV2;

TEST(ScalarMigration, CopiesHpAndKeepsAmmoDefault)
{
    PlayerV1 old = {37};
    PlayerV2 next = {0};
    /* A distinct default makes an accidental write into ammo visible. */
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

Append a separate case to the same `test.cpp`. Start the write at the end of the destination: the offset itself isn't past the limit, but no bytes remain. This exercises the fourth check independently.

```cpp
TEST(ScalarMigration, RejectsCopyPastDestination)
{
    PlayerV1 old = {37};
    PlayerV2 storage[2] = {0};
    storage[0].ammo = 12;
    storage[1].hp = -1;
    const MemoryBlock src = {(unsigned char *)&old, sizeof old};
    /* Expose only the first player; the second catches a stray write. */
    const MemoryBlock dest = {(unsigned char *)&storage, sizeof storage[0]};
    const CopyOp op = {offsetof(PlayerV1, hp), dest.capacity, sizeof old.hp};

    EXPECT_FALSE(copy_field(&src, &dest, op));
    EXPECT_EQ(0, storage[0].hp);
    EXPECT_EQ(12, storage[0].ammo);
    EXPECT_EQ(-1, storage[1].hp);
}
```

Rebuild `memory_tests` and run the `ScalarMigration` group. Expect two passes. If the rejection test fails, check that the destination length comparison uses `dest->capacity - op.new_offset`, not the full destination capacity.

Next: [01a · Build a copy plan from two layout tables](01a-copy-plan-tutorial.md).
