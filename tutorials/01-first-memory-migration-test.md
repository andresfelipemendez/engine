# 01 · First memory migration test: add `ammo` to `Player`

Start with two structs in one C99 program. The old layout has `hp`; the new layout has `hp` and `ammo`. Give the old player **37 HP** and the new player a default of **12 ammo**. After migration, expect `hp = 37` and `ammo = 12`; the old player must still have 37 HP.

No TOML, DLLs, `VirtualAlloc`, or reload loop yet. The two stack objects provide separate, correctly aligned blocks of memory. The test knows both C types so it can set up and check the fixture. The **copy function** gets only byte buffers, their lengths, and a copy operation. That is the part that will eventually belong to your host.

## 1. Create `migration_test.c`

Create a folder named `scalar-migration-lab` outside the repository. In that folder, create `migration_test.c` with the complete program below. Leave the engine's `memorymigration/main.c` untouched while you work through the scratch test.

```c
#include <stddef.h>  /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  /* memcpy */

typedef struct { int hp; } PlayerV1;
typedef struct { int hp; int ammo; } PlayerV2;

typedef struct {
    size_t old_offset;
    size_t new_offset;
    size_t size;
} CopyOp;

/* For this test, src and dst are separate, non-overlapping blocks. */
static int copy_field(void *dst, size_t dst_size,
                      const void *src, size_t src_size, CopyOp op)
{
    if (op.old_offset > src_size || op.size > src_size - op.old_offset ||
        op.new_offset > dst_size || op.size > dst_size - op.new_offset)
        return 0;

    memcpy((unsigned char *)dst + op.new_offset,
           (const unsigned char *)src + op.old_offset,
           op.size);
    return 1;
}

int main(void)
{
    const PlayerV1 old = { .hp = 37 };
    PlayerV2 next = { .hp = 0, .ammo = 12 };

    /* For now, write the copy plan by hand instead of diffing layouts. */
    const CopyOp copy_hp = {
        .old_offset = offsetof(PlayerV1, hp),
        .new_offset = offsetof(PlayerV2, hp),
        .size = sizeof old.hp,
    };

    if (!copy_field(&next, sizeof next, &old, sizeof old, copy_hp)) {
        fprintf(stderr, "FAIL: copy operation is out of bounds\n");
        return EXIT_FAILURE;
    }

    if (old.hp != 37 || next.hp != 37 || next.ammo != 12) {
        fprintf(stderr, "FAIL: old hp=%d; new hp=%d, ammo=%d\n",
                old.hp, next.hp, next.ammo);
        return EXIT_FAILURE;
    }

    printf("PASS: old hp=%d; new hp=%d, ammo=%d\n",
           old.hp, next.hp, next.ammo);
    return EXIT_SUCCESS;
}
```

`offsetof` tells you where `hp` starts in **each** layout. Casting to a character pointer makes `+ offset` advance by bytes. `sizeof old.hp` copies just the `int`, not the entire old object. The subtraction-based bounds checks keep the operation inside each block without risking overflow from `offset + size`. The copy never touches `ammo`: that field has no source, so it keeps the new layout's default.

### What the four bounds checks mean

On each side, ask two questions: **Is the starting offset valid? Does the whole copy fit from there?**

| Condition | What it rejects |
| --- | --- |
| `op.old_offset > src_size` | **Source starts out of bounds:** the read would start past the end of the old buffer. |
| `op.size > src_size - op.old_offset` | **Source read is too large:** there aren't enough bytes left to read. |
| `op.new_offset > dst_size` | **Destination starts out of bounds:** the write would start past the end of the new buffer. |
| `op.size > dst_size - op.new_offset` | **Destination write is too large:** there isn't enough room left to write. |

Picture an **8-byte source buffer**. Start at offset **6** and try to copy **4 bytes**:

```text
Byte offset:    0   1   2   3   4   5   6   7 |  8   9
Source buffer: [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] | outside
Requested:                            [6] [7] | [8] [9]
                                       OK  OK |  NO  NO
```

The first check lets offset `6` through: it isn't past the end. The second check rejects the read:

```text
op.size > src_size - op.old_offset
   4    >     8    -       6
   4    >     2                      -> reject
```

**`8 - 6` counts how many bytes are left starting at offset 6.** Only bytes 6 and 7 remain, but the copy asks for four. The destination checks work the same way, except the question is how many bytes you can **write**.

**The order matters.** C evaluates `||` left to right and stops at the first true condition. If the starting offset is past the end, the subtraction on that side never runs. Otherwise, subtracting a larger offset from a smaller `size_t` would wrap around and make it look like you have a lot of space left. An offset exactly equal to the buffer size leaves zero bytes, so only a zero-byte copy passes at that position.

## 2. Compile and run it

Save `migration_test.c`. In an **x64 Native Tools Command Prompt**, change to `scalar-migration-lab` and run (MSVC's `/std:c11` mode accepts this C99 example):

```cmd
cl /nologo /std:c11 /W4 migration_test.c
migration_test.exe
```

Or with a C99 compiler:

```sh
cc -std=c99 -Wall -Wextra -pedantic migration_test.c -o migration_test
./migration_test
```

Expected output:

```text
PASS: old hp=37; new hp=37, ammo=12
```

## 3. Make sure the test can catch the wrong approach

Change **only** the declaration of `PlayerV2` to put `ammo` first:

```c
typedef struct { int ammo; int hp; } PlayerV2;
```

Run the same test. It should still pass: your `offsetof(PlayerV2, hp)` is now different, and the copy follows it. By contrast, `memcpy(&next, &old, sizeof old)` would write `37` into `ammo` and leave `hp` at `0`. Before the reorder, that wrong approach might appear to work by coincidence.

The next small step is to replace the hand-written `CopyOp` with one calculated from **two field descriptors**. Give `hp` the same stable ID in both versions; give `ammo` a new ID. Match the `hp` entries to create one copy operation, and leave `ammo` alone. Keep the same test and expected output while you change how the copy plan is produced.

Next: [02 · Put the engine's memory index at byte zero](02-memory-index-tutorial.md).
