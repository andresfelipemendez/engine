# 02 · Put the engine's memory index at byte zero

Continue the memory-index test you've started in `memorymigration/main.c`. Place the regions in fixed order—`Engine`, `Game`, `Editor`—then use the index to find `Game` in one block without searching. No new source files or directories are needed.

## Prerequisites

- Finish the [scalar migration test](01-first-memory-migration-test.md) and have a C compiler available.
- Open `memorymigration/memorymigration.slnx` in Visual Studio or use an x64 Native Tools Command Prompt. The existing project already includes `main.c` and `memory.h`.

## How the block is laid out

Start with one invariant: **the block begins with `Memory`, and the DLL that owns the block knows which version of `Memory` is there.** `Memory` is the index of top-level regions (`Engine`, `Game`, `Editor`). Those regions hold the actual state. The root stays at byte zero even if the index grows or the regions move.

```text
base + 0
  |
  v
  [ Memory: index entries ] [padding] [ Engine ] [ Game ] [ Editor ]
              |                          ^         ^          ^
              +---- Engine offset -------+         |          |
              +---- Game offset -------------------+          |
              +---- Editor offset ----------------------------+
```

An offset is relative to the block base, not to the address of its index entry. `Memory` doesn't list itself as a region to migrate. The new DLL creates its own index during ordinary initialization; copying the old index would replace correct new offsets with stale ones.

## Tasks

### Task 1: Make the slots fixed in `memorymigration/memory.h`

Open `memorymigration/memory.h`. Keep your existing `RegionType` enum in its current order. In `Region`, remove the `type` member: the slot already identifies the region. In `Memory`, remove `count` because all slots are populated, and size `regions` with `REGION_COUNT` instead of `3`. The complete header should look like this:

```c
#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

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
    Region regions[REGION_COUNT];
} Memory;

#endif
```

`Memory` lives at byte zero. Each `Region` records its start `offset` and `size` in bytes. `REGION_GAME` is a **slot number**, not a byte offset: `regions[REGION_GAME].offset` tells you where `Game` starts. `REGION_COUNT` is the number of slots, not a region. This header is private to the engine once the host and DLL are separate.

Engine code can use an `Engine *`, `Editor *`, or `Game *`, but get those as **temporary typed views** rather than storing raw pointers as the index entries. After you've found and checked an entry, the address is `base + entry.offset` with `base` an `unsigned char *`. Cast that address to the expected type only after checking the slot and validating the entry's bounds, size, and alignment. Don't keep that pointer after switching blocks.

The enum above is convenient **within one DLL**. For matching between builds, keep using region names from your `.mem` declarations, as in the [DLL migration tutorial](06-dll-migration-tutorial.md). Don't assume an enum ordinal from one build has the same meaning in another.

### Task 2: Finish placement and lookup in `memorymigration/main.c`

Open `memorymigration/main.c`. Keep the existing includes, `Marker`, and scalar migration code. In `init_block`, remove the `order` parameter and walk the enum slots from `0` through `REGION_COUNT - 1`. Store each entry in `index->regions[type]`. Keep your direct `find_region` lookup above `test_memory_index`. The two helpers should look like this:

```c
static int init_block(unsigned char *base, size_t capacity)
{
    if (capacity < sizeof(Memory))
        return 0;

    Memory *index = (Memory *)base;
    *index = (Memory){0};
    size_t cursor = sizeof(*index);
    const size_t stride = sizeof(Marker);
    const size_t padding = (stride - cursor % stride) % stride;

    if (padding > capacity - cursor ||
        REGION_COUNT > (capacity - cursor - padding) / stride)
        return 0;
    cursor += padding;

    for (size_t i = 0; i < REGION_COUNT; ++i) {
        const RegionType type = (RegionType)i;
        index->regions[type] = (Region){cursor, stride};
        *(Marker *)(base + cursor) = (Marker){10 * (1 + (int)type)};
        cursor += stride;
    }
    return 1;
}

static const Region *find_region(const Memory *index, RegionType type)
{
    if ((unsigned)type >= REGION_COUNT)
        return NULL;
    return &index->regions[type];
}
```

