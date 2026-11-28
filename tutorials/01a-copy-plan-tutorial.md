# 01a · Build a copy plan from two layout tables

After [Step 01](01-first-memory-migration-test.md), keep `copy_field` as the executor. Now write the part that decides **which bytes are allowed to move**. The fixture knows both `Player` types; the planner never includes either declaration. No DLLs, generated files, or Windows pages yet.

## 1. Describe the layouts

Add `memorymigration/memory/engine_api.h` to the `memory` project. These types describe the shape of a block from the outside. A field offset is relative to its region; a region offset is relative to the block. A kind is not a size: `i32` and `u32` are both four bytes but have different meanings in this exercise.

```c
#ifndef ENGINE_API_H
#define ENGINE_API_H
#include <stdint.h>

enum { STORAGE_I32 = 1, STORAGE_U32 = 2 };

typedef struct {
    const char *name;
    uint32_t kind;
    uint64_t offset, size, alignment;
} FieldLayout;

typedef struct {
    const char *name;
    uint32_t field_count;
    uint64_t offset, size, alignment;
    const FieldLayout *fields;
} RegionLayout;

typedef struct {
    uint64_t block_size, block_alignment;
    uint32_t region_count;
    const RegionLayout *regions;
} EngineLayout;
#endif
```

In `memorymigration/memory/memory.h`, include `engine_api.h` and add this declaration inside the existing C linkage guards. The caller provides room for `limit` operations. On failure `*count` is zero; don't execute any entries, even if the planner wrote a partial list.

```c
bool build_copy_plan(const EngineLayout *src, const EngineLayout *dest,
                     CopyOp *ops, size_t limit, size_t *count);
```

## 2. Validate first, then match by name

Append the following to `memorymigration/memory/memory.c`, which already includes `<string.h>`. This checkpoint accepts trusted, readable descriptor arrays and name strings, one scalar storage width, and non-overlapping layouts. It checks numeric ranges and rejects duplicate names. These checks cannot make an arbitrary pointer safe to dereference. The caller must also verify each advertised block size against its actual allocation before executing the plan.

```c
static bool fits(uint64_t offset, uint64_t size, uint64_t limit)
{
    /* Check before subtracting so an invalid start cannot wrap. */
    return offset <= limit && size <= limit - offset;
}

static bool valid_layout(const EngineLayout *layout)
{
    if (layout->block_size > SIZE_MAX ||
        (layout->region_count && !layout->regions))
        return false;
    for (uint32_t i = 0; i < layout->region_count; ++i) {
        const RegionLayout *r = &layout->regions[i];
        if (!r->name || !fits(r->offset, r->size, layout->block_size) ||
            (r->field_count && !r->fields))
            return false;
        for (uint32_t k = 0; k < i; ++k)
            if (strcmp(r->name, layout->regions[k].name) == 0)
                return false;
        for (uint32_t j = 0; j < r->field_count; ++j) {
            const FieldLayout *f = &r->fields[j];
            if (!f->name || (f->kind != STORAGE_I32 && f->kind != STORAGE_U32) ||
                f->size != sizeof(int32_t) || !fits(f->offset, f->size, r->size))
                return false;
            for (uint32_t k = 0; k < j; ++k)
                if (strcmp(f->name, r->fields[k].name) == 0)
                    return false;
        }
    }
    return true;
}

bool build_copy_plan(const EngineLayout *src, const EngineLayout *dest,
                     CopyOp *ops, size_t limit, size_t *count)
{
    size_t n = 0;
    *count = 0;
    if (!valid_layout(src) || !valid_layout(dest))
        return false;

    for (uint32_t i = 0; i < src->region_count; ++i) {
        const RegionLayout *a = &src->regions[i];
        for (uint32_t j = 0; j < dest->region_count; ++j) {
            const RegionLayout *b = &dest->regions[j];
            if (strcmp(a->name, b->name) != 0)
                continue;
            for (uint32_t x = 0; x < a->field_count; ++x) {
                const FieldLayout *from = &a->fields[x];
                for (uint32_t y = 0; y < b->field_count; ++y) {
                    const FieldLayout *to = &b->fields[y];
                    if (strcmp(from->name, to->name) != 0)
                        continue;
                    if (from->kind != to->kind || from->size != to->size || n == limit)
                        return false;
                    /* Containment checks proved both sums fit their blocks. */
                    ops[n++] = (CopyOp){(size_t)(a->offset + from->offset),
                                        (size_t)(b->offset + to->offset),
                                        (size_t)from->size};
                }
            }
        }
    }
    *count = n;
    return true;
}
```

