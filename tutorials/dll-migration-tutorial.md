# Migrate opaque engine memory between two DLL builds

Use `memorymigration.slnx` to build **one host executable**. At runtime, `main.c` calls a TOML generator and `cl /LD`, just like the engine's `devenv.c` calls `cl`. The generator produces **`memory.h` and layout metadata**, not a version-specific migration function. The host loads a v1 DLL and a v2 DLL in one run, asks each for its layout, and migrates the block by comparing those descriptions. You don't need a file watcher or a reload loop to test this.

| Owner | Responsibility |
| --- | --- |
| Host | Reserves and commits pages with `VirtualAlloc`, loads the DLLs, validates and diffs layout descriptions, copies bytes, and switches the active DLL and block. |
| Engine DLL | Defines its persistent C types, decides where its regions go, and uses its own `Engine` index to access them. Exposes layout metadata but **does not** implement v1-to-v2 migration. |
| Generator | Reads the TOML schema and emits `engine_api.h`, `memory.h`, and the C layout tables and exports. |

**Opaque doesn't mean the host can't copy the bytes.** It means the host never includes `memory.h`, casts the block to `Engine *`, or needs to know that a field represents health or speed. It operates on IDs, byte ranges, and storage kinds supplied by the DLL.

## 1. Give the host and DLL a small, stable API

Generate `engine_api.h` from the TOML schema too. Give the schema an `abi_version` key. The generator uses that key to select the shared ABI declarations below. **Schemas with the same `abi_version` must generate ABI-compatible headers**, even if they have different regions and fields. Put those schema-specific declarations in each DLL's generated `memory.h`, not in the shared ABI.

Generate `engine_api.h` **before Visual Studio compiles the host** (run the generator manually first, then automate it if you want). The host cannot include a header that only gets generated when the executable runs. At runtime each schema generation can emit its own copy of `engine_api.h` into that DLL's output folder. Compile both DLLs and the host as x64 using matching ABI conventions.

Start with plain arrays. This is a flattened form of your layout AST: a root contains regions, each region contains fields. It's **compiled layout metadata**, not a C syntax tree. An AST that only says `int health;` isn't enough to move bytes; the description needs offsets, sizes, alignment, and a stable storage-kind ID.

```c
#ifndef ENGINE_API_H
#define ENGINE_API_H

#include <stdint.h>

#define ENGINE_API_VERSION 1u

enum { STORAGE_I32 = 1, STORAGE_F32 = 2, STORAGE_U32 = 3 };

typedef struct {
    uint32_t id;
    uint32_t kind;         /* Shared enum: e.g. STORAGE_I32 or STORAGE_F32. */
    uint64_t offset;       /* Relative to the start of this region. */
    uint64_t size;
    uint64_t alignment;
} FieldLayout;

typedef struct {
    uint32_t id;
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

For this test, table pointers can point to immutable storage inside each DLL. **Keep both DLLs loaded while the host uses those pointers.** If you want to retain a description after unloading its DLL, deep-copy the tables into host-owned storage first. Copying `EngineLayout` alone only copies its pointer.

## 2. Generate `memory.h` and let the compiler measure it

Keep old and new TOML fixtures so one run can build both versions. Start with scalar fields only, and assign stable IDs to both regions and fields. Same ID means same logical data; order and spelling may change. Don't recycle IDs for unrelated data.

For example, a v2 schema might look like this:

```toml
abi_version = 1

[[regions]]
id = 10
name = "World"

[[regions.fields]]
id = 2
name = "speed"
type = "f32"

[[regions.fields]]
id = 3
name = "max_health"
type = "i32"
default = 100

