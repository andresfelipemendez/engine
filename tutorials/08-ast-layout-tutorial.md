# 08 · From lexer tokens to a C struct AST and memory layout (optional)

Your end goal has two parts: **discover which structs and fields the source declares**, then **determine where those fields sit in an object**. Keep those as separate passes. The parser reads syntax. The layout pass uses the target compiler's type sizes, alignments, and packing rules. Nothing in the text `int count;` tells the lexer its offset within a struct.

Work through one struct first, then parse a list. These are snippets to adapt in a scratch project, not a complete C parser.

## 1. Pick a tiny grammar and an expected result

Start with tagged struct definitions containing one or more named fields of type `char`, `int`, or `double`. For now, require one field per declaration. No typedefs, pointers, arrays, bit-fields, or packing directives yet.

```c
struct Record {
    char tag;
    int count;
    double value;
    char end;
};
```

Describe that subset as a grammar. A quoted word or punctuation mark below corresponds to one token from the lexer:

```text
file       = struct_def* EOF
struct_def = "struct" IDENTIFIER "{" field+ "}" ";"
field      = builtin_type IDENTIFIER ";"
builtin_type = "char" | "int" | "double"
```

Add `TOK_KW_STRUCT`, `TOK_KW_CHAR`, and `TOK_KW_DOUBLE` to the lexer tutorial's enum and keyword classifier. `TOK_KW_INT` is already there. Add `TOK_LBRACE` and `TOK_RBRACE` to its punctuation switch. First run your token dump on the fixture; don't debug the lexer and parser at the same time.

Your parser's first output should be something like:

```text
Struct Record
  Field tag: char
  Field count: int
  Field value: double
  Field end: char
```

There are deliberately no byte offsets in that output yet. The source token for `count` has an offset **in the header text**. The field `count` will later get a different offset **in an instance of `struct Record`**.

## 2. Decide what the AST stores

Reuse the `Token` type from the lexer tutorial. Here, a name token is a compact reference back to the original source bytes. An arena-allocated linked list is a simple first choice: you can add fields without knowing how many are coming, and you can keep them in declaration order.

```c
typedef enum {
    TYPE_CHAR, TYPE_INT, TYPE_DOUBLE
} TypeKind;

typedef struct FieldDecl {
    TypeKind type;
    Token name;
    struct FieldDecl *next;
} FieldDecl;

typedef struct StructDecl {
    Token tag;
    FieldDecl *first_field;
    struct StructDecl *next;
} StructDecl;
```

Think of this as `file → list of structs → list of fields`. The links aren't the layout of the described C structs: they are just how your **parser's own objects** are organized. Keep the file buffer alive so you can print a name with `fwrite(source + name.start, 1, name.length, stdout)`.

## 3. Build one node at a time with one lookahead token

A parser often needs to look before consuming. For example, when reading a struct body, `}` means “there are no more fields.” Keep one current token in the parser. `take` advances only when that token is the kind you expected.

```c
/* These examples use lexer.h and arena.h from the earlier tutorials. */
typedef struct {
    Lexer lex;
    Token current;
    Arena *arena;
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
```

Initialize `Parser p = {0};`, set `p.lex` to a fresh lexer over your source, set `p.arena = &arena`, and then call `p.current = lexer_next(&p.lex);` **once** before parsing. From then on, `take` is what advances through the input.

Each grammar rule can be a function. Here's the field rule. Notice that it validates the tokens before it allocates the node:

```c
static FieldDecl *parse_field(Parser *p)
{
    TypeKind type;
    if (take(p, TOK_KW_CHAR, NULL))
        type = TYPE_CHAR;
    else if (take(p, TOK_KW_INT, NULL))
        type = TYPE_INT;
    else if (take(p, TOK_KW_DOUBLE, NULL))
        type = TYPE_DOUBLE;
    else
        return NULL;

    Token name;
    if (!take(p, TOK_IDENTIFIER, &name) ||
        !take(p, TOK_SEMICOLON, NULL))
        return NULL;

    FieldDecl *field = arena_alloc(p->arena, sizeof *field,
                                   _Alignof(FieldDecl));
    if (field == NULL)
        return NULL;
    *field = (FieldDecl){type, name, NULL};
    return field;
}
```

Try parsing `int count;` directly with `parse_field`. Now remove the semicolon: the identifier is valid, but `take` sees EOF instead of `;`, so the field rule fails. For a first debugger or token dump, inspect `p.current.start` on failure. Later, report the expected token and track allocation failure separately from syntax errors.

Next, write `parse_struct` as a driver for that field rule:

1. Take `TOK_KW_STRUCT`, the tag identifier, and `TOK_LBRACE`.
2. Read fields until the current token is `TOK_RBRACE`. For each field, call `parse_field`; **stop if it fails**, including on unexpected EOF. Append each node at the tail, not the head, or you'll reverse the declaration order and compute the wrong layout.
3. Reject an empty body for this subset. Take `TOK_RBRACE` and then `TOK_SEMICOLON`.
4. Allocate a `StructDecl` and store the tag and the field list. At file scope, call `parse_struct` repeatedly until EOF and append each result to a struct list. Don't continue after a rule fails in this initial version.

One way to append without a special case for the first field uses a pointer to the link where the next field goes:

```c
FieldDecl *first = NULL;
FieldDecl **link = &first;

while (p->current.kind != TOK_RBRACE) {
    FieldDecl *field = parse_field(p);
    if (field == NULL)
        return NULL;
    *link = field;
    link = &field->next;
}
```

`link` begins by referring to `first`. After the first append, it refers to the first field's `next`. Draw those two states if the double pointer feels unfamiliar. A regular `first` and `last` pair works too.

## 4. Compute layout in a second pass

Pick a target. For this exercise, use **MSVC x64 with default packing**. You can get the size and natural alignment of `char`, `int`, and `double` from `sizeof` and `_Alignof` **if your parser is compiled for the same target**. If you want your tool to analyze another target, pass it that target's type layout information instead of using the tool's own `sizeof`.

Start with `cursor = 0` and `struct_alignment = 1`. For each field, in declaration order:

1. Look up its `size` and `alignment` from its type.
2. Round `cursor` up to a multiple of the field's alignment. That is the **field offset**.
3. Advance `cursor` to `field_offset + size`.
4. Track the largest alignment of any field.

Finally, round the cursor up to the struct's alignment to get the **struct size**. That trailing padding makes the next element in an array of these structs start at a correctly aligned address. A simple way to find padding is `remainder = cursor % alignment`, then `padding = remainder ? alignment - remainder : 0`. Check for overflow before adding padding or field size.

For `Record`, under the stated target and packing:

| Field | Size | Alignment | Offset | Padding before |
| --- | ---: | ---: | ---: | ---: |
| `tag` | 1 | 1 | 0 | 0 |
| `count` | 4 | 4 | 4 | 3 |
| `value` | 8 | 8 | 8 | 0 |
| `end` | 1 | 1 | 16 | 0 |

The cursor ends at 17. The struct has alignment 8, so its size is **24 bytes**, including 7 bytes of tail padding. If you later inspect an actual `Record` object, the address of a field is the object's byte address plus that field's offset. **No runtime address exists just because you parsed a declaration.**

Store this separately from the AST at first. For example, a layout record can point to its `StructDecl`, store the total size and alignment, and have one `{field, offset, size, alignment}` result per field. Later, this also gives you a place to store layouts for the same AST under different targets or packing rules.

## 5. Use the compiler to check your work

In your scratch folder, create `record_layout_check.c`. Copy `struct Record` from section 1 into it, then append the complete snippet below. This is a standalone compiler check; don't add it to the parser build.

```c
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    printf("size=%zu align=%zu\n", sizeof(struct Record),
           _Alignof(struct Record));
    printf("tag=%zu count=%zu value=%zu end=%zu\n",
           offsetof(struct Record, tag), offsetof(struct Record, count),
           offsetof(struct Record, value), offsetof(struct Record, end));
}
```

In an x64 Native Tools Command Prompt, change to the scratch folder and run `cl /nologo /W4 /TC /std:c11 record_layout_check.c`, then `record_layout_check.exe`. This verifier uses C11's `_Alignof`; for MSVC in a mode that lacks it, use `__alignof(struct Record)` instead. With default MSVC x64 packing, expect:

```text
size=24 align=8
tag=0 count=4 value=8 end=16
```

If the numbers differ, first check the field order, target, and packing flags, then compare these numbers with the layout pass, not the lexer offsets. Then change the field order or add a second struct and predict the new result before running the check again.

**What to add next:** Try fixed-size arrays, then pointers, then fields that are another previously defined struct. At that point, you'll want a type representation richer than the three-value `TypeKind` and a lookup table for struct tags. Treat `#pragma pack`, explicit alignment, unions, and bit-fields as later features; don't silently claim correct offsets for input that uses rules you haven't modeled. If you plan to migrate memory between struct versions, record the layout for **each version separately**—matching field names do not imply matching offsets.

Return to the main path at [05 · The `.mem` code generator](05-c-codegen-tutorial.md).