`malloc` gives this fixture an aligned base. Rounding up to a multiple of `sizeof(Marker)` puts every marker at a valid array-style stride from that base; no C11 `_Alignof` is needed. This placement rule is for the one-type fixture. When the regions have different types, align each region separately as in [03 · Arena allocation](03-arena-tutorial.md). `find_region` checks the slot number, not the entry's contents. Use it on a block that `init_block` initialized successfully.

### Task 3: First test

In `memorymigration/main.c`, replace the unfinished `test_memory_index` below `find_region`. Remove `orders`, `game_offsets`, and the loop over two blocks. One block is enough: look up `REGION_GAME`, check the entry, then add the entry's offset to the block base to read the marker.

```c
static int test_memory_index(void)
{
    const size_t capacity = sizeof(Memory) + (REGION_COUNT + 1) * sizeof(Marker);
    unsigned char *block = malloc(capacity);
    if (!block || !init_block(block, capacity)) {
        fputs("FAIL: could not initialize block\n", stderr);
        free(block);
        return EXIT_FAILURE;
    }

    const Region *game = find_region((const Memory *)block, REGION_GAME);
    if (!game || game->offset < sizeof(Memory) ||
        game->offset > capacity || game->offset % sizeof(Marker) != 0 ||
        game->size != sizeof(Marker) || game->size > capacity - game->offset) {
        fputs("FAIL: invalid Game entry\n", stderr);
        free(block);
        return EXIT_FAILURE;
    }

    const Marker *marker = (const Marker *)(block + game->offset);
    printf("Game offset=%zu marker=%d\n", game->offset, marker->value);
    if (marker->value != 20) {
        fputs("FAIL: got the wrong marker\n", stderr);
        free(block);
        return EXIT_FAILURE;
    }

    free(block);
    puts("PASS: found Game in its fixed slot");
    return EXIT_SUCCESS;
}
```

In your existing `main`, make the scalar-value mismatch branch return `EXIT_FAILURE` after printing its error. Then replace the final `return EXIT_SUCCESS;` **in `main`** with `return test_memory_index();`. That runs the new test only after the scalar test passes and propagates its result. Do not add a second `main`.

Save `memorymigration/main.c`. In Visual Studio, set **memorymigration** as the startup project, select **x64**, and run it. Or, from the repository root in an **x64 Native Tools Command Prompt**, run the checkpoint directly without building the generator:

```cmd
cd memorymigration
cl /nologo /W4 /TC /std:c11 main.c /Fo:"%TEMP%\memorymigration-index-check.obj" /Fe:"%TEMP%\memorymigration-index-check.exe"
"%TEMP%\memorymigration-index-check.exe"
```

The command builds the existing `main.c`; only the temporary output filenames are new. MSVC's `/std:c11` accepts the C99 code in the example.

Expected output on a typical x64 build (the offset can vary by ABI):

```text
Game offset=52 marker=20
PASS: found Game in its fixed slot
```

- **Cannot open `memory.h`:** Check that you're building the `main.c` in `memorymigration/`; `memory.h` is beside it.
- **Wrong marker:** Check that `REGION_GAME` is the second enum value (slot `1`, so the fixture writes `10 * (1 + 1)`) and that lookup adds the recorded offset to the **block base**.

**Try it:** In `test_memory_index`, change the capacity to `sizeof(Memory)`. Rebuild and run. You should get `FAIL: could not initialize block`, not a crash. Restore the original capacity before continuing.

## Review

You found `Game` in one block through its fixed index slot. Two blocks initialized with the same layout would give `Game` the same offset, so there's no offset-change assertion here. Across builds, the slot can stay the same while the byte offset changes—for example, if the preceding `Engine` region grows—without changing the placement order. This checkpoint does **not** migrate region data or build a DLL.

