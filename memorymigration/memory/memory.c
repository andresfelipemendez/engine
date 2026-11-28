#include "memory.h"

bool memory_init(MemoryBlock block) {
	if (!block.base || block.capacity < sizeof(Memory))
		return false;

	Memory* index = (Memory*)block.base;
	*index = (Memory){
		.front = sizeof(Memory), 
		.back = block.capacity
	};
	return true;
}

bool copy_field(const MemoryBlock* src, const MemoryBlock* dest, CopyOp op) {
	if (op.src_offset > src->capacity || op.size > src->capacity - op.src_offset ||
		op.dest_offset > dest->capacity || op.size > dest->capacity - op.dest_offset)
		return false;

	memcpy(dest->base + op.dest_offset, src->base + op.src_offset, op.size);
	return true;
}