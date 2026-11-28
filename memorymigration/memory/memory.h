#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
	unsigned char* base;
	size_t capacity;
} MemoryBlock;

typedef struct {
	size_t offset;
	size_t length;
} StringRef;

typedef struct {
	size_t src_offset;
	size_t dest_offset;
	size_t size;
} CopyOp;

bool  memory_init(MemoryBlock block);
bool copy_field(const MemoryBlock* src, const MemoryBlock* dest, CopyOp op);

#ifdef __cplusplus
}
#endif

#endif // !MEMORY_H
