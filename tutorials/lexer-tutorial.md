# Write your first C header lexer

A **lexer** (or **tokenizer**) turns bytes into meaningful pieces called tokens. A **parser** turns those pieces into structure. In `int *count;`, the lexer sees a keyword, a star, a name, and a semicolon. The parser is the part that decides the star means `count` is a pointer.

Work through this in a separate scratch folder. The first four sections give you a runnable example with three files. No arena or file I/O is required to get the lexer working; you'll connect it to your existing loader afterward.

## 1. Decide what a token looks like (`lexer.h`)

For `extern int *count;`, the lexer should produce:

| Kind | Text | Byte span |
| --- | --- | --- |
| `TOK_KW_EXTERN` | `extern` | `[0, 6)` |
| `TOK_KW_INT` | `int` | `[7, 10)` |
| `TOK_STAR` | `*` | `[11, 12)` |
| `TOK_IDENTIFIER` | `count` | `[12, 17)` |
| `TOK_SEMICOLON` | `;` | `[17, 18)` |
| `TOK_EOF` | — | `[18, 18)` |

`[12, 17)` means start at byte 12 and stop **before** byte 17. You can store this as `start = 12`, `length = 5`. The token doesn't need its own copy of `count`; the original bytes are still in the source buffer.

```c
#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOK_EOF, TOK_ERROR,
    TOK_IDENTIFIER, TOK_DECIMAL,
    TOK_KW_INT, TOK_KW_EXTERN,
    TOK_STAR, TOK_SEMICOLON, TOK_COMMA,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET
} TokenKind;

typedef struct {
    TokenKind kind;
    size_t start;
    size_t length;
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t pos;            /* Offset of the next byte to inspect. */
} Lexer;

Token lexer_next(Lexer *lex);

#endif
```

`Lexer` is the reading state. `Token` is a description of what was just read. Passing `Lexer *` means every call to `lexer_next` advances the same cursor.

## 2. Write the small helpers (`lexer.c`)

Start with **peek**: look at a byte without advancing. Use `int` as the return type so `-1` can mean end of input, while all actual bytes are in `0..255`. Check the boundary *before* looking into the buffer.

```c
#include "lexer.h"
#include <string.h>

static int peek(const Lexer *lex, size_t ahead)
{
    /* The scanner maintains pos <= length. */
    if (ahead >= lex->length - lex->pos)
        return -1;
    return (unsigned char)lex->source[lex->pos + ahead];
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_name_start(int c)
{
    return c == '_' || (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z');
}

static Token token(const Lexer *lex, TokenKind kind, size_t start)
{
    Token t = {kind, start, lex->pos - start};
    return t;
}

static TokenKind name_kind(const char *text, size_t length)
{
    if (length == 3 && memcmp(text, "int", 3) == 0)
        return TOK_KW_INT;
    if (length == 6 && memcmp(text, "extern", 6) == 0)
        return TOK_KW_EXTERN;
    return TOK_IDENTIFIER;
}
```

The name classifier checks the **length and contents**. The characters in a token are not necessarily followed by `\0`, so don't use `strcmp` on a slice of the source. Consume the whole identifier before classifying it: otherwise `integer` could incorrectly become `int` plus `eger`.

## 3. Implement `lexer_next` (`lexer.c`, continued)

The pattern is **skip whitespace → remember start → consume one token → return its span**. A second call starts right where the first one finished.

```c
Token lexer_next(Lexer *lex)
{
    while (is_space(peek(lex, 0)))
        lex->pos++;

    size_t start = lex->pos;
    int c = peek(lex, 0);
    if (c == -1)
        return token(lex, TOK_EOF, start);

    if (is_name_start(c)) {
        do {
            lex->pos++;
            c = peek(lex, 0);
        } while (is_name_start(c) || is_digit(c));

        TokenKind kind = name_kind(lex->source + start, lex->pos - start);
        return token(lex, kind, start);
    }

    if (is_digit(c)) {
        do {
            lex->pos++;
        } while (is_digit(peek(lex, 0)));
        return token(lex, TOK_DECIMAL, start);
    }

    /* Punctuation is one byte in this subset. Consume unknown bytes too. */
    lex->pos++;
    switch (c) {
    case '*': return token(lex, TOK_STAR, start);
    case ';': return token(lex, TOK_SEMICOLON, start);
    case ',': return token(lex, TOK_COMMA, start);
    case '(': return token(lex, TOK_LPAREN, start);
    case ')': return token(lex, TOK_RPAREN, start);
    case '[': return token(lex, TOK_LBRACKET, start);
    case ']': return token(lex, TOK_RBRACKET, start);
    default:  return token(lex, TOK_ERROR, start);
    }
}
```

Trace it with `*count;`: the first call consumes byte 0 and returns the star. The second advances over `count` and stops when it **peeks** at `;`. It does not consume the semicolon. The third call consumes `;`. The fourth returns a zero-length EOF token. Every non-EOF token consumes at least one byte, so a loop can't get stuck on unknown input.

The decimal rule reads digits; it does not convert them to an integer yet. This is enough for `int values[16];`, but not enough to tokenize general C numbers such as `0xFF` or `3.5f` correctly.

## 4. See it run (`lexer_demo.c`)

Start with a string literal rather than your Windows file loader. That way if the test fails, you only have the lexer to debug.

