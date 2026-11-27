#include "memory.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* message) {
	fprintf(stderr, "schema: %s\n", message);
	exit(EXIT_FAILURE);
}

static toml_datum_t require(toml_datum_t table, const char* key, toml_type_t type)
{
	if (table.type != TOML_TABLE)
		fail("expected a table");
}

int main(int argc, char** argv) {
	if (argc != 3)
		fail("");
}