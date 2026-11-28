# 06 · Migrate opaque engine memory between two DLL builds

Complete [the copy planner](01a-copy-plan-tutorial.md), [live region placement](02a-live-layout-tutorial.md), and [generated tables](05-c-codegen-tutorial.md). This is a Windows x64 **Google Test integration** exercise: load one DLL first, then migrate between two. The test project is the host; there is no new console app or reload loop.

## 1. Give the host and DLL a small, stable API

Use the `engine_api.h` types from Step 01a with `ENGINE_API_VERSION` set to `1u` as in Step 05. The host and DLL must agree on the shared types and calling convention. Neither the host nor the planner includes a generated `memory.h`. That name belongs to a specific build's private declarations in its output folder.

The planned-layout export is the fixed aggregate checkpoint from Step 05. A separate live-layout export will combine the same generated field table with the instance's region placement. Keeping those two contracts distinct avoids treating an aggregate offset as the live location of a region.

## 2. Generate `memory.h` and let the compiler measure it

Use the Step 05 fixture files, commands, and generator in `codegen-lab`. `build/v1` and `build/v2` each contain the ABI header, private `memory.h`, and the generated `.inc`. The v1 schema contains `i32 hp`; v2 contains `i32 ammo` followed by `i32 hp`. Build x64 modules to match the x64 test runner.

## 3. Return a layout from inside the DLL

