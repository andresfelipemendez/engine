# Tutorial roadmap

Follow the numbered path for the engine's one-shot memory migration prototype. Use a scratch program for Step 01 and the existing `memorymigration` project for Step 02. Neither step needs DLLs or generated headers.

| Step | Tutorial | Move on when… |
| --- | --- | --- |
| 01 | [First memory migration test](01-first-memory-migration-test.md) | `hp` survives a field reorder and `ammo` keeps its new default. Try matching two field descriptors instead of hard-coding the copy operation before building the host diff. |
| 02 | [Memory index](02-memory-index-tutorial.md) | One block places `Engine`, `Game`, and `Editor` in fixed order, and a direct lookup finds `Game` through the index at byte zero. Use the existing `memorymigration/memory.h` and `memorymigration/main.c`; defer strings and sub-arenas until you need them. |
| 03 | [Arena allocation](03-arena-tutorial.md) | You can allocate aligned ranges from one block and reject a request that doesn't fit. |
| 04 | [Lexer](04-lexer-tutorial.md) | You can tokenize input reliably. Reuse the lexer for `.mem`; you don't need to parse full C headers. |
| 05 | [`.mem` code generator](05-c-codegen-tutorial.md) | Two `Player` schema versions each emit `engine_api.h`, `memory.h`, and `memory_layout.inc`. |
| 06 | [One-shot DLL migration](06-dll-migration-tutorial.md) | One host migrates scalar state using the two DLLs' exported layouts, preserves defaults, and rejects incompatible field kinds. No live reload loop yet. |
| 07 | [Array migration](07-array-migration-tutorial.md) | Inline arrays migrate first; then arena-backed arrays copy their contents and get new references. Revisit sub-arenas in Step 02 before the arena-backed case. |

## Optional side lesson

- [08 · C-header AST and layout](08-ast-layout-tutorial.md): try this any time after Step 04. The `.mem` path goes straight to Step 05 and uses the C compiler's `sizeof` and `offsetof` rather than reproducing its struct layout rules.