```c
#include "lexer.h"
#include <stdio.h>

static const char *kind_name(TokenKind kind)
{
    switch (kind) {
    case TOK_EOF:        return "EOF";
    case TOK_ERROR:      return "ERROR";
    case TOK_IDENTIFIER: return "IDENTIFIER";
    case TOK_DECIMAL:    return "DECIMAL";
    case TOK_KW_INT:     return "KW_INT";
    case TOK_KW_EXTERN:  return "KW_EXTERN";
    case TOK_STAR:       return "STAR";
    case TOK_SEMICOLON:  return "SEMICOLON";
    case TOK_COMMA:      return "COMMA";
    case TOK_LPAREN:     return "LPAREN";
    case TOK_RPAREN:     return "RPAREN";
    case TOK_LBRACKET:   return "LBRACKET";
    case TOK_RBRACKET:   return "RBRACKET";
    }
    return "UNKNOWN";
}

int main(void)
{
    const char input[] = "extern int *count;";
    Lexer lex = {input, sizeof input - 1, 0};  /* Exclude the trailing NUL. */

    for (;;) {
        Token t = lexer_next(&lex);
        printf("%-12s [%zu, %zu)  ", kind_name(t.kind),
               t.start, t.start + t.length);
        if (t.length != 0)
            fwrite(lex.source + t.start, 1, t.length, stdout);
        putchar('\n');
        if (t.kind == TOK_EOF || t.kind == TOK_ERROR)
            return t.kind == TOK_ERROR ? 1 : 0;
    }
}
```

From an MSVC developer shell in your scratch folder:

```powershell
cl /nologo /std:c11 /W4 lexer.c lexer_demo.c /Fe:lexer_demo.exe
.\lexer_demo.exe
```

The spans should match the table above. Next, change only `input` and try:

| Input | What to check |
| --- | --- |
| `""` | EOF at offset 0 |
| `"int integer;"` | `int` is a keyword, `integer` is a single identifier |
| `"int *a[16];"` | `16` is one decimal token, and brackets are separate tokens |
| `"int (*a)[4];"` | Parentheses are tokens; it's not the lexer's job to understand them |
| `"int a;\r\nint b;"` | The second `int` starts at byte 8, after both newline bytes |
| `"@"` | An error token consumes one byte and the driver stops |

Once those work, initialize the same lexer from the loader: `Lexer lex = {sb.data, sb.length, 0};`. You can keep the token on the stack and return it by value. Only allocate a token array in the arena if you actually need to keep a copy of every token. Either way, the source bytes must stay alive as long as you're using token offsets to read spellings.

## 5. Add comments without changing token boundaries

Only try this after the starter lexer works. Add this helper **above** `lexer_next` in `lexer.c`:

```c
/* 1 means ready to scan the next token; 0 means an unterminated comment. */
static int skip_trivia(Lexer *lex, Token *error)
{
    for (;;) {
        while (is_space(peek(lex, 0)))
            lex->pos++;

        if (peek(lex, 0) != '/')
            return 1;

        if (peek(lex, 1) == '/') {
            lex->pos += 2;
            int c;
            while ((c = peek(lex, 0)) != -1 && c != '\r' && c != '\n')
                lex->pos++;
        } else if (peek(lex, 1) == '*') {
            size_t start = lex->pos;
            lex->pos += 2;
            while (peek(lex, 0) != -1 &&
                   !(peek(lex, 0) == '*' && peek(lex, 1) == '/'))
                lex->pos++;

            if (peek(lex, 0) == -1) {
                *error = token(lex, TOK_ERROR, start);
                return 0;
            }
            lex->pos += 2;
        } else {
            return 1;  /* Leave the slash for lexer_next to handle. */
        }
    }
}
```

Replace the initial whitespace loop in `lexer_next` with:

```c
    Token error;
    if (!skip_trivia(lex, &error))
        return error;
```

Now `int/**/count;` produces two separate words. `// comment` at EOF succeeds, but `/* comment` at EOF returns an error spanning from the opening slash to the end. Test `/` by itself too: it takes the ordinary error-token path, without reading past the buffer. For future line-number diagnostics, count `\r\n` as one logical newline while keeping both bytes in your offset calculations.

## 6. See the boundary between the lexer and the parser

For your first parser, accept exactly one declaration using this rule:

```text
int_declaration = KW_INT IDENTIFIER SEMICOLON EOF
```

This is a **pattern over token kinds**, not characters. Here's a small example to try separately after your token dump works:

```c
/* On success, store the identifier token in *name. On failure, stop. */
static int parse_int_declaration(Lexer *lex, Token *name)
{
    if (lexer_next(lex).kind != TOK_KW_INT)
        return 0;

    Token ident = lexer_next(lex);
    if (ident.kind != TOK_IDENTIFIER)
        return 0;

    if (lexer_next(lex).kind != TOK_SEMICOLON)
        return 0;
    if (lexer_next(lex).kind != TOK_EOF)
        return 0;

    *name = ident;
    return 1;
}
```

Call it with a **fresh** lexer for `int count;`. It succeeds and tells you where the name is in the source. `int ;` fails because the parser needs a name; `int count` fails because it needs a semicolon. Both inputs can be tokenized without errors. That difference is why lexing and parsing are separate steps.

Your next exercise: instead of returning only the name, define a declaration node that records the name token and the base type `int`. Then add one optional `*` before the name. At that point you'll want a parser that holds **one lookahead token** so it can check whether the star is present without losing the identifier when it isn't. Don't tackle nested C declarators, macros, or real system headers until this small example works. Raw `#include` and `#if` need preprocessing; skipping them isn't the same as knowing which declarations are active.

Next: [Build a struct AST and compute field layouts](ast-layout-tutorial.md).