This is the complete example of `build/v2/memory_layout.inc` referenced by [Step 05](05-c-codegen-tutorial.md#5-emit-the-tables-and-shared-api). The generator owns the two tables between the macros and exports. [`offsetof`](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) (C99 §7.17) measures a member relative to its enclosing type. `_Alignof` is C11; these Windows fixture builds use `/std:c11`.

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

#ifndef ENGINE_LAYOUT_EXPORT
#define ENGINE_LAYOUT_EXPORT __declspec(dllexport)
#endif

ENGINE_LAYOUT_EXPORT uint32_t __cdecl engine_api_version(void)
{
    return ENGINE_API_VERSION;
}

ENGINE_LAYOUT_EXPORT int __cdecl engine_describe_layout(
    const void *block, EngineLayout *out)
{
    (void)block; /* This export describes only the fixed standalone fixture. */
    if (!out)
        return 0;
    *out = (EngineLayout){sizeof(EngineMemory), _Alignof(EngineMemory),
                          COUNT(regions), regions};
    return 1;
}

#undef ENGINE_LAYOUT_EXPORT
#undef REGION
#undef FIELD
#undef COUNT
```

## 4. Load just one DLL

In `codegen-lab/engine.c`, start with `#include "memory_layout.inc"`. Build v1 from the **x64 Native Tools Command Prompt**, working in `codegen-lab`. [`/LD`](https://learn.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library) builds a DLL; the `memory_layout.inc` include resolves through `/Ibuild\v1`. Don't place a generated copy next to `engine.c` where it would shadow this version.

```cmd
cl /nologo /W4 /TC /std:c11 /LD /Ibuild\v1 engine.c /Fobuild\v1\engine.obj /Febuild\v1\engine_v1.dll
```

For local tests, set the fixture directory in a `.runsettings` file next to your test solution. Use the **absolute** path of your `codegen-lab/build` directory, then select the file under **Test → Configure Run Settings**. [Visual Studio run settings](https://learn.microsoft.com/en-us/visualstudio/test/configure-unit-tests-by-using-a-dot-runsettings-file) documents test environment variables. Alternatively, set the same variable in the shell before running the Google Test executable with `--gtest_filter=DllMigration.*`.

```xml
<RunSettings>
  <RunConfiguration>
    <EnvironmentVariables>
      <ENGINE_FIXTURE_DIR>C:\labs\codegen-lab\build</ENGINE_FIXTURE_DIR>
    </EnvironmentVariables>
  </RunConfiguration>
</RunSettings>
```

Create `memorymigration/memory_test/dll_test.cpp` in `memory_tests`. Keep `pch.h` first. [`LoadLibraryW`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw) takes the full fixture path; [`GetProcAddress`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress) resolves an exported name. Run assertions inside a helper so the test always reaches `FreeLibrary` for a module it loaded.

```cpp
#include "pch.h"
#include "memory.h"
#include <windows.h>
#include <wchar.h>

static HMODULE LoadFixture(const wchar_t *relative)
{
    wchar_t dir[1024], path[1200];
    DWORD n = GetEnvironmentVariableW(L"ENGINE_FIXTURE_DIR", dir, 1024);
    if (!n || n >= 1024)
        return NULL;
    int written = swprintf(path, 1200, L"%ls\\%ls", dir, relative);
    if (written < 0 || written >= 1200)
        return NULL;
    return LoadLibraryW(path);
}

typedef uint32_t (__cdecl *VersionFn)(void);
typedef int (__cdecl *LayoutFn)(const void *, EngineLayout *);

static void CheckOneDll(HMODULE dll)
{
    VersionFn version = (VersionFn)GetProcAddress(dll, "engine_api_version");
    LayoutFn describe = (LayoutFn)GetProcAddress(dll, "engine_describe_layout");
    ASSERT_TRUE(version != NULL && describe != NULL);
    ASSERT_EQ(ENGINE_API_VERSION, version());
    EngineLayout layout = {0};
    ASSERT_TRUE(describe(NULL, &layout));
    ASSERT_EQ(1u, layout.region_count);
    ASSERT_EQ(1u, layout.regions[0].field_count);
    EXPECT_STREQ("Player", layout.regions[0].name);
    EXPECT_STREQ("hp", layout.regions[0].fields[0].name);
    EXPECT_EQ(STORAGE_I32, layout.regions[0].fields[0].kind);
}

TEST(DllMigration, LoadsOneLayout)
{
    HMODULE dll = LoadFixture(L"v1\\engine_v1.dll");
    ASSERT_TRUE(dll != NULL);
    CheckOneDll(dll);
    FreeLibrary(dll);
}
```

Run `DllMigration.LoadsOneLayout`. Expect one pass. If loading fails, first check the absolute fixture directory, the DLL's location, and x64 on both sides. If an export fails, inspect `dumpbin /exports build\v1\engine_v1.dll`. Don't use a layout table after unloading: the output struct belongs to the test, but its table and string pointers point into the DLL.

## 5. Give each DLL its own live block

Append the following to `codegen-lab/engine.c` below the generated include. This is the Step 02a pattern with a one-region private index at byte zero. A larger `engine` array in v2 moves the region; the generated `fields_Player` table still measures fields relative to `Player`. The host will ask for the required size, not cast the block. `engine_init` creates ordinary defaults for this fixture; the old build uses a known HP value so the test can observe migration.

```c
#ifdef PLAYER_HAS_AMMO
#define ENGINE_WORDS 2
#else
#define ENGINE_WORDS 1
#endif

typedef struct { size_t player_offset; } FixtureIndex;
typedef struct {
    FixtureIndex index;
    int32_t engine[ENGINE_WORDS];
    Player player;
} FixtureState;

__declspec(dllexport) uint64_t __cdecl engine_state_size(void)
{
    return sizeof(FixtureState);
}

__declspec(dllexport) int __cdecl engine_init(void *block, uint64_t size)
{
    if (!block || size < sizeof(FixtureState))
        return 0;
    FixtureState *state = block;
    *state = (FixtureState){0};
    state->index.player_offset = offsetof(FixtureState, player);
#ifdef PLAYER_HAS_AMMO
    state->player.ammo = 12;
#else
    state->player.hp = 37;
#endif
    return 1;
}

__declspec(dllexport) int __cdecl engine_describe_live(
    const void *block, uint64_t size, RegionLayout *region, EngineLayout *out)
{
    if (!block || size < sizeof(FixtureState))
        return 0;
    const FixtureState *state = block;
    *region = regions[0];
    /* The generated field table stays; replace the aggregate's placement. */
    region->offset = state->index.player_offset;
    *out = (EngineLayout){sizeof(FixtureState), _Alignof(FixtureState), 1, region};
    return 1;
}

__declspec(dllexport) void __cdecl engine_read_values(
    const void *block, int32_t *hp, int32_t *ammo)
{
    const unsigned char *bytes = block;
    const FixtureIndex *index = block;
    const Player *player = (const Player *)(bytes + index->player_offset);
    *hp = (int32_t)player->hp;
#ifdef PLAYER_HAS_AMMO
    *ammo = player->ammo;
#else
    *ammo = 0;
#endif
}
```

Create `schemas/player-bad.mem` in `codegen-lab`: use v2's schema with `u32 hp` instead of `i32 hp`. In the same prompt, regenerate that fixture and build all three DLLs. Stop on the first failed command; don't run against stale artifacts. Close active fixture users before rebuilding DLLs.

```cmd
mkdir build\bad
build\generate_memory.exe schemas\player-bad.mem build\bad
cl /nologo /W4 /TC /std:c11 /LD /Ibuild\v1 engine.c /Fobuild\v1\engine.obj /Febuild\v1\engine_v1.dll
cl /nologo /W4 /TC /std:c11 /LD /DPLAYER_HAS_AMMO /Ibuild\v2 engine.c /Fobuild\v2\engine.obj /Febuild\v2\engine_v2.dll
cl /nologo /W4 /TC /std:c11 /LD /DPLAYER_HAS_AMMO /Ibuild\bad engine.c /Fobuild\bad\engine.obj /Febuild\bad\engine_bad.dll
```

## 6. Diff the descriptions into a copy plan

Append these helpers and cases to `dll_test.cpp`. The host supplies separate committed pages using [`VirtualAlloc`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc); this fixture uses only ordinary x64 struct alignment. The test never includes `FixtureState` or `FixtureIndex`. On a failed plan it performs no copy, and the old engine's read export still works. Validate sizes against the actual allocations before applying even a successful plan.

```cpp
typedef uint64_t (__cdecl *SizeFn)(void);
typedef int (__cdecl *InitFn)(void *, uint64_t);
typedef int (__cdecl *LiveFn)(const void *, uint64_t, RegionLayout *, EngineLayout *);
typedef void (__cdecl *ReadFn)(const void *, int32_t *, int32_t *);

typedef struct {
    HMODULE dll;
    MemoryBlock block;
    LiveFn describe;
    ReadFn read;
} Instance;

static bool OpenInstance(const wchar_t *path, Instance *out)
{
    out->dll = LoadFixture(path);
    if (!out->dll)
        return false;
    VersionFn version = (VersionFn)GetProcAddress(out->dll, "engine_api_version");
    if (!version || version() != ENGINE_API_VERSION)
        return false;
    SizeFn size = (SizeFn)GetProcAddress(out->dll, "engine_state_size");
    InitFn init = (InitFn)GetProcAddress(out->dll, "engine_init");
    out->describe = (LiveFn)GetProcAddress(out->dll, "engine_describe_live");
    out->read = (ReadFn)GetProcAddress(out->dll, "engine_read_values");
    if (!size || !init || !out->describe || !out->read)
        return false;
    uint64_t bytes = size();
    if (!bytes || bytes > SIZE_MAX)
        return false;
    out->block.capacity = (size_t)bytes;
    out->block.base = (unsigned char *)VirtualAlloc(NULL, out->block.capacity,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return out->block.base && init(out->block.base, out->block.capacity);
}

static void CloseInstance(Instance *instance)
{
    if (instance->block.base)
        VirtualFree(instance->block.base, 0, MEM_RELEASE);
    if (instance->dll)
        FreeLibrary(instance->dll);
}

static void CheckMigration(const Instance *old, const Instance *candidate,
                           bool should_copy)
{
    RegionLayout old_region = {0}, new_region = {0};
    EngineLayout a = {0}, b = {0};
    ASSERT_TRUE(old->describe(old->block.base, old->block.capacity, &old_region, &a));
    ASSERT_TRUE(candidate->describe(candidate->block.base, candidate->block.capacity,
                                    &new_region, &b));
    ASSERT_LE(a.block_size, old->block.capacity);
    ASSERT_LE(b.block_size, candidate->block.capacity);
    ASSERT_NE(old_region.offset, new_region.offset);
    CopyOp op = {0};
    size_t count = 0;
    bool planned = build_copy_plan(&a, &b, &op, 1, &count);
    ASSERT_EQ(should_copy, planned);
    if (planned) {
        ASSERT_EQ(1u, count);
        ASSERT_TRUE(copy_field(&old->block, &candidate->block, op));
    } else {
        EXPECT_EQ(0u, count);
    }
    int32_t hp = 0, ammo = 0;
    candidate->read(candidate->block.base, &hp, &ammo);
    EXPECT_EQ(should_copy ? 37 : 0, hp);
    EXPECT_EQ(12, ammo);
    old->read(old->block.base, &hp, &ammo);
    EXPECT_EQ(37, hp);
}

static void RunMigration(const wchar_t *path, bool should_copy)
{
    Instance old = {0}, candidate = {0};
    bool opened_old = OpenInstance(L"v1\\engine_v1.dll", &old);
    bool opened_candidate = OpenInstance(path, &candidate);
    EXPECT_TRUE(opened_old);
    EXPECT_TRUE(opened_candidate);
    if (opened_old && opened_candidate)
        CheckMigration(&old, &candidate, should_copy);
    /* CheckMigration has returned, so none of its borrowed tables are in use. */
    CloseInstance(&candidate);
    CloseInstance(&old);
}

TEST(DllMigration, MigratesMatchingHp)
{
    RunMigration(L"v2\\engine_v2.dll", true);
}

TEST(DllMigration, RejectsCandidateAndKeepsOldState)
{
    RunMigration(L"bad\\engine_bad.dll", false);
}
```

Build `memory_tests` and run `DllMigration` in Test Explorer. Expect three passes. If offsets don't differ, check `/DPLAYER_HAS_AMMO` and the live export: it must replace the generated aggregate offset. If the bad candidate is accepted, compare the two `hp` kinds and check that you loaded `engine_bad.dll`.

This proves an attempt can be rejected without losing the old state. A real host would discard only the candidate on failure, or replace its active `(DLL, block)` pair together on success. This test releases both instances at the end; it doesn't implement dispatch or a reload loop.

Next: [07 · Array migration](07-array-migration-tutorial.md).
