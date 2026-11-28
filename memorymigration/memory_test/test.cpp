#include "pch.h"
#include "memory.h"

static void CheckCursors(MemoryBlock block) 
{
	ASSERT_TRUE(memory_init(block));
	const Memory* index = (const Memory*)block.base;
	EXPECT_EQ(sizeof(Memory), index->front);
	EXPECT_EQ(block.capacity, index->back);
}

TEST(MemoryInit, SetsCurors) 
{
	const size_t capacity = 256;
	MemoryBlock block = {(unsigned char*)malloc(capacity), capacity};
	ASSERT_TRUE(block.base != NULL);
	CheckCursors(block);
	free(block.base);
}

TEST(MemoryInit, RejectsUndesizeBlock)
{
	Memory storage = { 0 };
	MemoryBlock block = { (unsigned char*)&storage,sizeof(storage) - 1 };
	EXPECT_FALSE(memory_init(block));
}

TEST(MemoryInit, RejectsNullBlock)
{
	MemoryBlock block = { NULL, 256 };
	EXPECT_FALSE(memory_init(block));
}

typedef struct { int hp; } PlayerV1;
typedef struct {
	int ammo; 
	int hp;
} PlayerV2;

TEST(ScalarMigration, CopierHpAndKeepsAmmoDefault) {
	const PlayerV1 prev = { 37 };
	PlayerV2 next = { 0 };
	next.ammo = 12;
	const CopyOp copy_hp = {
		offsetof(PlayerV1, hp), offsetof(PlayerV2,hp), sizeof(prev.hp)
	};

	const MemoryBlock src = { (unsigned char*)&prev,sizeof(prev) };
	const MemoryBlock dest = { (unsigned char*)&next,sizeof(next) };

	ASSERT_TRUE(copy_field(&src, &dest, copy_hp));
	EXPECT_EQ(37,prev.hp);
	EXPECT_EQ(37, next.hp);
	EXPECT_EQ(12, next.ammo);
}


TEST(ScalarMigration, RejectsCopyPastDestination) {
	const PlayerV1 prev = { 37 };
	PlayerV2 next[2] = {0};
	next[0].ammo = 12;
	next[1].hp = -1;

	const MemoryBlock src = { (unsigned char*)&prev,sizeof(prev) };
	const MemoryBlock dest = { (unsigned char*)&next,sizeof(next[0])};

	const CopyOp copy_hp = {
		offsetof(PlayerV1, hp), dest.capacity, sizeof(prev.hp)
	};

	ASSERT_FALSE(copy_field(&src, &dest, copy_hp));
	EXPECT_EQ(0, next[0].hp);
	EXPECT_EQ(12, next[0].ammo);
	EXPECT_EQ(-1, next[1].hp);
}
