// Build script (nob.h style). Bootstrap once, no flags needed:
//
//     gcc -o build build.c     (Windows / MinGW)
//     cc  -o build build.c     (Linux / WSL / macOS)
//
// then just run ./build -- it recompiles itself whenever build.c or nob.h change.
//
//     ./build          native target (on Windows: Windows + WSL)
//     ./build win      Windows only
//     ./build wsl      WSL only (from Windows)
//
// Products (all in out/):
//     engine[.exe]     host: window, GPU, memory, loads + hot reloads the game.
//                      Relinked only when host.c changes (it is locked while running).
//     libs.dll/.so/.dylib  sokol + cgltf + imgui/dbgui. Loaded once by the host, so GPU
//                      state survives game reloads. Cached.
//     game.dll/.so/.dylib  unity build of the game (src/game.c). Rebuilt every run;
//                      the running engine picks it up automatically.
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#define SRC "src/"
#define TP  "third_party/"

#if defined(_WIN32)
    #define OBJ      "out/win/"
    #define LIB_EXT  "dll"
    #define EXE      "out/engine.exe"
    #define BACKEND  "-DSOKOL_D3D11"
#elif defined(__APPLE__)
    #define OBJ      "out/macos/"
    #define LIB_EXT  "dylib"
    #define EXE      "out/engine"
    #define BACKEND  "-DSOKOL_METAL"
#else
    #define OBJ      "out/linux/"
    #define LIB_EXT  "so"
    #define EXE      "out/engine"
    #define BACKEND  "-DSOKOL_GLCORE"
#endif
#define LIBS_LIB "out/libs." LIB_EXT
#define GAME_LIB "out/game." LIB_EXT

static const char *imgui_units[] = { "imgui", "imgui_draw", "imgui_tables", "imgui_widgets", "imgui_demo" };

static void common_flags(Cmd *cmd) {
    cmd_append(cmd, "-O1", BACKEND, "-DUSE_DBG_UI");
    cmd_append(cmd, "-I" SRC, "-I" TP "sokol", "-I" TP "imgui", "-I" TP "cgltf");
#if !defined(_WIN32)
    cmd_append(cmd, "-fPIC");
#endif
}

static void c_compile(Cmd *cmd, const char *src, const char *obj) {
    cmd_append(cmd, "gcc", "-std=gnu99");
    common_flags(cmd);
    cmd_append(cmd, "-c", src, "-o", obj);
}

static void cpp_compile(Cmd *cmd, const char *src, const char *obj) {
    cmd_append(cmd, "g++", "-std=c++17");
    common_flags(cmd);
    cmd_append(cmd, "-c", src, "-o", obj);
}

// links against the libs shared library (import directly from the .dll on
// MinGW; on Linux by soname with an $ORIGIN rpath so out/ is searched;
// on macOS via its @rpath install name)
static void link_libs(Cmd *cmd) {
#if defined(_WIN32)
    cmd_append(cmd, LIBS_LIB);
#elif defined(__APPLE__)
    cmd_append(cmd, LIBS_LIB, "-Wl,-rpath,@loader_path");
#else
    cmd_append(cmd, "-Lout", "-l:libs.so", "-Wl,-rpath,$ORIGIN");
#endif
}

#if defined(_WIN32)
    #define SHDC "tools/sokol-shdc.exe"
#elif defined(__APPLE__)
    #define SHDC "tools/macos/sokol-shdc"
    #if defined(__aarch64__)
        #define SHDC_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin/osx_arm64/sokol-shdc"
    #else
        #define SHDC_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin/osx/sokol-shdc"
    #endif
#else
    #define SHDC "tools/linux/sokol-shdc"
    #define SHDC_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin/linux/sokol-shdc"
#endif

