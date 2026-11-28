# 05 · Build a tiny memory-schema generator in C

Instead of TOML or parsing full C headers, give the generator one job: read a small declaration language and emit C. Start with `Player`, then feed its generated layout tables into the [DLL migration tutorial](06-dll-migration-tutorial.md).

```text
Player.mem → lexer → parser → structs and fields → generated C → cl /LD
```

## Prerequisites

Finish [04 · Lexer](04-lexer-tutorial.md). Create `codegen-lab` outside the repository and copy your working `lexer.h` and `lexer.c` into it. Build this scratch version independently of any legacy generator project.

## 1. Create two input fixtures

In `codegen-lab`, create a `schemas` directory and save the following as `schemas/player-v1.mem`:

```text
struct Player {
    i32 hp
}
```

In the same directory, create `schemas/player-v2.mem`. Put `ammo` first so that `hp` actually moves:

```text
struct Player {
    i32 ammo
    i32 hp
}
```

The whole grammar is:

```text
file   = struct_definition+ EOF
struct_definition = "struct" name "{" field+ "}"
field  = type_name name
```

`+` means one or more. A field is exactly two words, so whitespace separates tokens, but newlines and semicolons aren't needed to end a field. `type_name` is a word the parser looks up in a small table. Start with `i32` and `u32`, which emit `int32_t` and `uint32_t` from `<stdint.h>`. Later, add `i8`, `u8`, `i16`, `u16`, `i64`, and `u64` by extending the table, not the grammar.

Leave defaults and numeric field IDs out of the grammar for now. The engine initializes its defaults. The host matches names within each struct, so for now renaming a field means removing the old field and adding a new one.

## 2. Teach the lexer just three new tokens

Start from the [lexer tutorial](04-lexer-tutorial.md). In the scratch `lexer.h`, add `TOK_KW_STRUCT`, `TOK_LBRACE`, and `TOK_RBRACE` to `TokenKind`. In the scratch `lexer.c`, add `if (length == 6 && memcmp(text, "struct", 6) == 0) return TOK_KW_STRUCT;` to `name_kind` before its fallback. In the `lexer_next` punctuation switch, add `case '{': return token(lex, TOK_LBRACE, start);` and `case '}': return token(lex, TOK_RBRACE, start);`. The lexer should return `i32`, `hp`, and `Player` as `TOK_IDENTIFIER`. It already knows digits are allowed after the first letter of an identifier.

When stepping through the parser test in the next section, feed the scanner `struct Player { i32 hp }` to isolate lexer problems. Ignore byte spans at first; successive calls to `lexer_next` should produce:

```text
KW_STRUCT   struct
IDENTIFIER  Player
LBRACE      {
IDENTIFIER  i32
IDENTIFIER  hp
RBRACE      }
EOF
```

Test `struct Player { i23 hp }` too. That is valid **tokenization**; `i23` is just a word. It's the **parser** that reports an unknown type.

## 3. Parse one struct in `generate_memory.c`

Create `codegen-lab/generate_memory.c` beside `lexer.c`. Start it with the includes, types, and helpers below, then add the shared parser entry point and Google Test checkpoint below.

The AST can be two nested arrays. Store name tokens as byte spans into the source buffer, and keep that buffer alive until you finish writing the output. A fixed limit is fine for a first tool; diagnose exceeding it rather than writing past the array.

