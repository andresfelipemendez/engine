#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
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
  size_t count;
  size_t front;
  size_t back;
  Region regions[3];
} Memory;

typedef struct {
  size_t offset;
  size_t length;
} StringRef;

#endif // !MEMORY_H
