# 02a · Describe a live block without exposing its index

Finish [the planner](01a-copy-plan-tutorial.md) and [the byte-zero index](02-memory-index-tutorial.md). This test changes where `Game` starts without changing where `hp` sits inside `Game`. The engine will supply both pieces of information. The planner will still use only `EngineLayout`.

## 1. Combine live placement with a field table

In `memorymigration/memory/memory.h`, declare this function inside the C linkage guards. Its caller owns `region`, so the returned `EngineLayout.regions` can point to it without referring to a local variable that disappears on return. Keep `region` and the field table alive until planning finishes.

```c
bool describe_game(MemoryBlock block, const FieldLayout *fields,
                   uint32_t count, RegionLayout *region, EngineLayout *layout);
```

Append the implementation to `memorymigration/memory/memory.c`. This is an **engine-side** helper for already initialized blocks, not a new operation the host performs on arbitrary bytes.

```c
bool describe_game(MemoryBlock block, const FieldLayout *fields,
                   uint32_t count, RegionLayout *region, EngineLayout *layout)
{
    if (!block.base || block.capacity < sizeof(Memory))
        return false;
    const Memory *index = (const Memory *)block.base;
    const Region *game = memory_find_region(index, REGION_GAME);
    if (!game || game->offset < sizeof(Memory) ||
        !fits(game->offset, game->size, block.capacity))
        return false;

    /* Placement comes from this instance; field offsets come from its type. */
    *region = (RegionLayout){"Game", count, game->offset, game->size,
                             sizeof(int32_t), fields};
    *layout = (EngineLayout){block.capacity, 1, 1, region};
    return true;
}
```

This fixture describes only `Game`. The block alignment of `1` is a placeholder for this already allocated fixture, not an allocation request for a real DLL. `fits` is the private helper from Step 01a. The generic planner doesn't use typed loads and doesn't need the alignment columns to compute byte offsets.

## 2. Move the region, not the field

Append to `memorymigration/memory_test/test.cpp`. These fixture structs stand in for two engine builds. The setup writes the index directly so the test isolates placement from the allocator. The descriptor passed to the planner is identical in shape for both builds, but its region offset differs.

```cpp
typedef struct { int32_t hp; } LiveGame;
typedef struct { Memory index; int32_t engine[1]; LiveGame game; } LiveV1;
typedef struct { Memory index; int32_t engine[2]; LiveGame game; } LiveV2;

TEST(LiveLayout, FindsHpAfterGameMoves)
{
    LiveV1 old_block = {0};
    LiveV2 new_block = {0};
    old_block.game.hp = 37;
    new_block.engine[1] = 99;  /* A stale offset would hit this value. */
    old_block.index.regions[REGION_GAME].offset = offsetof(LiveV1, game);
    old_block.index.regions[REGION_GAME].size = sizeof(LiveGame);
    new_block.index.regions[REGION_GAME].offset = offsetof(LiveV2, game);
    new_block.index.regions[REGION_GAME].size = sizeof(LiveGame);
    const MemoryBlock src = {(unsigned char *)&old_block, sizeof old_block};
    const MemoryBlock dest = {(unsigned char *)&new_block, sizeof new_block};
    const FieldLayout hp = {"hp", STORAGE_I32, offsetof(LiveGame, hp),
                            sizeof(int32_t), alignof(int32_t)};
    RegionLayout old_region = {0}, new_region = {0};
    EngineLayout old_layout = {0}, new_layout = {0};
    ASSERT_TRUE(describe_game(src, &hp, 1, &old_region, &old_layout));
    ASSERT_TRUE(describe_game(dest, &hp, 1, &new_region, &new_layout));
    ASSERT_NE(old_region.offset, new_region.offset);

    /* From here until verification, use only descriptors and byte views. */
    CopyOp op = {0};
    size_t count = 0;
    ASSERT_TRUE(build_copy_plan(&old_layout, &new_layout, &op, 1, &count));
    ASSERT_EQ(1u, count);
    EXPECT_EQ(old_region.offset + hp.offset, op.old_offset);
    EXPECT_EQ(new_region.offset + hp.offset, op.new_offset);
    ASSERT_TRUE(copy_field(&src, &dest, op));
    EXPECT_EQ(37, new_block.game.hp);
    EXPECT_EQ(99, new_block.engine[1]);
}
```

Build `memory_tests` and run `LiveLayout.FindsHpAfterGameMoves` in Test Explorer. Expect it to pass. Temporarily make the new descriptor use `old_region.offset` *after* the offset assertions and rebuild the plan, or simply substitute that offset in the copy operation. The copy still fits the block, but the wrong bytes change. This is why bounds checks cannot replace correct layout descriptions. Restore the original code.

Next: [03 · Arena allocation](03-arena-tutorial.md). In [05 · Code generation](05-c-codegen-tutorial.md), the generator supplies the field table; live region placement still comes from this build's index.