```c
#include "lexer.h"
#include <string.h>

typedef enum { SCHEMA_I32, SCHEMA_U32 } SchemaType;

typedef struct {
    SchemaType type;
    Token name;
} FieldDecl;

typedef struct {
    Token name;
    size_t field_count;
    FieldDecl fields[32];
} StructDecl;

typedef struct {
    Lexer lex;
    Token current;
} Parser;

static int take(Parser *p, TokenKind kind, Token *out)
{
    /* Keep the unexpected token current so the caller can report its span. */
    if (p->current.kind != kind)
        return 0;
    if (out != NULL)
        *out = p->current;
    p->current = lexer_next(&p->lex);
    return 1;
}

static int spelling_is(const char *source, Token t, const char *word)
{
    size_t length = strlen(word);
    /* A token is a span, not a separately terminated string. */
    return t.length == length && memcmp(source + t.start, word, length) == 0;
}

static int parse_field(Parser *p, FieldDecl *out)
{
    Token type = p->current;
    if (type.kind != TOK_IDENTIFIER)
        return 0;

    if (spelling_is(p->lex.source, type, "i32"))
        out->type = SCHEMA_I32;
    else if (spelling_is(p->lex.source, type, "u32"))
        out->type = SCHEMA_U32;
    else
        return 0; /* p->current still points at the unknown word. */

    take(p, TOK_IDENTIFIER, NULL);
    return take(p, TOK_IDENTIFIER, &out->name);
}

static int parse_struct(Parser *p, StructDecl *out)
{
    out->field_count = 0;
    if (!take(p, TOK_KW_STRUCT, NULL) ||
        !take(p, TOK_IDENTIFIER, &out->name) ||
        !take(p, TOK_LBRACE, NULL))
        return 0;

    while (p->current.kind != TOK_RBRACE) {
        /* Stop at a missing closing brace or before indexing past fields[]. */
        if (p->current.kind == TOK_EOF || out->field_count == 32)
            return 0;
        if (!parse_field(p, &out->fields[out->field_count]))
            return 0;
        out->field_count++;
    }
    if (out->field_count == 0)
        return 0;
    return take(p, TOK_RBRACE, NULL);
}
```

In `generate_memory.c`, add `#include <stdio.h>` and `#include <stdlib.h>` beside the other includes. Append the following below `parse_struct` so the string fixture and the later file path use the same parser. The first `lexer_next` primes lookahead; checking EOF prevents accepting merely a valid prefix.

```c
static int parse_schema(const char *source, size_t length, StructDecl *out)
{
    Parser p = {0};
    p.lex = (Lexer){source, length, 0};
    p.current = lexer_next(&p.lex);
    return parse_struct(&p, out) && p.current.kind == TOK_EOF;
}

#ifdef CODEGEN_TEST
int schema_field_count(const char *source, size_t length)
{
    StructDecl decl;
    return parse_schema(source, length, &decl) ? (int)decl.field_count : -1;
}
#endif
```

Add `codegen-lab/lexer.c` and `codegen-lab/generate_memory.c` to `memory_tests` as **Existing Items**. For those two files use `/TC /std:c11` and no precompiled header. Set `CODEGEN_TEST` in the **file-level** preprocessor definitions for `generate_memory.c`. Append to `memorymigration/memory_test/test.cpp`:

```cpp
extern "C" int schema_field_count(const char *, size_t);

TEST(SchemaParser, ParsesStringAndRejectsUnknownKind)
{
    const char good[] = "struct Player { i32 ammo i32 hp }";
    const char bad[] = "struct Player { i23 ammo i32 hp }";
    EXPECT_EQ(2, schema_field_count(good, sizeof good - 1));
    EXPECT_EQ(-1, schema_field_count(bad, sizeof bad - 1));
}
```

Build `memory_tests` and run `SchemaParser` in Test Explorer. Expect one pass. If valid input is rejected at byte zero, inspect the first token: it should be `TOK_KW_STRUCT`, not `TOK_IDENTIFIER`. Keep this narrow parser until the one-struct file fixture works. Before accepting general schemas, add validation for duplicate names, reserved identifiers, generated-symbol collisions, and names beyond your chosen limit.

## 4. Emit `memory.h` one line at a time

For the v2 fixture, aim for this output (add an include guard in the generated file):

```c
#include <stdint.h>

typedef struct {
    int32_t ammo;
    int32_t hp;
} Player;

typedef struct {
    Player region_Player;
} EngineMemory;
```