A new field never matches an old field, so no operation can overwrite its initialized value. [`strcmp`](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) (C99 §7.21.4.2) compares the string contents, not the addresses where two builds happen to store the names.

## 3. Test the plan separately from the copy

Append to `memorymigration/memory_test/test.cpp`. The reordered fixture forces the offsets to differ on the MSVC x64 target. The table entries are hand-written, but [`offsetof`](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) (C99 §7.17) lets the compiler measure member offsets rather than relying on guessed padding.

```cpp
typedef struct { int32_t hp; } PlanPlayerV1;
typedef struct { int32_t ammo, hp; } PlanPlayerV2;

static const FieldLayout old_hp = {"hp", STORAGE_I32,
    offsetof(PlanPlayerV1, hp), sizeof(int32_t), alignof(int32_t)};
static const FieldLayout new_fields[] = {
    {"ammo", STORAGE_I32, offsetof(PlanPlayerV2, ammo), sizeof(int32_t), alignof(int32_t)},
    {"hp", STORAGE_I32, offsetof(PlanPlayerV2, hp), sizeof(int32_t), alignof(int32_t)}
};

static bool PlanPlayer(const FieldLayout *fields, uint32_t field_count,
                       CopyOp *op, size_t *count)
{
    const RegionLayout old_region = {"Player", 1, 0, sizeof(PlanPlayerV1),
                                     alignof(PlanPlayerV1), &old_hp};
    const RegionLayout new_region = {"Player", field_count, 0, sizeof(PlanPlayerV2),
                                     alignof(PlanPlayerV2), fields};
    const EngineLayout old_layout = {sizeof(PlanPlayerV1), alignof(PlanPlayerV1), 1, &old_region};
    const EngineLayout new_layout = {sizeof(PlanPlayerV2), alignof(PlanPlayerV2), 1, &new_region};
    return build_copy_plan(&old_layout, &new_layout, op, 1, count);
}

TEST(CopyPlan, MovesOnlyHp)
{
    CopyOp op = {0};
    size_t count = 0;
    ASSERT_TRUE(PlanPlayer(new_fields, 2, &op, &count));
    ASSERT_EQ(1u, count);
    EXPECT_EQ(offsetof(PlanPlayerV1, hp), op.old_offset);
    EXPECT_EQ(offsetof(PlanPlayerV2, hp), op.new_offset);
    EXPECT_EQ(sizeof(int32_t), op.size);

    PlanPlayerV1 old_player = {37};
    PlanPlayerV2 new_player = {12, 0};
    const MemoryBlock src = {(unsigned char *)&old_player, sizeof old_player};
    const MemoryBlock dest = {(unsigned char *)&new_player, sizeof new_player};
    ASSERT_TRUE(copy_field(&src, &dest, op));
    EXPECT_EQ(37, new_player.hp);
    EXPECT_EQ(12, new_player.ammo);
}

TEST(CopyPlan, RejectsSameSizeKindMismatch)
{
    FieldLayout bad = new_fields[1];
    bad.kind = STORAGE_U32;  /* Size still matches; meaning does not. */
    CopyOp op = {0};
    size_t count = 1;
    EXPECT_FALSE(PlanPlayer(&bad, 1, &op, &count));
    EXPECT_EQ(0u, count);
}

TEST(CopyPlan, RejectsFieldOutsideItsRegion)
{
    FieldLayout bad = new_fields[1];
    bad.offset = sizeof(PlanPlayerV2);  /* Valid end, no room for a field. */
    CopyOp op = {0};
    size_t count = 1;
    EXPECT_FALSE(PlanPlayer(&bad, 1, &op, &count));
    EXPECT_EQ(0u, count);
}
```

Build `memory_tests` and run `CopyPlan` in Test Explorer. Expect three passes. If `MovesOnlyHp` gets offset zero for the destination, check that the planner uses `to->offset`, not the source field offset. The `alignof` spelling is used only in the C++-compiled test fixture; the planner copies bytes and does not dereference typed payloads.

Next: [02 · Memory index](02-memory-index-tutorial.md), then [02a · Describe a live block](02a-live-layout-tutorial.md).
