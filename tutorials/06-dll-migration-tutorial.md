# 06 · Migrate opaque engine memory between two DLL builds

Use `memorymigration.slnx` to build **one host executable**. At runtime, `main.c` calls a C generator for your `.mem` language and then `cl /LD`, just like the engine's `devenv.c` calls `cl`. The generator produces **`memory.h` and layout metadata**, not a version-specific migration function. The host loads a v1 DLL and a v2 DLL in one run, asks each for its layout, and migrates the block by comparing those descriptions. You don't need a file watcher or a reload loop to test this.

## Prerequisites

- Finish the [`.mem` generator checkpoint](05-c-codegen-tutorial.md) and open an x64 Native Tools Command Prompt.
- For the integration steps below, use `memorymigration/` as the working directory. This lesson describes what you'll add, not what's already wired: the current `main.c` runs the scalar test and has a partial memory-index helper. Keep your scratch fixtures separate so you can rerun them.
- In `memorymigration/`, prepare `schemas/player-v1.mem`, `schemas/player-v2.mem`, and `engine.c` (the DLL source, separate from the host's `main.c`). Put your built generator at `build/generate_memory.exe` and create the `build/v1` and `build/v2` directories before invoking it.

| Owner | Responsibility |
| --- | --- |
| Host | Reserves and commits pages with `VirtualAlloc`, loads the DLLs, validates and diffs layout descriptions, copies bytes, and switches the active DLL and block. |
| Engine DLL | Defines its persistent C types, decides where its regions go, and uses its own `Memory` index to access them. Exposes layout metadata but **does not** implement v1-to-v2 migration. |
| Generator | Reads the `.mem` schema and emits `engine_api.h`, `memory.h`, and the C layout tables and exports. |

**Opaque doesn't mean the host can't copy the bytes.** It means the host never includes `memory.h`, casts the block to `Engine *`, or needs to know that a field represents health or speed. It operates on names, byte ranges, and storage kinds supplied by the DLL.

## 1. Give the host and DLL a small, stable API

Generate `engine_api.h` alongside the other outputs. For now the generator always emits the same ABI-1 declarations below; the `.mem` language does not need a version directive. **Changing fields must not change the format the host uses to read layout tables.** Put schema-specific declarations in each DLL's generated `memory.h`, not in the shared ABI.

Generate `engine_api.h` **before Visual Studio compiles the host** (run the generator manually first, then automate it if you want). The host cannot include a header that only gets generated when the executable runs. At runtime each schema generation can emit its own copy of `engine_api.h` into that DLL's output folder. Compile both DLLs and the host as x64 using matching ABI conventions.

Start with plain arrays. This is a flattened form of your layout AST: a root contains regions, each region contains fields. It's **compiled layout metadata**, not a C syntax tree. An AST that only says `i32 hp` isn't enough to move bytes; the description needs offsets, sizes, alignment, and a stable storage-kind ID.

```c
#ifndef ENGINE_API_H
#define ENGINE_API_H

#include <stdint.h>

#define ENGINE_API_VERSION 1u

enum { STORAGE_I32 = 1, STORAGE_U32 = 2 };

typedef struct {
    const char *name;
    uint32_t kind;         /* Shared enum: STORAGE_I32 or STORAGE_U32. */
    uint64_t offset;       /* Relative to the start of this region. */
    uint64_t size;
    uint64_t alignment;
} FieldLayout;

typedef struct {
    const char *name;
    uint32_t field_count;
    uint64_t offset;       /* Relative to the start of the memory block. */
    uint64_t size;
    uint64_t alignment;
    const FieldLayout *fields;
} RegionLayout;

typedef struct {
    uint64_t block_size;
    uint64_t block_alignment;
    uint32_t region_count;
    const RegionLayout *regions;
} EngineLayout;

typedef uint32_t (__cdecl *EngineApiVersionFn)(void);
typedef int (__cdecl *EngineDescribeLayoutFn)(const void *block, EngineLayout *out);

#endif
```

The DLL exports `engine_api_version` and `engine_describe_layout`. Section 3 shows the actual function bodies and how the host calls them. `block == NULL` asks for the planned layout of a new block. A non-null `block` asks for the layout of that live instance.

Also export `int engine_init(void *block, uint64_t size)`: ordinary initialization for any fresh block. It creates the engine's own index and sets default field values. It does not take an old block. Use `__declspec(dllexport)` on the DLL functions and matching function-pointer typedefs and calling conventions on the host side. The host calls them through `GetProcAddress`; it doesn't link an import library.

For this test, table pointers can point to immutable storage inside each DLL. **Keep both DLLs loaded while the host uses those pointers.** If you want to retain a description after unloading its DLL, deep-copy the tables and their name strings into host-owned storage first. Copying `EngineLayout` alone only copies its pointer.

## 2. Generate `memory.h` and let the compiler measure it

Keep old and new `.mem` fixtures so one run can build both versions. Each top-level struct is one region. The host matches regions by name and fields by name within their region. Position may change; for now a rename is a removal plus an addition.

For `schemas/player-v1.mem`:

```text
struct Player {
    i32 hp
}
```

And `schemas/player-v2.mem`:

```text
struct Player {
    i32 ammo
    i32 hp
}
```

Putting `ammo` first makes `hp` move. The syntax intentionally leaves out defaults: v2's ordinary `engine_init` sets `ammo` to `12`. For this fixture, compile the v2-only initialization statement behind a build define such as `PLAYER_HAS_AMMO` (pass `/DPLAYER_HAS_AMMO` only when compiling v2), so v1 never tries to access a member it doesn't have.

The [C code-generator tutorial](05-c-codegen-tutorial.md) shows how to tokenize the language, parse structs and fields, and emit the generated files. In brief:

1. Validate unique names, valid C spellings, and supported storage kinds (`i32 → int32_t`, `u32 → uint32_t`).
2. Emit `engine_api.h` using the fixed ABI template and `memory.h` in declaration order. Both DLLs can call their type `Player` because they compile separately.
3. Emit `memory_layout.inc` with the layout tables and exports below. MSVC evaluates the `offsetof`, `sizeof`, and `_Alignof` expressions in the Windows build. **The generator does not predict MSVC padding.**

Inside the DLL, use one placement routine to calculate region offsets for both `engine_describe_layout(NULL, ...)` and `engine_init`. For the simplest fixed-size version you can also generate a top-level C aggregate and use `offsetof` for the region offsets. Either way, the **DLL**, not the host, owns the engine index. If that index is stored in the block, build it fresh in `engine_init` and **exclude it from the fields the host copies**. Otherwise the host might overwrite the new index with the old region offsets.

## 3. Return a layout from inside the DLL

Start with a fixed layout, so you can prove the export works without building a dynamic index first. In the v2 DLL, suppose the generated `memory.h` contains these types:

```c
#include <stdint.h>

typedef struct { int32_t ammo; int32_t hp; } Player;
typedef struct { Player region_Player; } EngineMemory;
```

Now have the generator emit the following `memory_layout.inc` next to `memory.h`. [Tutorial 05, §5](05-c-codegen-tutorial.md#5-emit-the-tables-and-shared-api) shows how to write the two tables between the fixed prefix (includes and macros) and suffix (exports and `#undef`s). Include `"memory_layout.inc"` **once** in your DLL's `engine.c`; don't compile it into the host. The `FIELD` macro shows exactly what to emit per field. The `REGION` macro computes offsets in the opposite direction: relative to the block instead of the field's enclosing struct.

```c
#include "engine_api.h"
#include "memory.h"
#include <stddef.h>

#define COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))
#define FIELD(kind, T, member, C) \
    {#member, kind, offsetof(T, member), sizeof(C), _Alignof(C)}
#define REGION(name, member, T, table) \
    {name, COUNT(table), offsetof(EngineMemory, member), \
     sizeof(T), _Alignof(T), table}

static const FieldLayout fields_Player[] = {
    FIELD(STORAGE_I32, Player, ammo, int32_t),
    FIELD(STORAGE_I32, Player, hp, int32_t),
};
static const RegionLayout regions[] = {
    REGION("Player", region_Player, Player, fields_Player),
};

__declspec(dllexport) uint32_t __cdecl engine_api_version(void)
{
    return ENGINE_API_VERSION;
}

__declspec(dllexport) int __cdecl engine_describe_layout(
    const void *block, EngineLayout *out)
{
    (void)block; /* Every instance uses this DLL's fixed layout. */
    if (out == NULL)
        return 0;

    *out = (EngineLayout){
        .block_size = sizeof(EngineMemory),
        .block_alignment = _Alignof(EngineMemory),
        .region_count = COUNT(regions),
        .regions = regions,
    };
    return 1;
}

#undef REGION
#undef FIELD
#undef COUNT
```

There are two lifetimes here. The **host** owns the `EngineLayout` variable: it passes its address and the DLL fills it in with `*out = ...`. The **DLL** owns the static `regions` and `fields_*` arrays. They're not stack variables, so the pointers remain valid after the function returns, but **not after `FreeLibrary`**. To keep the description longer, deep-copy all the arrays and their name strings while the DLL is still loaded.

Here's the host side **after** `LoadLibraryW` succeeds. Since `out` is caller-owned, no allocation or cross-DLL `free` is involved:

```c
EngineApiVersionFn version = (EngineApiVersionFn)GetProcAddress(
    dll, "engine_api_version");
EngineDescribeLayoutFn describe = (EngineDescribeLayoutFn)GetProcAddress(
    dll, "engine_describe_layout");

EngineLayout layout = {0};
if (!version || version() != ENGINE_API_VERSION || !describe ||
    !describe(NULL, &layout)) {
    fprintf(stderr, "Could not read a compatible engine layout\n");
    FreeLibrary(dll);
    return EXIT_FAILURE;
}
/* On success, validate the description before using it. */
```

For a live v1 block, call the v1 DLL's `describe(old_block, &old_layout)`. Call the **v2** DLL's `describe(NULL, &new_layout)` for the planned layout. Both calls use the same signature, but their tables come from different compiled headers. If you later let the engine choose offsets at runtime, `offsetof(EngineMemory, member)` no longer describes those placements. Read actual region offsets from that DLL's own index for a live block and use the same placement routine as `engine_init` for a planned block. Return the actual offsets via caller-owned table storage or another explicitly documented lifetime; don't return pointers to local arrays.

## 4. Build and load two DLLs from `main.c`

Keep `memorymigration.vcxproj` as the host-only project. Run the built host from the project folder in an **x64 Native Tools Command Prompt** so its child `cl` processes inherit `PATH`, `INCLUDE`, and `LIB`. Set the Visual Studio debugger working directory to `$(ProjectDir)` if debugging there; the debuggee still needs the developer environment. Don't hard-code a Visual Studio toolset version.

Give your generator two arguments: the input schema and output directory. Generate `engine_api.h`, `memory.h`, and `memory_layout.inc` into each of `build/v1` and `build/v2`. Keep `engine.c` shared across builds, and don't leave copies of the generated files beside `engine.c` that would shadow the versions in the include directories. Once the build directories exist, a v1 command could look like this in `main.c`:

```c
if (system("build\\generate_memory.exe schemas\\player-v1.mem "
           "build\\v1") != 0)
    return EXIT_FAILURE;

if (system("cl /nologo /TC /std:c11 /LD /Ibuild\\v1 engine.c "
           "/Fobuild\\v1\\engine.obj /Febuild\\v1\\engine_v1.dll") != 0)
    return EXIT_FAILURE;
```

Repeat those calls in `memorymigration/main.c` for v2: use `schemas\\player-v2.mem` and `build\\v2` in the generator call, use `/Ibuild\\v2` and `/DPLAYER_HAS_AMMO` in the `cl` call, and write `build\\v2\\engine.obj` and `build\\v2\\engine_v2.dll`. Use `GetFullPathNameW` (checking for failure or truncation) and `LoadLibraryW` to load the exact DLL you built. Resolve and check every required export. **Never load an output from a failed build** just because a stale DLL remains on disk. Use separate filenames so both DLLs can stay mapped and the build doesn't try to overwrite a loaded DLL.

First get the generator to emit the v2 example and print its layout from the host. Then generate v1 and confirm the same executable sees a different layout through the same exports.

## 5. Reserve pages, then let each engine initialize them

Ask v1 for its fresh layout. The host checks the requested size and alignment, reserves and commits sufficient memory, then calls v1's `engine_init`. With this demo's ordinary C alignments, a `VirtualAlloc` base can satisfy the request; still check the actual base alignment and reject unsupported requests. You can use `MEM_RESERVE | MEM_COMMIT` in one call. If you split them later, remember that **reserved-only pages aren't readable or writable**.

For a test fixture, have v1 seed its own state with known values through a test-only export. The host doesn't write good-looking magic numbers into guessed offsets. When ready to migrate:

1. Ask the old DLL for the layout of the **live old block**.
2. Ask the new DLL for its planned layout, allocate a separate candidate block, and call its ordinary `engine_init`. Ask it for the candidate's actual layout. Verify the actual layout fits the block and matches the placement contract.
3. Keep the old block and DLL intact until the candidate is validated and fully populated.

The new engine knows how to initialize a block and how to use it. It never sees the old block and doesn't have a migration callback.

## 6. Diff the descriptions into a copy plan

Do the diff **before** writing any bytes. Match region names, then field names within each matched region. Use `strcmp` on the name strings; the pointer values will differ across DLLs. A linear search is fine for the prototype. The plan can be an array of `{old_absolute_offset, new_absolute_offset, byte_count}` records in a small host-owned arena or a fixed-capacity array for this test.

| Difference | Host action |
| --- | --- |
| Same region and field names, same storage kind and size | Add a copy operation. Offsets may differ. |
| New field or new region | Leave the candidate's initialized value untouched. |
| Removed field or region | No copy. |
| Same field name with a changed kind or size | Fail this migration. Don't silently cast or treat equal-sized types as interchangeable. |

Check both descriptions: unique, non-null names in each scope, supported kinds, sane table counts, nonzero valid alignments, aligned addresses, and no out-of-bounds or overlapping region/field ranges in this scalar-only model. Use checks like `offset <= limit && size <= limit - offset` before adding sizes or offsets. Calculate absolute offsets only after validating the field is within its region and the region is within the actual block. If a description is invalid or the plan doesn't fit, discard the candidate; the old engine is still usable.

After validation, applying a plan entry is just:

```c
memcpy(new_base + op.new_absolute_offset,
       old_base + op.old_absolute_offset,
       op.byte_count);
```

`old_base` and `new_base` are byte pointers to distinct committed blocks. Copy **fields**, not whole regions, so new defaults and new engine bookkeeping survive. The first version should reject raw pointers, references into the old block, and process resources such as file handles. Those need explicit semantics; a generic byte copy can't fix them.

## 7. Prove that the engine can use the result

Make this a one-shot test, not a live reload loop:

1. v1 seeds `hp = 37` through its own typed code. v2 initializes `ammo = 12` in the candidate.
2. The host prints the copy plan using the names and byte offsets from the metadata. It never uses `Player` as a C type or hard-codes the name `hp` in the diff.
3. v2 verifies through its normal access path that `hp = 37` and `ammo = 12`. A test-only verification export can return pass/fail. On the prototype MSVC target, the copy plan should move four bytes from old offset 0 to new offset 4.
4. Change v2 `hp` to `u32` and confirm that the host rejects the kind mismatch even though the size is unchanged. Check that v1 still reads 37 HP.
5. On success, switch the active pair **`(DLL, block)`** together. Do not dispatch an old export with the new block or unload a DLL while any function pointer or layout table from it is still in use. Free the old allocation with `VirtualFree(old_base, 0, MEM_RELEASE)` only when it is no longer referenced.

In a real reload you'll also need to stop engine calls and background work during the handoff. Leave that out of this test. If the one-shot version passes, you've proved the important boundary: **the engine interprets and indexes its state; the host only owns the pages and executes the diff.**

Next: [07 · Migrate arrays between memory blocks](07-array-migration-tutorial.md).