This prototype can include `memory.h` because it plays both roles in one program. Once you split the DLL from the host, keep placement, lookup, and the private header on the engine side. When the generator takes ownership of `memory.h`, edit the generator rather than a generated copy.

## Keep the region index separate from field metadata

The index can tell you **where `Game` starts**. It cannot tell you **where `hp` sits inside `Game`**, or what kind of field it is. C doesn't enumerate struct members at runtime. The `.mem` generator emits a separate field table with each field's name, storage kind, compiler-computed `offsetof`, size, and alignment. This table is required for migration: [Tutorial 05, §5](05-c-codegen-tutorial.md#5-emit-the-tables-and-shared-api) shows how to generate it from the same declarations as `memory.h`.

| Information | Where it comes from |
| --- | --- |
| `Game` starts at byte 128 of this particular block | The DLL's `Memory` index |
| `hp` starts at byte 4 inside `Game` | The generated field table, measured by the C compiler |
| `hp` starts at byte 132 of this block | `128 + 4`, after validating both ranges |

When asked to describe a **live** block, the old DLL reads its own `Memory` at byte zero, pairs each region's actual placement with the generated field table, and returns a layout view through the shared ABI. The new DLL does the same for its own block. If placements vary by instance, use caller-owned output storage for the entries built during this call, or otherwise give them a clearly defined lifetime. Do not return pointers to local arrays.

**Byte zero is a location contract, not a layout contract.** If v2 adds an index entry or changes `Memory`, v2 must not cast the old block to its own `Memory *`. The host shouldn't cast either block. It asks old code to describe old memory and new code to describe new memory, then copies only the matching payload fields. New index bytes are never part of the copy plan.

## Optional follow-up: let strings grow backward from the end

After Task 3, open `memorymigration/memory.h` and check for `size_t front;` and `size_t back;` in `Memory`. In `memorymigration/main.c`, check that `init_block` sets `index->front = cursor;` and `index->back = capacity;` after placing the regions. Your prototype already has both fields and assignments; don't add them a second time.

Keep two cursors in the engine's `Memory` index. `front` is the next free byte after the index, regions, and carved-out sub-arenas. `back` is the first occupied byte of the string pool, or `capacity` when the pool is empty. Both are offsets from the block base.

```text
0                       front                   back           capacity
| [Memory][regions][sub-arenas] -> | free space | <- [strings] |
```

The front allocator grows up; the string arena grows down. Treat the gap `[front, back)` as shared free space. Before an allocation on either side, check that `front <= back` and that the request, including any padding, fits in `back - front`. Check `back <= capacity` when validating the index. Neither allocator can reserve space the other already owns.

### Store references, not `char *` pointers

Use the existing `StringRef` in `memorymigration/memory.h` rather than adding another definition. A persistent field can use it to hold a block-relative offset and a byte count:

```c
typedef struct {
    size_t offset;  /* From the block base; 0 can mean absent. */
    size_t length;  /* Excludes the optional C string terminator. */
} StringRef;
```

For this exercise, always store a trailing `\0` so the engine can use the bytes as a C string when appropriate. To append `n` bytes, first check that adding one won't overflow and that `n + 1` fits in the gap. Then subtract `n + 1` from `back`, copy the bytes to `base + back`, write the terminator, and return `{back, n}`. No alignment padding is needed for character bytes. For example, in a 256-byte block with an empty pool, storing `"door"` uses five bytes and moves `back` from `256` to `251`; the reference is `{251, 4}`.

When resolving a reference, check that it is present, that its offset is within the occupied string pool, and that the bytes **and terminator** fit in the block. Only then form a temporary pointer. Treat strings as immutable: replacing one appends a new string and changes the reference. A bump arena doesn't reclaim individual strings. Don't reset or compact the pool while live references still point into it.

### Task 1: Append bytes and return a `StringRef`

In `memorymigration/main.c`, add `append_string` **after `init_block` and before `test_memory_index`**. It uses `<stdbool.h>` and `<string.h>`, which the file already includes. The caller supplies a readable byte range `text[0..length)`. The function writes the new reference through `out` only on success.

```c
static bool append_string(unsigned char *base, size_t capacity,
                          const char *text, size_t length, StringRef *out)
{
    if (capacity < sizeof(Memory))
        return false;

    Memory *index = (Memory *)base;
    const bool cursorsAreInvalid = index->front < sizeof(Memory) ||
        index->front > index->back || index->back > capacity;
    if (cursorsAreInvalid)
        return false;

    const size_t available = index->back - index->front;
    const bool noRoomForBytesAndTerminator = length >= available;
    if (noRoomForBytesAndTerminator)
        return false;

    const size_t offset = index->back - (length + 1);
    memmove(base + offset, text, length);
    base[offset + length] = '\0';
    index->back = offset;
    *out = (StringRef){offset, length};
    return true;
}
```

`length >= available` rejects a request even if the text bytes fit but the terminator doesn't. Doing that check **before** adding one also avoids overflow. With a capacity of `256`, `append_string` puts `door\0` at bytes `251`–`255`. The next append puts `open\0` at bytes `246`–`250`. It doesn't move `door`. `front` stays where `init_block` left it.

### Task 2: Resolve a reference into a temporary pointer

Add `lookup_string` **below `append_string`** in the same file. It returns `NULL` for a reference outside the occupied pool or one whose terminator is missing. It doesn't allocate anything.

```c
static const char *lookup_string(const unsigned char *base, size_t capacity,
                                 StringRef ref)
{
    if (capacity < sizeof(Memory))
        return NULL;

    const Memory *index = (const Memory *)base;
    const bool cursorsAreInvalid = index->front < sizeof(Memory) ||
        index->front > index->back || index->back > capacity;
    if (cursorsAreInvalid)
        return NULL;

    const bool offsetIsOutsidePool = ref.offset < index->back ||
        ref.offset >= capacity;
    if (offsetIsOutsidePool)
        return NULL;

    const size_t bytesRemaining = capacity - ref.offset;
    const bool terminatorWouldBeOutsideBlock = ref.length >= bytesRemaining;
    if (terminatorWouldBeOutsideBlock || base[ref.offset + ref.length] != '\0')
        return NULL;

    return (const char *)(base + ref.offset);
}
```

Pass an aligned block initialized by `init_block` to both helpers, and pass a valid output pointer to `append_string`. These checks make it safe to resolve the references the allocator returned; they don't prove that an arbitrary offset is the start of an actual allocation. Keep the `StringRef` for later use, not the returned pointer, which is only valid while the backing block stays alive at that address.

### Task 3: Exercise both helpers from `test_memory_index`

1. In `test_memory_index`, change `capacity` to `256`. If you're still using `sizeof(Memory)` from the previous failure test, the block can't even hold the regions, so the string code won't run.
2. In `memorymigration/main.c`, add this test helper **below `lookup_string` and above `test_memory_index`**. It borrows the initialized block and leaves freeing it to `test_memory_index`.

```c
static bool test_string_pool(unsigned char *block, size_t capacity)
{
    StringRef door, open;
    if (!append_string(block, capacity, "door", sizeof("door") - 1, &door) ||
        !append_string(block, capacity, "open", sizeof("open") - 1, &open)) {
        fputs("FAIL: could not append test strings\n", stderr);
        return false;
    }

    const char *doorText = lookup_string(block, capacity, door);
    const char *openText = lookup_string(block, capacity, open);
    if (!doorText || !openText || strcmp(doorText, "door") != 0 ||
        strcmp(openText, "open") != 0 || open.offset >= door.offset) {
        fputs("FAIL: wrong string contents or placement\n", stderr);
        return false;
    }
    printf("%s: offset=%zu length=%zu\n", doorText, door.offset, door.length);
    printf("%s: offset=%zu length=%zu\n", openText, open.offset, open.length);

    const Memory *index = (const Memory *)block;
    const size_t frontBefore = index->front;
    const size_t backBefore = index->back;
    const char tooLong[256] = {0};
    StringRef unused;
    const bool oversizedAppendSucceeded =
        append_string(block, capacity, tooLong, sizeof tooLong, &unused);
    if (oversizedAppendSucceeded || index->back != backBefore ||
        index->front != frontBefore) {
        fputs("FAIL: oversized append succeeded or moved a cursor\n", stderr);
        return false;
    }

    printf("PASS: oversized append rejected; back still %zu\n", index->back);
    return true;
}
```

3. In `test_memory_index`, insert this call **after checking `marker->value` and before the final `free(block)`**. Leave that final `free` in place for the success path.

```c
if (!test_string_pool(block, capacity)) {
    free(block);
    return EXIT_FAILURE;
}
```

4. At the end of `main`, use `return test_memory_index();` instead of calling it and then using a bare `return;`. This gives the console the test's success or failure status.

Rebuild `memorymigration/main.c` with the same command as Task 3 of the memory-index test. Alongside the `Game` output, expect:

```text
door: offset=251 length=4
open: offset=246 length=4
PASS: oversized append rejected; back still 246
```

If the offsets are one byte lower than expected, check that the caller passes `sizeof("door") - 1`, not `sizeof("door")`: `append_string` reserves the terminator itself. If the rejected append moves `back`, check that the fit test runs before updating the cursor. For a boundary test, pass `backBefore - frontBefore` instead of `sizeof tooLong` as the last request's **length**. Those bytes fit exactly, but there's no byte left for the terminator, so it should still fail without changing either cursor.

### A string reference needs migration semantics

Copying `{offset, length}` by itself is **not** enough: a new block can have a different capacity or place the same string at a different offset. For the first scalar-only migration test, leave strings out of the copy plan. Before supporting them, add a distinct storage kind such as `STRING_REF` to the layout metadata. That tells the host not to treat the field as two unrelated integers.

One way to support it while keeping `Memory` opaque is to export ordinary string operations through the shared API: resolve a reference using the old DLL, then append those bytes to the new block using the new DLL. Give the host an ABI-stable string reference format or have the exports read and write the reference fields on its behalf. The host coordinates the copy and installs the **new** reference; the engines only perform normal lookups and allocations. Leave an absent reference absent, and discard the candidate if a string doesn't fit. You can preserve shared references with an old-to-new reference map later if you need it.

## Optional follow-up: carve out sub-arenas for other DLL allocations

Use the front allocator to reserve **non-overlapping ranges** for sub-arenas. For example, give `Engine`, `Game`, and `Editor` a permanent sub-arena each, and reserve a separate scratch sub-arena for temporary work. Record a sub-arena's block-relative start, capacity, and used bytes in the engine's index; its local allocations advance only its own cursor. Moving a sub-arena's local cursor doesn't move the root `front`: the whole range was already reserved. Give each sub-arena the alignment it needs when you carve it out, and still align each typed allocation within it.

- **Persistent sub-arenas** hold data that should survive reload. Use offsets or handles for persistent references. The index and generated metadata must describe the live objects you want to migrate; having a sub-arena does not make its arbitrary contents discoverable. For the first version, migrate declared regions and leave arbitrary sub-arena objects out until you can enumerate them and fix up their references.
- **Scratch sub-arenas** hold temporary allocations. Reset them at a defined point (for example, the end of a frame). Never keep a persistent reference into scratch or include scratch bytes in the migration plan.

The host still owns the virtual pages; the DLL's arena cursors only partition the memory it was given. Start with the block fully committed. If you later commit pages on demand, both ends can need commits, and that must happen before either allocator writes into a newly used page.

## Next lesson

Continue with [03 · Arena allocation](03-arena-tutorial.md). Come back to the sub-arena exercise here before [07 · Array migration](07-array-migration-tutorial.md).
