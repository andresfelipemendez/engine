#include "memory.h"

#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int value;
} Marker;

typedef struct {
  int hp;
} PlayerV1;
typedef struct {
  int hp;
  int ammo;
} PlayerV2;

typedef struct {
  size_t old_offset;
  size_t new_offset;
  size_t size;
} CopyOp;

static int copy_field(void *dst, size_t dst_size, const void *src,
                      size_t src_size, CopyOp op) {
  int sourceStartIsOutOfBounds = op.old_offset > src_size;
  int sourceReadIsTooLarge = op.size > src_size - op.old_offset;
  int destinationStartIsOutOfBounds = op.new_offset > dst_size;
  int destinationWriteIsTooLarge = op.size > dst_size;
  if (sourceStartIsOutOfBounds || sourceReadIsTooLarge ||
      destinationStartIsOutOfBounds || destinationWriteIsTooLarge) {
    return 0;
  }

  memcpy((unsigned char *)dst + op.new_offset,
         (const unsigned char *)src + op.old_offset, op.size);
  return 1;
}

static int init_block(unsigned char *base, size_t capacity) {
  if (capacity < sizeof(Memory))
    return 0;

  Memory *index = (Memory *)base;
  *index = (Memory){0};
  size_t cursor = sizeof(*index);
  const size_t stride = sizeof(Marker);
  const size_t padding = (stride - cursor % stride) % stride;

  if (padding > capacity - cursor || 3 > (capacity - cursor - padding) / stride)
    return 0;
  cursor += padding;

  for (size_t i = 0; i < 3; ++i) {
    const RegionType type = (RegionType)i;
    index->regions[i] = (Region){cursor, stride};
    *(Marker *)(base + cursor) = (Marker){10 * (1 + (int)type)};
    cursor += stride;
  }
  index->count = 3;
  index->front = cursor;
  index->back = capacity;
  return 1;
}

static int test_memory_index(void) {
  const size_t capacity = sizeof(Memory); // +4 * sizeof(Marker);
  size_t game_offsets[2];

  unsigned char *block = malloc(capacity);
  if (!block || !init_block(block, capacity)) {
    fputs("FAIL: could not initialize block\n", stderr);
    free(block);
    return EXIT_FAILURE;
  }

  const Memory *index = (const Memory *)block;
  const Region *game = &index->regions[REGION_GAME];

  const bool gameStartsInsideIndex = game->offset < sizeof(Memory);
  const bool gameStartsPastBlock = game->offset > capacity;
  const bool gameOffsetNotOnMarkerStride = game->offset % sizeof(Marker) != 0;
  const bool gameSizeIsNotOneMarker = game->size != sizeof(Marker);
  const bool gameExtendsPastBlock =
      !gameStartsPastBlock && game->size > capacity - game->offset;
  if (gameStartsPastBlock || gameStartsPastBlock ||
      gameOffsetNotOnMarkerStride || gameSizeIsNotOneMarker ||
      gameExtendsPastBlock) {
    fputs("FAIL: invalid Game entry\n", stderr);
    free(block);
    return EXIT_FAILURE;
  }

  const Marker *marker = (const Marker *)(block + game->offset);
  printf("Game offset=%zu marker=%d\n", game->offset, marker->value);
  if (marker->value != 20) {
    fputs("Fail: got the wrong marker\n", stderr);
    free(block);
    return EXIT_FAILURE;
  }

  free(block);
  puts("PASS: found Game in its fixed slot");
  return EXIT_SUCCESS;
}

int main() {
  const PlayerV1 old = {.hp = 37};
  PlayerV2 next = {.hp = 0, .ammo = 12};

  const CopyOp copy_hp = {
      .old_offset = offsetof(PlayerV1, hp),
      .new_offset = offsetof(PlayerV2, hp),
      .size = sizeof(old.hp),
  };

  if (!copy_field(&next, sizeof(next), &old, sizeof(old), copy_hp)) {
    fprintf(stderr, "FAIL: copy operaion is out of bounds\n");
    return EXIT_FAILURE;
  }

  if (old.hp != 37 || next.hp != 37 || next.ammo != 12) {
    fprintf(stderr, "FAIL: old hp=%d; new hp=%d, ammo=%d\n", old.hp, next.hp,
            next.ammo);
  }

  test_memory_index();

  return;
}
