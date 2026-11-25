#ifndef MIGRATION_API_H
#define MIGRATION_API_H

#include <stddef.h>

typedef struct {
	size_t old_size, old_align;
	size_t new_size, new_align;
};

#endif