[[regions.fields]]
id = 1
name = "hp"
type = "i32"
```

In v1, define region `10` with `health` (ID `1`, `i32`) followed by `speed` (ID `2`, `f32`). Also add a `Counter` region with a single `u32` field in **both** versions, after `World`. This gives you a second region whose block offset can move when `World` grows.

Write the generator in C with `tomlc17`. The [C code-generator tutorial](c-codegen-tutorial.md) shows how to compile the parser, walk the TOML arrays, and emit the actual C declarations and table entries. The steps here describe what you'll generate:

1. Validate names, unique IDs, supported storage kinds, and defaults. Use a whitelist such as `i32 → int32_t`, `u32 → uint32_t`, and `f32 → float`.
2. Emit `engine_api.h` for the selected ABI and `memory.h` with declarations in TOML list order. Both DLLs can call their own type `World` because they compile separately.
3. Emit `memory_layout.inc` with the layout tables and exports below. MSVC evaluates the `offsetof`, `sizeof`, and `_Alignof` expressions. **The generator does not predict MSVC padding.**
4. Emit the default values the DLL uses in normal initialization. For v2, normal initialization sets `max_health` to `100`; the migration code won't need to know that `max_health` exists.

Inside the DLL, use one placement routine to calculate region offsets for both `engine_describe_layout(NULL, ...)` and `engine_init`. For the simplest fixed-size version you can also generate a top-level C aggregate and use `offsetof` for the region offsets. Either way, the **DLL**, not the host, owns the engine index. If that index is stored in the block, build it fresh in `engine_init` and **exclude it from the fields the host copies**. Otherwise the host might overwrite the new index with the old region offsets.

## 3. Return a layout from inside the DLL

Start with a fixed layout, so you can prove the export works without building a dynamic index first. In the v2 DLL, suppose the generated `memory.h` contains these types:

```c
#include <stdint.h>

typedef struct { float speed; int32_t max_health; int32_t hp; } World;
typedef struct { uint32_t value; } Counter;
typedef struct { World region_World; Counter region_Counter; } EngineMemory;
```

Now have the generator emit the following `memory_layout.inc` next to `memory.h`. Include `"memory_layout.inc"` **once** in your DLL's `engine.c`; don't compile it into the host. The `FIELD` macro shows exactly what to emit per field. The `REGION` macro computes offsets in the opposite direction: relative to the block instead of the field's enclosing struct.

```c
#include "engine_api.h"
#include "memory.h"
#include <stddef.h>

#define COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))
#define FIELD(id, kind, T, member, C) \
    {id, kind, offsetof(T, member), sizeof(C), _Alignof(C)}
#define REGION(id, member, T, table) \
    {id, COUNT(table), offsetof(EngineMemory, member), \
     sizeof(T), _Alignof(T), table}

