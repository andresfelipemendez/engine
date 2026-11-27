# 05 · Build a tiny memory-schema generator in C

Instead of TOML or parsing full C headers, give the generator one job: read a small declaration language and emit C. Start with `Player`, then feed its generated layout tables into the [DLL migration tutorial](06-dll-migration-tutorial.md).

```text
Player.mem → lexer → parser → structs and fields → generated C → cl /LD
```

## Prerequisites

Finish [04 · Lexer](04-lexer-tutorial.md). Create `codegen-lab` outside the repository and copy your working `lexer.h` and `lexer.c` into it. Keep the existing TOML prototype in `generate_memory/main.c` untouched while you build the scratch version.

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

Start from the [lexer tutorial](04-lexer-tutorial.md). In the scratch `lexer.h`, add `TOK_KW_STRUCT`, `TOK_LBRACE`, and `TOK_RBRACE` to `TokenKind`. In the scratch `lexer.c`, teach the keyword classifier about `struct` and add `{` and `}` to the punctuation switch. The lexer should return `i32`, `hp`, and `Player` as `TOK_IDENTIFIER`. It already knows digits are allowed after the first letter of an identifier.

Copy `lexer_demo.c` from `lexer-lab` into `codegen-lab`. In its `kind_name` switch, add labels `KW_STRUCT`, `LBRACE`, and `RBRACE` for the new token kinds. In its `main`, replace the input literal with `"struct Player { i32 hp }"`. From `codegen-lab` in the x64 Native Tools Command Prompt, run `cl /nologo /W4 /TC /std:c11 lexer.c lexer_demo.c /Fe:lexer_demo.exe` and then `lexer_demo.exe`. Ignoring the byte spans, the v1 dump should show:

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

Create `codegen-lab/generate_memory.c` beside `lexer.c`. Start it with the includes, types, and helpers below, then append a test `main` after `parse_struct`.

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

In `generate_memory.c`, add `#include <stdio.h>` and `#include <stdlib.h>` beside the other includes. Append this temporary `main` below `parse_struct`. The first `lexer_next` primes the parser's lookahead; checking for EOF ensures the parser didn't just accept a prefix.

```c
int main(void)
{
    const char source[] = "struct Player { i32 ammo i32 hp }";
    Parser p = {0};
    p.lex = (Lexer){source, sizeof(source) - 1, 0};
    p.current = lexer_next(&p.lex);
    StructDecl decl;
    if (!parse_struct(&p, &decl) || p.current.kind != TOK_EOF) {
        fprintf(stderr, "parse failed at byte %zu\n", p.current.start);
        return EXIT_FAILURE;
    }
    printf("Struct %.*s\n", (int)decl.name.length, source + decl.name.start);
    for (size_t i = 0; i < decl.field_count; ++i) {
        const FieldDecl field = decl.fields[i];
        printf("  %s %.*s\n", field.type == SCHEMA_I32 ? "i32" : "u32",
               (int)field.name.length, source + field.name.start);
    }
    return EXIT_SUCCESS;
}
```

Save both C files. In an x64 Native Tools Command Prompt, change to `codegen-lab` and run:

```cmd
cl /nologo /W4 /TC /std:c11 lexer.c generate_memory.c /Fe:parse_demo.exe
parse_demo.exe
```

Expect:

```text
Struct Player
  i32 ammo
  i32 hp
```

Change the first `i32` in the string literal to `i23`; the program should report a parse failure at that token. If valid input fails at byte zero, inspect the first token: it should be `TOK_KW_STRUCT`, not `TOK_IDENTIFIER`. Restore `i32` before continuing. Then give distinct errors for a missing closing brace, an unknown type, and a missing field name. Once the one-struct driver works, add a fixed-capacity array of `StructDecl` at file scope and repeat `parse_struct` until EOF. Before generating C, reject duplicate struct names and duplicate field names in a struct, C keywords or reserved names, collisions with your generated symbols, and names longer than a limit such as 63 bytes.

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

In `codegen-lab/generate_memory.c`, insert `emit_struct` below `parse_struct` and above `main` (`<stdio.h>` is already included). Once you validate the name length, use `%.*s` with the token's length to print its spelling without allocating a NUL-terminated copy. Call `emit_struct(stdout, source, &decl)` after successful parsing to check one emitted typedef before you start opening output files. Then call it once per struct and emit `EngineMemory` from the same struct list:

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