// the running game calls sokol-shdc for shader hot reload, so make sure the
// native binary exists (Linux/macOS: downloaded on first build)
static bool ensure_shdc(void) {
    if (file_exists(SHDC)) return true;
#if defined(_WIN32)
    nob_log(WARNING, "%s not found: shader hot reload disabled", SHDC);
    return true;
#else
    Cmd cmd = {0};
    if (!mkdir_if_not_exists(temp_dir_name(SHDC))) return false;
    cmd_append(&cmd, "curl", "-fsSL", "-o", SHDC, SHDC_URL);
    if (!cmd_run(&cmd)) return false;
    cmd_append(&cmd, "chmod", "+x", SHDC);
    return cmd_run(&cmd);
#endif
}

// the generated header is portable; regenerate it when the .glsl changed
static bool build_shaders(void) {
    const char *glsl = SRC "shaders/model.glsl";
    const char *hdr  = SRC "shaders/model.glsl.h";
    if (!file_exists(SHDC)) {
        nob_log(WARNING, "%s not found, using existing %s", SHDC, hdr);
        return true;
    }
    int rebuild = needs_rebuild1(hdr, glsl);
    if (rebuild < 0) return false;
    if (rebuild == 0) return true;
    Cmd cmd = {0};
    cmd_append(&cmd, SHDC, "-i", glsl, "-o", hdr, "-l", "hlsl5:glsl430:metal_macos");
    return cmd_run(&cmd);
}

static bool build_libs(void) {
    Cmd cmd = {0};
    Procs procs = {0};
    File_Paths objs = {0};

    const char *libs_deps[] = {
        SRC "libs.c",
        TP "sokol/sokol_app.h", TP "sokol/sokol_gfx.h", TP "sokol/sokol_glue.h",
        TP "sokol/sokol_log.h", TP "sokol/sokol_debugtext.h", TP "cgltf/cgltf.h",
    };
    da_append(&objs, OBJ "libs.o");
    if (needs_rebuild(OBJ "libs.o", libs_deps, ARRAY_LEN(libs_deps)) != 0) {
#if defined(__APPLE__)
        // sokol's Metal backend is Objective-C (-x must precede the source)
        cmd_append(&cmd, "gcc", "-std=gnu99", "-x", "objective-c");
        common_flags(&cmd);
        cmd_append(&cmd, "-c", SRC "libs.c", "-o", OBJ "libs.o");
#else
        c_compile(&cmd, SRC "libs.c", OBJ "libs.o");
#endif
        if (!cmd_run(&cmd, .async = &procs)) return false;
    }
    for (size_t i = 0; i < ARRAY_LEN(imgui_units); i++) {
        const char *src = temp_sprintf(TP "imgui/%s.cpp", imgui_units[i]);
        const char *obj = temp_sprintf(OBJ "%s.o", imgui_units[i]);
        da_append(&objs, obj);
        if (needs_rebuild1(obj, src) == 0) continue;
        cpp_compile(&cmd, src, obj);
        if (!cmd_run(&cmd, .async = &procs)) return false;
    }
    const char *dbgui_deps[] = { SRC "dbgui.cc", SRC "dbgui.h" };
    da_append(&objs, OBJ "dbgui.o");
    if (needs_rebuild(OBJ "dbgui.o", dbgui_deps, ARRAY_LEN(dbgui_deps)) != 0) {
        cpp_compile(&cmd, SRC "dbgui.cc", OBJ "dbgui.o");
        if (!cmd_run(&cmd, .async = &procs)) return false;
    }
    if (!procs_flush(&procs)) return false;

    // relink only when an object changed: the running engine locks the .dll
    if (needs_rebuild(LIBS_LIB, objs.items, objs.count) == 0) return true;
    cmd_append(&cmd, "g++", "-shared", "-o", LIBS_LIB);
    da_append_many(&cmd, objs.items, objs.count);
#if defined(_WIN32)
    // cgltf/dbgui have no dllexport annotations; sokol's dllexport would
    // otherwise switch off MinGW's automatic export of everything else
    cmd_append(&cmd, "-Wl,--export-all-symbols");
    cmd_append(&cmd, "-lkernel32", "-luser32", "-lgdi32", "-lshell32", "-ld3d11", "-ldxgi");
#elif defined(__APPLE__)
    cmd_append(&cmd, "-install_name", "@rpath/libs.dylib");
    cmd_append(&cmd, "-framework", "Cocoa", "-framework", "QuartzCore", "-framework", "Metal", "-framework", "MetalKit");
#else
    cmd_append(&cmd, "-Wl,-soname,libs.so");
    cmd_append(&cmd, "-lX11", "-lXi", "-lXcursor", "-lGL", "-ldl", "-lpthread", "-lm");
#endif
    return cmd_run(&cmd);
}

