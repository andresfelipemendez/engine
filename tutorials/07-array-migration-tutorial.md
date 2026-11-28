# 07 · Migrate arrays between memory blocks

A string is a byte array with a few extra rules: its length may exclude a trailing `\0`, and you may want to validate its encoding. Start with **arrays of `i32`** so you can learn the general migration rules without those details.

There are two cases: **inline arrays**, whose elements live inside the struct, and **arena-backed arrays**, whose struct stores a reference to elements elsewhere in the block. Do them in that order. The first case reuses the [single-field migration test](01-first-memory-migration-test.md); the second builds on the [memory index and sub-arenas](02-memory-index-tutorial.md).

## 1. Inline arrays: copy elements, not the whole struct

Append the following to `memorymigration/memory_test/test.cpp` in the same Google Test project as [Step 01](01-first-memory-migration-test.md). Add `#include <stdint.h>` with the other includes. Reuse `copy_field` from `memory.lib`; leave `.mem` and the DLLs alone for this checkpoint.

```cpp
typedef struct { int32_t scores[2]; } GameV1;
typedef struct { int32_t scores[3]; } GameV2;

TEST(ArrayMigration, PreservesDefaultInNewSlot)
{
    GameV1 old_game = {{10, 20}};
    GameV2 new_game = {{-1, -1, -1}};
    const MemoryBlock src = {(unsigned char *)&old_game, sizeof old_game};
    const MemoryBlock dest = {(unsigned char *)&new_game, sizeof new_game};
    /* Only two elements exist in the source; leave the new slot alone. */
    const CopyOp op = {offsetof(GameV1, scores), offsetof(GameV2, scores),
                       sizeof old_game.scores};

    ASSERT_TRUE(copy_field(&src, &dest, op));
    EXPECT_EQ(10, new_game.scores[0]);
    EXPECT_EQ(20, new_game.scores[1]);
    EXPECT_EQ(-1, new_game.scores[2]);
}
```

The field description for each version needs the **array offset within the struct**, the **element kind**, the **element stride** in bytes, and the **number of elements**. For a C array, elements are contiguous and the stride is `sizeof(element)` ([C99 §§6.2.5 and 6.5.3.4](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf)). Use the generated C code to obtain the offset with `offsetof` and the count with `sizeof(array) / sizeof(array[0])` when `array` is the actual member, not a pointer. The host doesn't need `GameV1` or `GameV2` as C types.

For this exercise, explicitly allow a fixed array to grow or shrink: copy `min(old_count, new_count)` elements, leave new entries initialized, and drop trailing entries if the array shrank. If dropping game data isn't acceptable, reject shrinking instead. A changed element kind is an error even if both kinds occupy four bytes.

For identical scalar elements, the copy can be one `memcpy`. Before computing the byte count, check that the stride is nonzero and `count <= SIZE_MAX / stride`. Then reuse the source and destination bounds checks from the single-field test. For the example, the copy is two `i32` elements, not three. The old array doesn't have a third element to read.

**Checkpoint:** Build the Google Test project and run `ArrayMigration.PreservesDefaultInNewSlot` in Test Explorer. Expect one pass. Temporarily use `sizeof new_game.scores` as the byte count. The copy should now be rejected because it requests three elements from a two-element source. Restore the original byte count. Kind validation belongs in the later metadata-driven copy planner, not this byte-copy helper.

## 2. Arena-backed arrays: move the contents and rebuild the reference

Once inline arrays work, plan the arena-backed case as a separate test in the same project. Start with this reference type above the new case in `test.cpp`. Store a reference, not a C pointer, in persistent memory. In the later DLL experiment, put this type in the shared API header only if the host and both DLLs need to exchange it directly.

```c
#include <stdint.h>

typedef struct {
    uint64_t offset;    /* Start of the elements, relative to the block base. */
    uint64_t count;     /* Live elements. */
    uint64_t capacity;  /* Allocated element slots, not bytes. */
} ArrayRef;
```

For the prototype, use `{0, 0, 0}` as the canonical empty reference, since byte zero is occupied by `Memory`. A reserved array can also have `count == 0` and a nonzero offset and capacity. The declared field metadata says **this field stores an `ArrayRef`**, and describes the element kind, stride, and required alignment. The `ArrayRef` in a live block supplies the current offset, count, and capacity. Neither the reference nor the metadata is enough on its own.

```text
OLD  Game.scores = {offset: 128, count: 2, capacity: 4}
     old base + 128 → [10] [20] [unused] [unused]

NEW  Game.scores = {offset: 200, count: 2, capacity: 2}
     new base + 200 → [10] [20]
```

A valid migration can change both **offset** and **capacity**. It preserves the live values and their order. Copying the old `ArrayRef` as an ordinary field would incorrectly point to offset 128 in the new block.

### How the host and DLL cooperate

1. **Read the old reference** using the field offset and shared reference format. If reading generic block bytes, `memcpy` the validated reference bytes into a local `ArrayRef` rather than casting an arbitrary byte address. Check `count <= capacity`. For a nonempty allocation, check the offset, alignment, and that `capacity <= bytes_remaining / stride` before multiplying; also verify the backing range belongs to the owning sub-arena, not just somewhere in the block. The old DLL can expose an ordinary array lookup operation to validate that ownership without the host interpreting `Memory`.
2. **Allocate new storage** through an ordinary allocation export in the new DLL. Give it the candidate block, the arena or array identity, and the required element count. Let the DLL choose an aligned range from the owning sub-arena and return the new offset and capacity. For the test, allocating exactly the live count is enough. The host must check that the returned range fits and is large enough before it writes into it.
3. **Copy the live elements**, not the unused capacity. For matching scalar types and strides, copy `count * stride` bytes; the value and order remain the same. If there are zero live elements, no byte copy is needed; you can keep the candidate's normal empty representation.
4. **Install the new reference** in the candidate's array field only after that allocation and copy succeed. If anything fails, discard the candidate block. The old block and its reference remain unchanged.

The DLLs know how to resolve and allocate arrays in their own arenas. **The host performs the migration**, just as it does for scalars. For this first version, give every array a single owner and don't preserve aliases or interior references. Two fields pointing to the same allocation need a remapping policy before you can promise to preserve that relationship.

## 3. Keep string-specific behavior separate

Both arrays and strings follow the same outline: validate the old range, allocate in the candidate, copy live data, and install a new reference. A string can still use the backward-growing string arena while other arrays use ordinary sub-arenas. Keep a separate string kind in the metadata so the migration counts the live bytes correctly and reserves room for a terminator when needed. A generic `u8` array does **not** automatically get a trailing zero.

## 4. Only then try arrays of structs

For `Enemy[]`, the old and new **element strides can differ** if `Enemy` gains or reorders fields. Use the old stride to find old element `i`, the new stride to find new element `i`, and apply the element's own field-copy plan to each pair. Initialize new elements or fields before copying so their defaults survive. Don't use one raw `memcpy` across changed struct layouts.

**Next scratch test:** use two differently sized blocks. Put `{10, 20}` at an old array offset, allocate the candidate array at a different offset, and verify the values through the new reference. Then corrupt the old count so it exceeds capacity, and make sure the migration rejects it without modifying the old block. Only after those tests pass should you add array syntax to your `.mem` language.
