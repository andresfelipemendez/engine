# A memory arena for the header parser

The goal is to allocate one block in `main` and use it for source text, tokens, and eventually AST nodes. An arena is a **bump allocator**: each allocation advances a cursor. It doesn't free individual objects. These are example snippets to type and adapt, not changes to the project's C files.

## 1. Sketch `arena.h`

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

## 2. Sketch `arena.c`

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

## 3. Let `main` own the Windows allocation

With `<windows.h>` available in `main.c`, the lifetime looks like this:

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

## 4. Point the loader at the arena

Pass the same `Arena *` from `parse` to `load_source`. Keep `_wopen` in binary mode, `_fstat64`, the size check before adding one byte, and the `_read` loop. Replace only the source buffer allocation:

```c
size_t mark = arena->used;
char *data = arena_alloc(arena, length + 1, 1);
```

Check `data` before reading. If the read fails after allocation, close the descriptor and restore `arena->used = mark`, **provided you're discarding everything allocated after that mark and no pointers to those allocations escaped**. On success, record the file's byte count in `SourceBuffer.length`, put `\0` at `data[length]`, and close the descriptor. The parser uses the buffer and length; it doesn't reopen the file.

For a typed object, request that type's alignment, for example `arena_alloc(arena, sizeof(Token), _Alignof(Token))` in C11. For arrays, check that `count * sizeof(Token)` won't overflow before allocating.

**Ownership rule:** Never call `free` or `VirtualFree` on an individual arena slice. The source buffer must outlive every token or node that refers to it. Release the original block in `main` only after those references are no longer needed.

### Try it before using a real header

Use a small block and test that consecutive allocations don't overlap, that a typed allocation after an odd-sized text allocation is aligned, and that an oversized request returns `NULL` without moving `used`. Then load `testheader.h` and check that the reported length excludes the terminator.

Next: [Build a small C header lexer](lexer-tutorial.md).
