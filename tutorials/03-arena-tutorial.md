# 03 · A memory arena for the header parser

You'll allocate two aligned slices from one block and verify that a failed allocation leaves the cursor unchanged. An arena is a **bump allocator**: each allocation advances a cursor. It doesn't free individual objects.

## Prerequisites

Finish [02 · Memory index](02-memory-index-tutorial.md) and create `arena-lab` outside the repository. Run the first three tasks there; you don't need a loader or a Visual Studio project.

## Tasks

### Task 1: Declare the arena in `arena.h`

Create `arena-lab/arena.h` with the following contents. It lets the test driver and allocator agree on the same `Arena` type and function declarations.

```c
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct {
    unsigned char *base;
    size_t capacity;
    size_t used;
} Arena;

/* memory must point to a writable block of at least capacity bytes. */
void arena_init(Arena *arena, void *memory, size_t capacity);

/* Returns NULL on failure. Alignment must be a nonzero power of two. */
void *arena_alloc(Arena *arena, size_t size, size_t alignment);

#endif
```

`base` and `capacity` describe the whole block. `used` is the offset of the next available byte, including any padding used by previous allocations. Pass an `Arena *`, not an `Arena` by value, so callers update the same cursor.

### Task 2: Implement the cursor in `arena.c`

Create `arena-lab/arena.c` beside `arena.h` and put the following code in it. This file implements the functions the driver will call.

```c
#include "arena.h"
#include <stdint.h>

void arena_init(Arena *arena, void *memory, size_t capacity)
{
    arena->base = memory;
    arena->capacity = capacity;
    arena->used = 0;
}

void *arena_alloc(Arena *arena, size_t size, size_t alignment)
{
    if (size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0)
        return NULL;

    size_t remaining = arena->capacity - arena->used;
    unsigned char *cursor = arena->base + arena->used;
    size_t misalignment = (uintptr_t)cursor % alignment;
    size_t padding = misalignment ? alignment - misalignment : 0;

    if (padding > remaining || size > remaining - padding)
        return NULL;

    arena->used += padding + size;
    return cursor + padding;
}
```

Say the base is 8-byte aligned and the file buffer uses 101 bytes. An 8-byte-aligned allocation starts at offset 104, not 101. Compute padding from the **address** rather than just the offset so this also works with a base address that isn't already aligned for the requested type. Check capacity before moving the cursor: a failed allocation leaves `used` unchanged.

### Task 3: Call the allocator from `arena_demo.c`

Create `arena-lab/arena_demo.c` with the following code. The driver owns the block; `Arena` just tracks space inside it.

```c
#include "arena.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    unsigned char *block = malloc(32);
    if (!block) {
        fputs("FAIL: malloc\n", stderr);
        return EXIT_FAILURE;
    }

    Arena arena;
    arena_init(&arena, block, 32);
    unsigned char *text = arena_alloc(&arena, 3, 1);
    unsigned char *slot = arena_alloc(&arena, 8, 8);
    size_t mark = arena.used;
    void *too_big = arena_alloc(&arena, 33, 1);

    int ok = text == block && slot && slot >= text + 3 &&
             (uintptr_t)slot % 8 == 0 && !too_big && arena.used == mark;
    free(block);
    if (!ok) {
        fputs("FAIL: placement or cursor check\n", stderr);
        return EXIT_FAILURE;
    }
    puts("PASS: aligned, non-overlapping slices; oversized request rejected");
    return EXIT_SUCCESS;
}
```

Save all three files. In an **x64 Native Tools Command Prompt**, change to `arena-lab` and run:

```cmd
cl /nologo /W4 /TC /std:c11 arena.c arena_demo.c /Fe:arena_demo.exe
arena_demo.exe
```

The example uses C99; for GCC use `gcc -std=c99 -Wall -Wextra -pedantic arena.c arena_demo.c -o arena_demo.exe` and run `./arena_demo.exe` from the same folder. Both builds should print:

```text
PASS: aligned, non-overlapping slices; oversized request rejected
```

If you get unresolved externals for `arena_init` or `arena_alloc`, check that `arena.c` is in the build command. If the placement check fails, print the addresses and `arena.used` before and after each call; look for missing padding or an early cursor update. For a deliberate failure, change the request for `slot` to use alignment `3`: the allocator should reject it and the driver should print `FAIL`. Restore `8` afterward.

## Optional integration: use Windows pages in your loader

You have a working allocator without any OS-specific code in `arena.c`. If you already have a Windows header-loader experiment with a `parse` entry point, add `<windows.h>` at the top of that experiment's `main.c` and use this sequence **inside its `main`** in place of the current backing-block allocation. This sketch assumes `parse` already exists; it is not a replacement for `arena_demo.c`.

```c
size_t capacity = 16u * 1024u * 1024u;  /* Example: 16 MiB. */
void *memory = VirtualAlloc(NULL, capacity,
                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
if (memory == NULL)
    return 1;

Arena arena;
arena_init(&arena, memory, capacity);
parse(&arena, L"testheader.h");
VirtualFree(memory, 0, MEM_RELEASE);
```

This version commits the whole block up front. It is intentionally fixed-capacity: if you run out of room, report the error instead of silently changing addresses. A version that reserves a larger range and commits pages on demand can come later. When you implement the sketch, add `arena.c` to the Visual Studio project so it is compiled and linked.

### Pass the arena to the loader

Pass the same `Arena *` from `parse` to `load_source`. Keep `_wopen` in binary mode, `_fstat64`, the size check before adding one byte, and the `_read` loop. Replace only the source buffer allocation:

```c
size_t mark = arena->used;
char *data = arena_alloc(arena, length + 1, 1);
```

Check `data` before reading. If the read fails after allocation, close the descriptor and restore `arena->used = mark`, **provided you're discarding everything allocated after that mark and no pointers to those allocations escaped**. On success, record the file's byte count in `SourceBuffer.length`, put `\0` at `data[length]`, and close the descriptor. The parser uses the buffer and length; it doesn't reopen the file.

For a typed object, request that type's alignment: use `_Alignof(Token)` in C11 or `__alignof(Token)` in MSVC. In a strictly C99 scratch fixture with one ordinarily aligned type and a `malloc` base, you can instead use the fixed-stride placement from [02 · Memory index](02-memory-index-tutorial.md); keep type alignment decisions out of portable arena code. For arrays, check that `count * sizeof(Token)` won't overflow before allocating.

**Ownership rule:** Never call `free` or `VirtualFree` on an individual arena slice. The source buffer must outlive every token or node that refers to it. Release the original block in `main` only after those references are no longer needed.

If you integrate the loader, run it on `testheader.h` and check that the reported length excludes the terminator. Otherwise, stop after the standalone test.

## Next lesson

Continue with [04 · Write your first lexer](04-lexer-tutorial.md).