static bool build_host(void) {
    Cmd cmd = {0};
    const char *host_deps[] = { SRC "host.c", SRC "game_api.h", SRC "dbgui.h" };
    if (needs_rebuild(OBJ "host.o", host_deps, ARRAY_LEN(host_deps)) != 0) {
        c_compile(&cmd, SRC "host.c", OBJ "host.o");
        cmd_append(&cmd, "-Wall", "-Wextra", "-DSOKOL_DLL");
        if (!cmd_run(&cmd)) return false;
    }
    const char *link_deps[] = { OBJ "host.o", LIBS_LIB };
    if (needs_rebuild(EXE, link_deps, ARRAY_LEN(link_deps)) == 0) return true;
    cmd_append(&cmd, "gcc", "-o", EXE, OBJ "host.o");
    link_libs(&cmd);
#if !defined(_WIN32)
    cmd_append(&cmd, "-ldl", "-lm");
#endif
    return cmd_run(&cmd);
}

// appends the space-separated words of env var GAME_CFLAGS (used by tests)
static void game_extra_flags(Cmd *cmd) {
    const char *env = getenv("GAME_CFLAGS");
    if (!env) return;
    String_View sv = sv_from_cstr(env);
    while (sv.count > 0) {
        sv = sv_trim_left(sv);
        String_View word = sv_chop_by_delim(&sv, ' ');
        if (word.count > 0) cmd_append(cmd, temp_sv_to_cstr(word));
    }
}

// FNV-1a over the non-whitespace text of the `typedef struct { ... } game_state_t;`
// block in src/game.c. The host re-inits game state when this changes, which
// catches reordered or retyped fields that keep the same sizeof.
// (Changes inside nested types like gpu_model_t are caught only via sizeof.)
static bool state_layout_hash(unsigned long long *out) {
    String_Builder sb = {0};
    if (!read_entire_file(SRC "game.c", &sb)) return false;
    sb_append_null(&sb);
    const char *end = strstr(sb.items, "} game_state_t;");
    const char *begin = NULL;
    for (const char *p = sb.items; end && (p = strstr(p, "typedef struct")) && (p < end); p++) begin = p;
    if (!end || !begin) {
        nob_log(ERROR, "src/game.c: cannot find the game_state_t definition");
        sb_free(sb);
        return false;
    }
    unsigned long long h = 1469598103934665603ull;
    for (const char *p = begin; p < end; p++) {
        if ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) continue;
        h = (h ^ (unsigned char)*p) * 1099511628211ull;
    }
    sb_free(sb);
    *out = h;
    return true;
}

static bool build_game(void) {
    Cmd cmd = {0};
    unsigned long long layout;
    if (!state_layout_hash(&layout)) return false;
    c_compile(&cmd, SRC "game.c", OBJ "game.o");
    cmd_append(&cmd, "-Wall", "-Wextra", "-DSOKOL_DLL");
    cmd_append(&cmd, temp_sprintf("-DGAME_STATE_HASH=0x%016llxull", layout));
    game_extra_flags(&cmd);
    if (!cmd_run(&cmd)) return false;
    // link to a temp name, then rename: the running engine polls GAME_LIB and
    // must never see a half-written file
    const char *tmp = "out/game.tmp." LIB_EXT;
    cmd_append(&cmd, "gcc", "-shared", "-o", tmp, OBJ "game.o");
    link_libs(&cmd);
#if !defined(_WIN32)
    cmd_append(&cmd, "-lm");
#endif
    if (!cmd_run(&cmd)) return false;
    return nob_rename(tmp, GAME_LIB);
}