static const FieldLayout world_fields[] = {
    FIELD(2, STORAGE_F32, World, speed, float),
    FIELD(3, STORAGE_I32, World, max_health, int32_t),
    FIELD(1, STORAGE_I32, World, hp, int32_t),
};
static const FieldLayout counter_fields[] = {
    FIELD(1, STORAGE_U32, Counter, value, uint32_t),
};
static const RegionLayout regions[] = {
    REGION(10, region_World, World, world_fields),
    REGION(20, region_Counter, Counter, counter_fields),
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

There are two lifetimes here. The **host** owns the `EngineLayout` variable: it passes its address and the DLL fills it in with `*out = ...`. The **DLL** owns the static `regions` and `*_fields` arrays. They're not stack variables, so the pointers remain valid after the function returns, but **not after `FreeLibrary`**. To keep the description longer, deep-copy all the arrays while the DLL is still loaded.

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
if (system("build\\generate_memory.exe schemas\\world-v1.toml "
           "build\\v1") != 0)
    return EXIT_FAILURE;

if (system("cl /nologo /TC /std:c11 /LD /Ibuild\\v1 engine.c "
           "/Fobuild\\v1\\engine.obj /Febuild\\v1\\engine_v1.dll") != 0)
    return EXIT_FAILURE;
```

Repeat for v2 with its own include directory and output DLL. Use `GetFullPathNameW` (checking for failure or truncation) and `LoadLibraryW` to load the exact DLL you built. Resolve and check every required export. **Never load an output from a failed build** just because a stale DLL remains on disk. Use separate filenames so both DLLs can stay mapped and the build doesn't try to overwrite a loaded DLL.

First get the generator to emit the v2 example and print its layout from the host. Then generate v1 and confirm the same executable sees a different layout through the same exports.

## 5. Reserve pages, then let each engine initialize them

Ask v1 for its fresh layout. The host checks the requested size and alignment, reserves and commits sufficient memory, then calls v1's `engine_init`. With this demo's ordinary C alignments, a `VirtualAlloc` base can satisfy the request; still check the actual base alignment and reject unsupported requests. You can use `MEM_RESERVE | MEM_COMMIT` in one call. If you split them later, remember that **reserved-only pages aren't readable or writable**.

For a test fixture, have v1 seed its own state with known values through a test-only export. The host doesn't write good-looking magic numbers into guessed offsets. When ready to migrate:

1. Ask the old DLL for the layout of the **live old block**.
2. Ask the new DLL for its planned layout, allocate a separate candidate block, and call its ordinary `engine_init`. Ask it for the candidate's actual layout. Verify the actual layout fits the block and matches the placement contract.
3. Keep the old block and DLL intact until the candidate is validated and fully populated.

The new engine knows how to initialize a block and how to use it. It never sees the old block and doesn't have a migration callback.

## 6. Diff the descriptions into a copy plan

Do the diff **before** writing any bytes. Use stable region IDs, then stable field IDs within each matched region. A linear search is fine for the prototype. The plan can be an array of `{old_absolute_offset, new_absolute_offset, byte_count}` records in a small host-owned arena or a fixed-capacity array for this test.

| Difference | Host action |
| --- | --- |
| Same region and field ID, same storage kind and size | Add a copy operation. Offsets may differ. |
| New field or new region | Leave the candidate's initialized value untouched. |
| Removed field or region | No copy. |
| Same ID with a changed kind or size | Fail this migration. Don't silently cast or treat equal-sized types as interchangeable. |

Check both descriptions: unique IDs, supported kinds, sane table counts, nonzero valid alignments, aligned addresses, and no out-of-bounds or overlapping region/field ranges in this scalar-only model. Use checks like `offset <= limit && size <= limit - offset` before adding sizes or offsets. Calculate absolute offsets only after validating the field is within its region and the region is within the actual block. If a description is invalid or the plan doesn't fit, discard the candidate; the old engine is still usable.

After validation, applying a plan entry is just:

```c
memcpy(new_base + op.new_absolute_offset,
       old_base + op.old_absolute_offset,
       op.byte_count);
```

`old_base` and `new_base` are byte pointers to distinct committed blocks. Copy **fields**, not whole regions, so new defaults and new engine bookkeeping survive. The first version should reject raw pointers, references into the old block, and process resources such as file handles. Those need explicit semantics; a generic byte copy can't fix them.

## 7. Prove that the engine can use the result

Make this a one-shot test, not a live reload loop:

1. v1 seeds `health = 37`, `speed = 2.5f`, and `Counter = 99` through its own typed code.
2. The host can print the copy plan using IDs and byte offsets, but doesn't use the C field names or types directly.
3. v2 verifies through its **normal engine index** that `hp = 37`, `speed = 2.5f`, `max_health = 100`, and `Counter = 99`. For the demo, a test-only verification export can return pass/fail. Make it check that the `Counter` region is also found at its new location.
4. Test a deliberate kind mismatch under the same ID, and confirm that the host rejects it and v1 still reads the same values.
5. On success, switch the active pair **`(DLL, block)`** together. Do not dispatch an old export with the new block or unload a DLL while any function pointer or layout table from it is still in use. Free the old allocation with `VirtualFree(old_base, 0, MEM_RELEASE)` only when it is no longer referenced.

In a real reload you'll also need to stop engine calls and background work during the handoff. Leave that out of this test. If the one-shot version passes, you've proved the important boundary: **the engine interprets and indexes its state; the host only owns the pages and executes the diff.**
