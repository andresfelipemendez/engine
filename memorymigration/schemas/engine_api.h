#ifndef ENGINE_API_H
#define ENGINE_API_H

#include <stdint.h>

typedef struct {
	uint32_t id;
	uint32_t kind;
	uint64_t offset;
	uint64_t size;
	uint64_t alignment;
} FieldLayout;

typedef struct {
	uint32_t id;
	uint32_t field_count;
	uint64_t offset;
	uint64_t size;
	uint64_t alignment;
	const FieldLayout* fields;
} RegionLayout;

typedef struct {
	uint64_t block_size;
	uint64_t block_alignment;
	uint64_t region_count;
	const RegionLayout* regions;
} EngineLayout;

typedef uint32_t(__cdecl* EngineApiVersionFn)(void);
typedef int(__cdecl* EngineDescribeLayoutFn)(const void* block, EngineLayout* out);

#endif