static bool build_native(void) {
    if (!mkdir_if_not_exists("out")) return false;
    if (!mkdir_if_not_exists(OBJ)) return false;
    if (!ensure_shdc()) return false;
    if (!build_shaders()) return false;
    if (!build_libs()) return false;
    if (!build_host()) return false;
    if (!build_game()) return false;
    nob_log(INFO, "built %s, %s, %s", EXE, LIBS_LIB, GAME_LIB);
    return true;
}

#if defined(_WIN32)
    #define TEST_EXE "out/hot_reload_test.exe"
#else
    #define TEST_EXE "out/hot_reload_test"
#endif

// builds and runs tests/hot_reload_test.c against the native build
static bool run_tests(const char *filter) {
    Cmd cmd = {0};
    const char *deps[] = { "tests/hot_reload_test.c", TP "nob.h" };
    if (needs_rebuild(TEST_EXE, deps, ARRAY_LEN(deps)) != 0) {
        cmd_append(&cmd, "gcc", "-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-result", "-O1", "-o", TEST_EXE, "tests/hot_reload_test.c");
        if (!cmd_run(&cmd)) return false;
    }
    cmd_append(&cmd, TEST_EXE);
    if (filter) cmd_append(&cmd, filter);
    return cmd_run(&cmd);
}

#if defined(_WIN32)
    #define IMPORT_GLB_EXE "out/import_glb.exe"
#else
    #define IMPORT_GLB_EXE "out/import_glb"
#endif

// command line tools in tools/*.c
static bool build_tools(void) {
    if (!mkdir_if_not_exists("out")) return false;
    const char *deps[] = { "tools/import_glb.c", TP "nob.h", TP "cgltf/cgltf.h" };
    if (needs_rebuild(IMPORT_GLB_EXE, deps, ARRAY_LEN(deps)) == 0) return true;
    Cmd cmd = {0};
    cmd_append(&cmd, "gcc", "-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-result", "-O1",
               "-o", IMPORT_GLB_EXE, "tools/import_glb.c");
    return cmd_run(&cmd);
}

#if defined(_WIN32)
// bootstrap + run the Linux build of this same script inside WSL
static bool build_wsl(const char *args) {
    Cmd cmd = {0};
    cmd_append(&cmd, "wsl", "-e", "sh", "-c",
               temp_sprintf("[ -x build ] || cc -o build build.c && ./build %s", args));
    return cmd_run(&cmd);
}
#endif

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "third_party/nob.h");

    const char *program = shift(argv, argc);
    (void)program;
    const char *target = (argc > 0) ? shift(argv, argc) : "all";

    if (strcmp(target, "tools") == 0) {
        return build_tools() ? 0 : 1;
    }

    if (strcmp(target, "test") == 0) {
        // ./build test [filter]
        const char *filter = (argc > 0) ? shift(argv, argc) : NULL;
        if (!build_native()) return 1;
        bool ok = run_tests(filter);
#if defined(_WIN32)
        ok = build_wsl(temp_sprintf("test %s", filter ? filter : "")) && ok;
#endif
        return ok ? 0 : 1;
    }

#if defined(_WIN32)
    bool want_win = (strcmp(target, "all") == 0) || (strcmp(target, "win") == 0);
    bool want_wsl = (strcmp(target, "all") == 0) || (strcmp(target, "wsl") == 0);
    if (!want_win && !want_wsl) {
        nob_log(ERROR, "unknown target '%s' (expected: win, wsl, test)", target);
        return 1;
    }
    if (want_win && !build_native()) return 1;
    if (want_wsl && !build_wsl("")) return 1;
#else
    if (strcmp(target, "all") != 0) {
        nob_log(ERROR, "unknown target '%s' (expected: test)", target);
        return 1;
    }
    if (!build_native()) return 1;
#endif
    return 0;
}
