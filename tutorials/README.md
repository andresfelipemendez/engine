# Tutorial roadmap

Build toward one observable result: **a host migrates live state between two engine builds without knowing their private C structs**. The host owns the backing memory and the copy plan. Each engine build owns its layout, initialization, and normal access to its state.

Start at 00a. Verify library behavior with C-style Google Test cases in Test Explorer. Use unit tests for allocation, parsing, layout descriptions, and copy planning; use Windows integration tests only when you actually load DLLs. The generator may have a command-line entry point as a build tool, but don't create a console app to verify an exercise.

**Planned** means the checkpoint is specified here but doesn't yet have a runnable lesson. Several linked lessons still use console demos; those are marked **revise** and are not the intended verification workflow.

| Step | Lesson | Move on when… |
| --- | --- | --- |
| 00a | [Set up the memory library and Google Test](00a-memory-library-tutorial.md) | `memory.lib` builds as C and the `memory_tests` project links it. |
| 00b | [Run the initialization unit tests](00b-google-test-tutorial.md) | `MemoryInit` checks the initialized cursors and rejects both a null block and an undersized block. |
| 01 | [Execute a single copy operation](01-first-memory-migration-test.md) | A hand-written `CopyOp` moves `hp` after a field reorder, preserves `ammo = 12`, and rejects an out-of-bounds write. |
| 01a | [Build a copy plan from hand-written layouts](01a-copy-plan-tutorial.md) | A pure C planner sees only two sets of descriptors, not `PlayerV1` or `PlayerV2`. First assert that it emits exactly one operation for `hp`; then apply that plan and check `hp = 37` and `ammo = 12`. Separate tests reject a same-size kind mismatch and an out-of-range descriptor before copying. No generator, DLLs, or platform allocation. |
| 02 | [Memory index at byte zero](02-memory-index-tutorial.md) | The engine puts `Engine`, `Game`, and `Editor` in fixed slots, finds `Game` in one initialized block, and rejects an invalid slot. |
| 02a | [Describe a live block without exposing its index](02a-live-layout-tutorial.md) | Grow a region before `Game` so `Game` moves while `hp` stays at the same offset *within* it. The engine pairs its actual region offsets with field metadata. The host-side test uses only the description and verifies `region offset + field offset` by applying the Step 01a planner. It never reads `Memory` directly. |
| 03 | [Arena allocation](03-arena-tutorial.md) — **revise** | Google Test checks alignment, non-overlap, and that a failed allocation leaves the cursor unchanged. |
| 04 | [Lexer](04-lexer-tutorial.md) — **revise** | Google Test checks token spans, EOF, and progress on bad input. Reuse the scanner for `.mem`; you don't need a full C header parser. |
| 05a | [Parse `.mem` from a string, then a file](05-c-codegen-tutorial.md#3-parse-one-struct-in-generate_memoryc) | The parser accepts the string fixture and rejects an unknown kind in Google Test. The generator reads the matching file fixture and rejects malformed input before emission. |
| 05b | [Generate and consume layout tables](05-c-codegen-tutorial.md#4-emit-memoryh-one-line-at-a-time) | Generate `engine_api.h`, `memory.h`, and `memory_layout.inc` for both schemas. Compile the outputs and assert names, kinds, sizes, and the changed `hp` offset. Replace Step 01a's hand-written field tables with generated ones and rerun the planner tests. Keep live region placement from Step 02a; don't use aggregate offsets after adding the index. |
| 06a | [Load one DLL and check the ABI](06-dll-migration-tutorial.md#4-load-just-one-dll) | A Windows Google Test integration case loads one built fixture, checks the API version, resolves the layout export, and reads a known descriptor. Keep the DLL loaded while borrowing its tables and name strings. No migration yet. |
| 06b | [Migrate between two DLLs once](06-dll-migration-tutorial.md#5-give-each-dll-its-own-live-block) | Initialize separate old and candidate blocks, ask each DLL to describe its own live state, and reuse the tested planner. Verify through v2's normal access path that `hp = 37` and `ammo = 12`. A separate rejection case discards the candidate and proves v1 still reads `hp = 37`. Only switch the active `(DLL, block)` pair after success. No watcher or reload loop yet. |
| 07a | [Migrate an inline array](07-array-migration-tutorial.md) | The existing Google Test suite proves that live values copy and the extra destination slot keeps its default. |
| 07b | **Planned: Migrate an arena-backed array** ([design sketch](07-array-migration-tutorial.md#2-arena-backed-arrays-move-the-contents-and-rebuild-the-reference)) | A runnable unit test places the array at different offsets in the two blocks, copies only live elements, and verifies the values through the new reference. A separate test rejects `count > capacity` without modifying the old block. Complete this before adding array syntax to `.mem`. |

## Optional side lesson

- [08 · C-header AST and layout](08-ast-layout-tutorial.md) — **revise**: try after Step 04. Compare the layout pass with compiler-measured `sizeof` and `offsetof` in tests. The main `.mem` path asks the compiler to measure layout directly; this lesson is for learning how alignment and padding produce those results.