In `codegen-lab/generate_memory.c`, insert `emit_struct` below `parse_struct` and above `main` (`<stdio.h>` is already included). Once you validate the name length, use `%.*s` with the token's length to print its spelling without allocating a NUL-terminated copy. The build tool below calls it once per struct and emits `EngineMemory` from the same struct list:

```c
/* Keep the spelling and the migration kind together so both emitters agree. */
static const struct {
    const char *c_type;
    const char *storage_kind;
} schema_types[] = {
    [SCHEMA_I32] = {"int32_t", "STORAGE_I32"},
    [SCHEMA_U32] = {"uint32_t", "STORAGE_U32"},
};

static void emit_struct(FILE *out, const char *source, const StructDecl *decl)
{
    fputs("typedef struct {\n", out);
    for (size_t i = 0; i < decl->field_count; ++i) {
        FieldDecl field = decl->fields[i];
        fprintf(out, "    %s %.*s;\n", schema_types[field.type].c_type,
                (int)field.name.length, source + field.name.start);
    }
    fprintf(out, "} %.*s;\n\n", (int)decl->name.length,
            source + decl->name.start);
}
```

Below `emit_struct`, add the wrapper that writes the entire header. Even for one parsed struct, pass `&decl, 1`; later pass the array of parsed structs and its count. The `region_` prefix must match the member name used by the metadata emitter in §5.

```c
static int emit_memory_header(FILE *out, const char *source,
                              const StructDecl *decls, size_t count)
{
    fputs("#ifndef GENERATED_MEMORY_H\n#define GENERATED_MEMORY_H\n\n"
          "#include <stdint.h>\n\n", out);
    for (size_t i = 0; i < count; ++i)
        emit_struct(out, source, &decls[i]);

    fputs("typedef struct {\n", out);
    for (size_t i = 0; i < count; ++i) {
        const Token name = decls[i].name;
        const char *text = source + name.start;
        const int length = (int)name.length;
        fprintf(out, "    %.*s region_%.*s;\n", length, text, length, text);
    }
    fputs("} EngineMemory;\n\n#endif\n", out);
    return !ferror(out);
}
```

Keep fields in declaration order. Don't open the outputs until parsing and validation succeed. When writing to a file, check both the emitter's result and `fclose`: a buffered write can fail during close.

## 5. Emit the tables and shared API