For the first check, replace everything after the successful parse check in the temporary `main` (the `printf`, field-printing loop, and final return) with `return emit_layout_tables(stdout, source, &decl, 1) ? EXIT_SUCCESS : EXIT_FAILURE;`. Rebuild with the `parse_demo.exe` command in §3 and run it from `codegen-lab`. With the original v2 input, expect:

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

From `codegen-lab`, create `templates/`. Use the complete `memory_layout.inc` example in [Tutorial 06, §3](06-dll-migration-tutorial.md#3-return-a-layout-from-inside-the-dll) to make these two plain-text files:

| Template file | Exact contents to copy |
| --- | --- |
| `templates/memory_layout_prefix.inc` | The three `#include` lines and the three macros, from `#include "engine_api.h"` through the closing line of `#define REGION`. |
| `templates/memory_layout_suffix.inc` | Everything from the definition of `engine_api_version` through `#undef COUNT`. |
| `templates/engine_api.h` | The whole ABI header code block in [Tutorial 06, §1](06-dll-migration-tutorial.md#1-give-the-host-and-dll-a-small-stable-api). |

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

To try assembly with the string fixture first, from `codegen-lab` create `build\v2`. In the temporary `main`, replace the stdout call with `return write_layout_file("build/v2/memory_layout.inc", source, &decl, 1) ? EXIT_SUCCESS : EXIT_FAILURE;`. Rebuild and run `parse_demo.exe`. Open the output file: the macros should be first, then the two tables, then `engine_describe_layout`. If a template cannot open, check that you ran from `codegen-lab`, not `build\v2`. The next task gives the generated file its companion headers so you can compile it.

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

Once you've validated the string fixture, change the test entry point to `int main(int argc, char **argv)`. Require `argc == 3`: read the schema from `argv[1]` into a buffer (open in binary mode) and pass that buffer and its length to `Parser`. After parsing and validation, call `write_outputs(argv[2], source, &decl, 1)` for the single-struct fixture, or pass your declaration array and its count after you add support for multiple structs. Convert the result to `EXIT_SUCCESS` or `EXIT_FAILURE`. Keep the source buffer alive until writing finishes; the token spans still refer to it. Don't compile the generated files if the generator returns failure.

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

Create `codegen-lab/check_layout.c` with the following contents. It compiles the same generated `.inc` the DLL will include, but calls `engine_describe_layout` directly so you can test generation without loading a DLL. The printing loop reads names, kinds, offsets, and sizes from the table; it never names a `Player` member in C.

```c
#include "memory_layout.inc"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    EngineLayout layout = {0};
    if (!engine_describe_layout(NULL, &layout))
        return EXIT_FAILURE;

    for (uint32_t i = 0; i < layout.region_count; ++i) {
        const RegionLayout *region = &layout.regions[i];
        for (uint32_t j = 0; j < region->field_count; ++j) {
            const FieldLayout *field = &region->fields[j];
            printf("%s.%s kind=%" PRIu32 " offset=%" PRIu64 " size=%" PRIu64 "\n",
                   region->name, field->name, field->kind,
                   field->offset, field->size);
        }
    }
    return EXIT_SUCCESS;
}
```

From `codegen-lab` in the same prompt, compile the checker once per output directory:

```cmd
cl /nologo /W4 /TC /std:c11 /Ibuild\v1 check_layout.c /Fe:build\check_v1.exe
build\check_v1.exe
cl /nologo /W4 /TC /std:c11 /Ibuild\v2 check_layout.c /Fe:build\check_v2.exe
build\check_v2.exe
```

Expected on MSVC x64, with v1 first and then v2:

```text
Player.hp kind=1 offset=0 size=4
Player.ammo kind=1 offset=0 size=4
Player.hp kind=1 offset=4 size=4
```

`hp` keeps its name and storage kind but moves from offset `0` to `4`. This checkpoint uses the generated `EngineMemory` aggregate as its whole block, so `REGION` can measure placement with `offsetof`. When you wire it into the [byte-zero `Memory` index](02-memory-index-tutorial.md), keep the generated **field** tables but take each **region's block offset** from that DLL's index. An offset measured in the aggregate does not describe a region placed after the index.

If both builds report the same `hp` offset, check the `/I` paths and make sure no generated files are sitting beside `check_layout.c` and shadowing the selected version. If the compiler reports an unknown `region_Player` or `fields_Player`, check that the region member and table prefixes agree between both emission loops.

Next: [06 · One-shot DLL migration](06-dll-migration-tutorial.md). Instead of calling `engine_describe_layout` directly, the host calls it in each DLL and compares the two tables to build the copy plan.
