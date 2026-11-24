#ifndef MEMORY_MIGRATION_H
#define MEMORY_MIGRATION_H
#include <stddef.h>
#include <stdbool.h>
typedef struct {
	char* data;
	size_t length;
} SourceBuffer;

void parse(const wchar_t *path);
bool load_source(const wchar_t* path, SourceBuffer* out);
#endif // !MEMORY_MIGRATION_H
