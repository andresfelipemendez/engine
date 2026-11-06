#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct {
    unsigned char *base;
    size_t used;
    size_t commited;
    size_t capacity;
} Arena;

Arena arena_reseverve(size_t capacity);
void* arena_alloc(Arena *arena, size_t size);

#endif