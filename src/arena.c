#include "arena.h"

Arena reserve(size_t capacity) {
    Arena a;
    a.base = platform_reserve(capacity);
    a.capacity = capacity;
}

void* arena_alloc(Arena *arena, size_t size) {

}