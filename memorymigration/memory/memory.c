#include "memory.h"

bool memory_init(MemoryBlock block) {
	if (!block.base || block.capacity < sizeof(Memory))
		return false;

	Memory* index = (Memory*)block.base;
	*index = (Memory){ sizeof(Memory), block.capacity };
	return true;
}

bool copy_field(const MemoryBlock* src, const MemoryBlock* dest, CopyOp op) {
	if (op.old_offset > src->capacity || op.size > src->capacity - op.old_offset ||
		op.new_offset > dest->capacity || op.size > dest->capacity - op.new_offset)
		return false;

	memcpy(dest->base + op.new_offset, src->base + op.old_offset, op.size);
	return true;
}