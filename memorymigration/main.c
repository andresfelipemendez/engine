#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include "memorymigration.h"

typedef struct { int hp; } PlayerV1;
typedef struct { int hp; int ammo; } PlayerV2;

typedef struct {
	size_t old_offset;
	size_t new_offset;
	size_t size;
} CopyOp;

static int copy_field(void* dst, size_t dst_size,
	const void* src, size_t src_size,
	CopyOp op)
{
	int sourceStartIsOutOfBounds = op.old_offset > src_size;
	int sourceReadIsTooLarge = op.size > src_size - op.old_offset;
	int destinationStartIsOutOfBounds = op.new_offset > dst_size;
	int destinationWriteIsTooLarge = op.size > dst_size;
	if (sourceStartIsOutOfBounds ||
		sourceReadIsTooLarge ||
		destinationStartIsOutOfBounds ||
		destinationWriteIsTooLarge) {
		return 0;
	}

	memcpy((unsigned char*)dst + op.new_offset,
		(const unsigned char*)src + op.old_offset,
		op.size);
	return 1;
}

int main() {
	const PlayerV1 old = { .hp = 37 };
	PlayerV2 next = { .hp = 0,.ammo = 12 };

	const CopyOp copy_hp = {
		.old_offset = offsetof(PlayerV1, hp),
		.new_offset = offsetof(PlayerV2, hp),
		.size = sizeof(old.hp),
	};

	if (!copy_field(&next, sizeof(next), &old, sizeof(old), copy_hp)) {
		fprintf(stderr, "FAIL: copy operaion is out of bounds\n");
		return EXIT_FAILURE;
	}

	if(old.hp != 37 || next.hp != 37 || next.ammo != 12) {
		fprintf(stderr, "FAIL: old hp=%d; new hp=%d, ammo=%d\n",
			old.hp, next.hp, next.ammo);
	}

	return EXIT_SUCCESS;
}