# Build the TOML-to-C generator in C

Your generator is a separate **C executable**. It reads one TOML schema and writes ordinary source files. It doesn't need to know MSVC's padding rules; it emits `offsetof`, `sizeof`, and `_Alignof` expressions that MSVC evaluates when compiling the engine DLL.

```text
cl builds generate_memory.exe once
    generate_memory.exe reads TOML and writes C
        main.c calls cl /LD on engine.c and the generated code
```

Use [`tomlc17`](https://github.com/cktan/tomlc17): a small MIT-licensed C parser distributed as **one `.h` and one `.c`**, not header-only. Vendor `src/tomlc17.h`, `src/tomlc17.c`, and its license from the same release. The generator runs in its own process; the library's allocations do not change the host's `VirtualAlloc` ownership model.

## 1. Read one schema and emit `memory.h`

In your current `schemas/world-v1.toml`, the last line is `type = i32`. TOML values that are strings need quotes: use `type = "i32"`. A bare key such as `type` is legal; a bare string *value* is not. Add `abi_version = 1` at the top level before any `[[regions]]` tables when you start emitting the API header.

Read `[[regions]]` as an array of tables. Each region table contains a `fields` array; each element of that array is another table. `tomlc17` gives you this tree directly. For example, `toml_get(field, "type")` returns a tagged value: check for `TOML_STRING` before using `u.s`. Keep the parsed tree alive while using those strings.

Here is a first runnable `tools/generate_memory.c` for your own fixtures. It only emits `memory.h`; the next sections build on the same traversal. The sample caps and validates IDs and rejects malformed identifiers and unknown types. Before accepting arbitrary schemas, also reject C keywords, reserved identifiers, and names that collide with generated symbols.

```c
#include "tomlc17.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message)
{
    fprintf(stderr, "schema: %s\n", message);
    exit(EXIT_FAILURE);
}

static toml_datum_t require(toml_datum_t table, const char *key, toml_type_t type)
{
    if (table.type != TOML_TABLE)
        fail("expected a table");
    toml_datum_t value = toml_get(table, key);
    if (value.type != type) {
        fprintf(stderr, "schema: missing or wrong type for '%s'\n", key);
        exit(EXIT_FAILURE);
    }
    return value;
}

static int letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static const char *name_of(toml_datum_t table)
{
    const char *name = require(table, "name", TOML_STRING).u.s;
    if (!letter((unsigned char)name[0]))
        fail("names must start with an ASCII letter");
    for (const unsigned char *p = (const unsigned char *)name + 1; *p; ++p) {
        if (!letter(*p) && *p != '_' && !(*p >= '0' && *p <= '9'))
            fail("invalid name character");
    }
    return name;
}

static uint32_t id_of(toml_datum_t table)
{
    int64_t id = require(table, "id", TOML_INT64).u.int64;
    if (id <= 0 || id > UINT32_MAX)
        fail("id must be a positive uint32_t");
    return (uint32_t)id;
}

typedef struct {
    const char *c_type;
    const char *kind_token;
} TypeInfo;

static TypeInfo type_of(toml_datum_t field)
{
    const char *type = require(field, "type", TOML_STRING).u.s;
    if (strcmp(type, "i32") == 0) return (TypeInfo){"int32_t", "STORAGE_I32"};
    if (strcmp(type, "u32") == 0) return (TypeInfo){"uint32_t", "STORAGE_U32"};
    if (strcmp(type, "f32") == 0) return (TypeInfo){"float", "STORAGE_F32"};
    fail("unknown field type");
    return (TypeInfo){0};
}

static void validate_list(toml_datum_t list)
{
    if (list.u.arr.size < 1 || list.u.arr.size > 256)
        fail("expected between 1 and 256 entries");
    for (int i = 0; i < list.u.arr.size; ++i) {
        toml_datum_t entry = list.u.arr.elem[i];
        uint32_t id = id_of(entry);
        const char *name = name_of(entry);
        for (int j = 0; j < i; ++j) {
            toml_datum_t prev = list.u.arr.elem[j];
            if (id == id_of(prev) || strcmp(name, name_of(prev)) == 0)
                fail("duplicate name or ID in the same list");
        }
    }
}

static void emit_memory(FILE *out, toml_datum_t regions)
{
    fputs("#ifndef MEMORY_H\n#define MEMORY_H\n#include <stdint.h>\n\n", out);
    for (int i = 0; i < regions.u.arr.size; ++i) {
        toml_datum_t region = regions.u.arr.elem[i];
        const char *name = name_of(region);
        toml_datum_t fields = require(region, "fields", TOML_ARRAY);
        fprintf(out, "typedef struct {\n");
        for (int j = 0; j < fields.u.arr.size; ++j) {
            toml_datum_t field = fields.u.arr.elem[j];
            fprintf(out, "    %s %s;\n", type_of(field).c_type, name_of(field));
        }
        fprintf(out, "} %s;\n\n", name);
    }
    fputs("typedef struct {\n", out);
    for (int i = 0; i < regions.u.arr.size; ++i) {
        const char *name = name_of(regions.u.arr.elem[i]);
        fprintf(out, "    %s region_%s;\n", name, name);
    }
    fputs("} EngineMemory;\n\n#endif\n", out);
}

int main(int argc, char **argv)
{
    if (argc != 3)
        fail("usage: generate_memory SCHEMA.toml OUTPUT_MEMORY.h");

    toml_result_t doc = toml_parse_file_ex(argv[1]);
    if (!doc.ok)
        fail(doc.errmsg);

    toml_datum_t regions = require(doc.toptab, "regions", TOML_ARRAY);
    validate_list(regions);
    for (int i = 0; i < regions.u.arr.size; ++i) {
        toml_datum_t fields = require(regions.u.arr.elem[i], "fields", TOML_ARRAY);
        validate_list(fields);
        for (int j = 0; j < fields.u.arr.size; ++j)
            (void)type_of(fields.u.arr.elem[j]);
    }

    FILE *out;
    if (fopen_s(&out, argv[2], "w") != 0)
        fail("could not open output file");
    emit_memory(out, regions);
    int write_failed = ferror(out);
    if (fclose(out) != 0 || write_failed)
        fail("could not write output file");
    toml_free(doc);
    return EXIT_SUCCESS;
}
```

From the project folder in an x64 Native Tools prompt, assuming the vendor files are in `vendor\tomlc17` and the output directories exist:

```cmd
cl /nologo /TC /std:c17 /Ivendor\tomlc17 tools\generate_memory.c vendor\tomlc17\tomlc17.c /Febuild\generate_memory.exe
build\generate_memory.exe schemas\world-v1.toml build\v1\memory.h
```

Open `memory.h` before proceeding. Check that the members appear in TOML order, and the aggregate contains a member like `World region_World;`. On a malformed file or unknown type, the program should fail before opening the output file. This first step doesn't consume `default` values yet.

## 2. Emit the layout tables with the same loops

Start from the working `memory_layout.inc` in the [DLL tutorial](dll-migration-tutorial.md#3-return-a-layout-from-inside-the-dll). The only schema-dependent parts there are the table entries. Move the common includes and macros into a prefix template, and move the common DLL export bodies and `#undef` lines into a suffix template. **Between them**, call this C emitter:

```c
static void emit_tables(FILE *out, toml_datum_t regions)
{
    for (int i = 0; i < regions.u.arr.size; ++i) {
        toml_datum_t region = regions.u.arr.elem[i];
        const char *name = name_of(region);
        toml_datum_t fields = require(region, "fields", TOML_ARRAY);
        fprintf(out, "static const FieldLayout fields_%s[] = {\n", name);
        for (int j = 0; j < fields.u.arr.size; ++j) {
            toml_datum_t field = fields.u.arr.elem[j];
            TypeInfo type = type_of(field);
            fprintf(out, "    FIELD(%" PRIu32 ", %s, %s, %s, %s),\n",
                    id_of(field), type.kind_token, name, name_of(field), type.c_type);
        }
        fputs("};\n", out);
    }
    fputs("static const RegionLayout regions[] = {\n", out);
    for (int i = 0; i < regions.u.arr.size; ++i) {
        toml_datum_t region = regions.u.arr.elem[i];
        const char *name = name_of(region);
        fprintf(out, "    REGION(%" PRIu32 ", region_%s, %s, fields_%s),\n",
                id_of(region), name, name, name);
    }
    fputs("};\n", out);
}
```

The `region_World` spelling must match the member emitted by `emit_memory`. For example, field `hp` produces `FIELD(1, STORAGE_I32, World, hp, int32_t)`. When MSVC compiles the DLL, it turns that into an entry with the real `offsetof(World, hp)`. The host reads that entry through `engine_describe_layout`; there is no parser or code generator involved in the host's diff.

You can append a template to the generated output with a bounded copy loop. Put this helper in the generator, using the `fail` above:

```c
static void emit_template(FILE *out, const char *path)
{
    FILE *in;
    if (fopen_s(&in, path, "rb") != 0)
        fail("could not open template");
    char bytes[4096];
    size_t n;
    while ((n = fread(bytes, 1, sizeof bytes, in)) != 0) {
        if (fwrite(bytes, 1, n, out) != n)
            fail("could not write generated file");
    }
    if (ferror(in))
        fail("could not read template");
    fclose(in);
}
```

Open `memory_layout.inc` in `wb` mode. Emit the prefix, `emit_tables(out, regions)`, then the suffix; check the write and close as in the starter. End each template with a newline. For the first fixture, the DLL can still set its defaults in ordinary `engine_init`; add generated, type-checked default assignments as a separate step.

## 3. Generate `engine_api.h` and wire up the host last

Use `require(doc.toptab, "abi_version", TOML_INT64).u.int64` to select the ABI template. If it's `1`, use `emit_template(out, "tools/templates/engine_api_v1.h.in")`, where the template contains the ABI declarations shown in the DLL tutorial. Reject versions you haven't implemented. Different schemas at ABI 1 produce the same API header; **schema changes affect the tables, not the format the host uses to read them**.

Once the three emitters work, change the second argument from an output filename to an output **directory**. Construct `memory.h`, `memory_layout.inc`, and `engine_api.h` paths there; with `snprintf`, check that the result fits before opening a file. Have the caller create the directory. Generate one API header before building the host, then use the same generator at runtime for each DLL version:

```c
if (system("build\\generate_memory.exe schemas\\world-v1.toml build\\v1") != 0)
    return EXIT_FAILURE;
/* Now call cl /LD for v1. Repeat generation and compilation for v2. */
```

This last command uses the **extended directory-output version**, not the starter in section 1. Keep the build command fixed while prototyping and stop if the generator fails. You should be able to edit a schema, run the host, and see the new `memory.h` and layout tables without having to rebuild the host, as long as the ABI version still matches.
