#include "pch.h"
#include "memory.h"

typedef struct { int hp; } PlayerV1;
typedef struct { int hp; int ammo; } PlayerV2;



TEST(ScalarMigration, CopierHpAndKeepsAmmoDefault) {
	const PlayerV1 old = { 37 };
	PlayerV2 next = { 0 };
	next.ammo = 12;
	const CopyOp copy_hp = {
		offsetof(PlayerV1, hp), offsetof(PlayerV2,hp), sizeof old.hp
	};
}

TEST(MemoryInit, SetsCurors) {
	const size_t capacity = 256;
	MemoryBlock block = { (unsigned char*)malloc(capacity),capacity };
	ASSERT_TRUE( block.base != NULL);

	ASSERT_TRUE(memory_init(block));
}