Generate expressions such as `offsetof(Player, hp)` rather than calculating padding in the generator. The compiler that builds each DLL then measures the layout it actually uses; [`offsetof` returns the member's byte offset](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) (C99 §7.17). This is why you can reorder the fixture and see the metadata follow the declaration.

This field table is the other required output of the DSL. `memory.h` lets engine code use fields normally; `memory_layout.inc` lets the host inspect the layout using data rather than C member names baked into the host. That's **generated reflection metadata**: the compiler checks and measures the declared fields, and the DLL exposes a table you can inspect at runtime. It is not built-in C reflection and describes only the types in your schema.

### Task 1: Emit one field table per struct, then the region table

In `codegen-lab/generate_memory.c`, add `emit_layout_tables` below `emit_memory_header` and above `main`. Pass it the **same validated declarations, in the same order**, as the header emitter. For a field, `%.*s` prints the member name from its token span. `schema_types` supplies both `STORAGE_I32` and `int32_t`, so the type in the layout row agrees with the declaration. The second loop emits a region row that references the field table emitted in the first loop.

```c
static int emit_layout_tables(FILE *out, const char *source,
                              const StructDecl *decls, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const StructDecl *decl = &decls[i];
        const int type_length = (int)decl->name.length;
        const char *type_name = source + decl->name.start;
        fprintf(out, "static const FieldLayout fields_%.*s[] = {\n",
                type_length, type_name);
        for (size_t j = 0; j < decl->field_count; ++j) {
            const FieldDecl *field = &decl->fields[j];
            fprintf(out, "    FIELD(%s, %.*s, %.*s, %s),\n",
                    schema_types[field->type].storage_kind,
                    type_length, type_name,
                    (int)field->name.length, source + field->name.start,
                    schema_types[field->type].c_type);
        }
        fputs("};\n\n", out);
    }

    fputs("static const RegionLayout regions[] = {\n", out);
    for (size_t i = 0; i < count; ++i) {
        const Token name = decls[i].name;
        const int length = (int)name.length;
        const char *text = source + name.start;
        fprintf(out, "    REGION(\"%.*s\", region_%.*s, %.*s, fields_%.*s),\n",
                length, text, length, text, length, text, length, text);
    }
    fputs("};\n\n", out);
    return !ferror(out);
}
```

When you run the file-based generator below, the v2 layout table should contain these rows. The Google Test checkpoint compiles them and asserts the resulting values:

```c
static const FieldLayout fields_Player[] = {
    FIELD(STORAGE_I32, Player, ammo, int32_t),
    FIELD(STORAGE_I32, Player, hp, int32_t),
};

static const RegionLayout regions[] = {
    REGION("Player", region_Player, Player, fields_Player),
};
```

`FIELD(STORAGE_I32, Player, hp, int32_t)` expands to `{ "hp", STORAGE_I32, offsetof(Player, hp), sizeof(int32_t), _Alignof(int32_t) }` in this MSVC C11 build. `offsetof(Player, hp)` is relative to the start of `Player`; `offsetof(EngineMemory, region_Player)` in `REGION` is relative to the start of the block. **The generator prints expressions; the compiler supplies the actual numbers.**

### Task 2: Assemble a complete `memory_layout.inc`

From `codegen-lab`, create `templates/`. Use the complete `memory_layout.inc` example in [Tutorial 06, §3](06-dll-migration-tutorial.md#3-return-a-layout-from-inside-the-dll) to make these three plain-text files:

| Template file | Exact contents to copy |
| --- | --- |
| `templates/memory_layout_prefix.inc` | The three `#include` lines and the three macros, from `#include "engine_api.h"` through the closing line of `#define REGION`. |
| `templates/memory_layout_suffix.inc` | Everything from `#ifndef ENGINE_LAYOUT_EXPORT` through `#undef COUNT`. |
| `templates/engine_api.h` | Copy the planner's `memorymigration/memory/engine_api.h` from [Step 01a](01a-copy-plan-tutorial.md). Add `#define ENGINE_API_VERSION 1u` after `<stdint.h>` in the template and in the planner header, so both use the same version. |

End each template with a newline. **Do not copy either sample table** into the templates: `emit_layout_tables` writes those. The ABI header is a fixed template because the host-side types must not change when you add an engine field.

In `generate_memory.c`, add these helpers below `emit_layout_tables`. `copy_template` streams the fixed text unchanged into the output. `write_layout_file` opens the output, writes prefix → tables → suffix, and checks the final close. Open the output in binary mode so copying a Windows template does not translate its line endings again.

```c
static int copy_template(FILE *out, const char *path)
{
    FILE *in = fopen(path, "rb");
    if (!in) {
        perror(path);
        return 0;
    }

    unsigned char buffer[1024];
    size_t n;
    int ok = 1;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(in))
        ok = 0;
    if (fclose(in) != 0)
        ok = 0;
    return ok;
}

static int write_layout_file(const char *path, const char *source,
                             const StructDecl *decls, size_t count)
{
    FILE *out = fopen(path, "wb");
    if (!out) {
        perror(path);
        return 0;
    }

    int ok = copy_template(out, "templates/memory_layout_prefix.inc") &&
             emit_layout_tables(out, source, decls, count) &&
             copy_template(out, "templates/memory_layout_suffix.inc");
    if (fclose(out) != 0)
        ok = 0;
    if (!ok)
        fprintf(stderr, "could not finish %s\n", path);
    return ok;
}
```

The next checkpoint calls this writer from the generator build tool. When inspecting its output, expect the macros first, then the two tables, then `engine_describe_layout`. If a template cannot open, check that the generator ran from `codegen-lab`.

## 6. Wire the steps together

In `codegen-lab/generate_memory.c`, add `write_outputs` above `main` and below the emitter helpers. It writes all three files from one invocation. Each output path is built from the caller's directory, and each stream is closed even if its emitter fails.

```c
static int write_outputs(const char *dir, const char *source,
                         const StructDecl *decls, size_t count)
{
    static const char *const names[] = {
        "memory.h", "engine_api.h", "memory_layout.inc"
    };
    char paths[3][1024];
    for (size_t i = 0; i < 3; ++i) {
        int n = snprintf(paths[i], sizeof paths[i], "%s/%s", dir, names[i]);
        if (n < 0 || (size_t)n >= sizeof paths[i]) {
            fputs("output path is too long\n", stderr);
            return 0;
        }
    }

    for (size_t i = 0; i < 2; ++i) {
        FILE *out = fopen(paths[i], "wb");
        if (!out) {
            perror(paths[i]);
            return 0;
        }
        int ok = i == 0 ? emit_memory_header(out, source, decls, count)
                        : copy_template(out, "templates/engine_api.h");
        if (fclose(out) != 0)
            ok = 0;
        if (!ok) {
            fprintf(stderr, "could not finish %s\n", paths[i]);
            return 0;
        }
    }
    return write_layout_file(paths[2], source, decls, count);
}
```

Append this file reader and build-tool entry point to `codegen-lab/generate_memory.c`, after `write_outputs`. `CODEGEN_TEST` excludes the build-tool entry point from the Google Test build. The lexer uses a byte count, not a terminating zero. The extra byte makes the size-limit check unambiguous: exactly 4096 bytes fit; 4097 do not. The source stays in scope through emission because each token stores a span into it. [`fread` counts items actually read](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fread); with item size 1 that count is bytes. Check `ferror` separately so a failed read is not mistaken for a short file.

```c
#ifndef CODEGEN_TEST
static int read_schema(const char *path, char *buffer, size_t capacity,
                       size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    size_t n = fread(buffer, 1, capacity, file);
    int ok = !ferror(file) && n < capacity;
    if (fclose(file) != 0)
        ok = 0;
    if (ok)
        *length = n;
    return ok;
}

int main(int argc, char **argv)
{
    char source[4097];
    size_t length = 0;
    StructDecl decl;
    if (argc != 3 || !read_schema(argv[1], source, sizeof source, &length)) {
        fputs("usage: generate_memory <readable schema, max 4096 bytes> <output dir>\n", stderr);
        return EXIT_FAILURE;
    }
    if (!parse_schema(source, length, &decl)) {
        fputs("invalid .mem schema\n", stderr);
        return EXIT_FAILURE;
    }
    return write_outputs(argv[2], source, &decl, 1) ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
```

This first build tool handles the single-struct fixture. Add multiple declarations and identifier validation as separate parser checkpoints before accepting general schemas.

From `codegen-lab` in the x64 Native Tools Command Prompt, rebuild and run (skip any `mkdir` for a directory you already created):

```cmd
mkdir build
mkdir build\v1
mkdir build\v2
cl /nologo /W4 /TC /std:c11 lexer.c generate_memory.c /Fe:build\generate_memory.exe
build\generate_memory.exe schemas\player-v1.mem build\v1
build\generate_memory.exe schemas\player-v2.mem build\v2
```

If a command can't open an input, template, or output, check that you're in `codegen-lab` and that both output directories exist. Each directory should contain `engine_api.h`, `memory.h`, and `memory_layout.inc`.

## 7. Compile the generated tables and read them back

In `codegen-lab/generated_v1.c`, include the generated table under fixture-specific function names. In `generated_v2.c`, use `generated_version_v2`, `generated_layout_v2`, and `build/v2/memory_layout.inc` instead. Compile both as C (`/TC /std:c11`), with precompiled headers disabled for these two files, by adding them as **Existing Items** to `memory_tests`. Keep them in `codegen-lab` so the relative includes find their outputs. Two translation units keep incompatible `Player` definitions out of the same C file.

```c
/* Link these as ordinary test functions, not DLL exports. */
#define ENGINE_LAYOUT_EXPORT
#define engine_api_version generated_version_v1
#define engine_describe_layout generated_layout_v1
#include "build/v1/memory_layout.inc"
```

Append this test and the two declarations to `memorymigration/memory_test/test.cpp`. It asserts what the disk fixtures generated, then substitutes those descriptions for the hand-written ones from Step 01a. The test uses integer arrays as backing fixtures for this fixed MSVC x64 case; the offsets are asserted before copying.

```cpp
extern "C" int __cdecl generated_layout_v1(const void *, EngineLayout *);
extern "C" int __cdecl generated_layout_v2(const void *, EngineLayout *);

TEST(GeneratedLayout, FileFixturesDriveTheSamePlanner)
{
    EngineLayout a = {0}, b = {0};
    ASSERT_TRUE(generated_layout_v1(NULL, &a));
    ASSERT_TRUE(generated_layout_v2(NULL, &b));
    ASSERT_EQ(1u, a.region_count);
    ASSERT_EQ(1u, b.region_count);
    ASSERT_EQ(1u, a.regions[0].field_count);
    ASSERT_EQ(2u, b.regions[0].field_count);
    EXPECT_STREQ("Player", a.regions[0].name);
    EXPECT_STREQ("Player", b.regions[0].name);
    EXPECT_STREQ("hp", a.regions[0].fields[0].name);
    EXPECT_STREQ("hp", b.regions[0].fields[1].name);
    EXPECT_EQ(STORAGE_I32, a.regions[0].fields[0].kind);
    EXPECT_EQ(STORAGE_I32, b.regions[0].fields[1].kind);
    EXPECT_EQ(sizeof(int32_t), a.regions[0].fields[0].size);
    EXPECT_EQ(sizeof(int32_t), b.regions[0].fields[1].size);
    EXPECT_EQ(0u, a.regions[0].fields[0].offset);
    EXPECT_EQ(sizeof(int32_t), b.regions[0].fields[1].offset);

    int32_t old_values[] = {37}, new_values[] = {12, 0};
    ASSERT_EQ(sizeof old_values, a.block_size);
    ASSERT_EQ(sizeof new_values, b.block_size);
    const MemoryBlock src = {(unsigned char *)&old_values, sizeof old_values};
    const MemoryBlock dest = {(unsigned char *)&new_values, sizeof new_values};
    CopyOp op = {0};
    size_t count = 0;
    ASSERT_TRUE(build_copy_plan(&a, &b, &op, 1, &count));
    ASSERT_EQ(1u, count);
    ASSERT_TRUE(copy_field(&src, &dest, op));
    EXPECT_EQ(37, new_values[1]);
    EXPECT_EQ(12, new_values[0]);
}
```

Build `memory_tests` and run `GeneratedLayout.FileFixturesDriveTheSamePlanner` in Test Explorer. Expect one pass. As a file-input failure check, create a third fixture by replacing `i32` with `i23`, and run the generator on it into an empty output directory. Expect a nonzero exit status and no generated files; don't compile stale outputs after a failed invocation.

`hp` keeps its name and storage kind but moves from offset `0` to `4`. This checkpoint uses the generated `EngineMemory` aggregate as its whole block, so `REGION` can measure placement with `offsetof`. When you wire it into the [byte-zero `Memory` index](02-memory-index-tutorial.md), keep the generated **field** tables but take each **region's block offset** from that DLL's index. An offset measured in the aggregate does not describe a region placed after the index.

If both builds report the same `hp` offset, check the wrapper include paths and regenerate both schema outputs. If the compiler reports an unknown `region_Player` or `fields_Player`, check that the region member and table prefixes agree between both emission loops.

Next: [06 · One-shot DLL migration](06-dll-migration-tutorial.md). First resolve the fixed-layout export in one DLL. Then use the live-layout exports in two DLLs and reuse the planner.
