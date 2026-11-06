// Dev environment: one command for the edit/build/run loop.
//
//     gcc -o devenv devenv.c       (once; afterwards it rebuilds itself)
//     ./devenv
//
// Builds, opens Sublime Text on the project, launches the engine, then watches src/:
//   - game code saved (src/game.c, src/shader_reload.c, ...): build; the running
//     engine hot reloads the new game library, state is kept
//   - platform layer saved (src/host.c, game_api.h, libs.c, dbgui.*, src/platform/,
//     build.c): close the engine, build, relaunch it
// Compiler output goes to this console. Shaders and models need no build: the
// engine reloads them itself. Ctrl+C to quit.
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
    #define ENGINE "./out/engine.exe"
#else
    #include <signal.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <time.h>
    #define ENGINE "./out/engine"
#endif

#define POLL_MS     200
#define SETTLE_MS   150     // editors may write a file in several steps
#define MAX_FILES   256

// files whose change needs a new engine executable (can't hot reload)
static const char *platform_files[] = {
    "src/host.c", "src/game_api.h", "src/libs.c", "src/dbgui.cc", "src/dbgui.h", "build.c",
};

typedef struct {
    char path[256];
    int64_t mtime;
    bool platform;
} watched_t;

static watched_t files[MAX_FILES];
static int num_files;
static Proc engine = INVALID_PROC;

//== platform helpers ==========================================================
static void sleep_ms(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static int64_t file_mtime(const char *path) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return -1;
    return ((int64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
}

static bool engine_running(void) {
    if (engine == INVALID_PROC) return false;
#if defined(_WIN32)
    if (WaitForSingleObject(engine, 0) == WAIT_TIMEOUT) return true;
    CloseHandle(engine);
#else
    int status;
    if (waitpid(engine, &status, WNOHANG) == 0) return true;
#endif
    engine = INVALID_PROC;
    return false;
}

static void engine_close(void) {
    if (!engine_running()) return;
    nob_log(INFO, "closing engine");
#if defined(_WIN32)
    TerminateProcess(engine, 0);
    WaitForSingleObject(engine, INFINITE);
    CloseHandle(engine);
#else
    kill(engine, SIGTERM);
    waitpid(engine, NULL, 0);
#endif
    engine = INVALID_PROC;
}

static void engine_launch(void) {
    if (engine_running()) return;
    Cmd cmd = {0};
    Procs procs = {0};
    cmd_append(&cmd, ENGINE);
    if (cmd_run(&cmd, .async = &procs) && (procs.count == 1)) {
        engine = procs.items[0];
    }
    free(procs.items);
}

// runs the build script for this platform; its output (compiler errors
// included) goes straight to this console
static bool build(void) {
    Cmd cmd = {0};
#if defined(_WIN32)
    cmd_append(&cmd, "./build.exe", "win");
#else
    cmd_append(&cmd, "./build");
#endif
    bool ok = cmd_run(&cmd);
    nob_log(ok ? INFO : ERROR, ok ? "build ok" : "build FAILED (engine keeps the last good code)");
    return ok;
}

//== watching ==================================================================
static bool has_source_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (!strcmp(dot, ".c") || !strcmp(dot, ".h") || !strcmp(dot, ".cc"));
}

static void watch(const char *path, bool platform) {
    if (num_files >= MAX_FILES) return;
    watched_t *w = &files[num_files++];
    snprintf(w->path, sizeof(w->path), "%s", path);
    w->mtime = file_mtime(path);
    w->platform = platform;
}

static bool is_platform_file(const char *path) {
    for (size_t i = 0; i < ARRAY_LEN(platform_files); i++) {
        if (!strcmp(path, platform_files[i])) return true;
    }
    return false;
}

// src/*.{c,h,cc} (game unless listed as platform), src/platform/* (platform), build.c
static void scan_files(void) {
    num_files = 0;
    File_Paths entries = {0};
    if (read_entire_dir("src", &entries)) {
        for (size_t i = 0; i < entries.count; i++) {
            if (!has_source_ext(entries.items[i])) continue;
            const char *path = temp_sprintf("src/%s", entries.items[i]);
            watch(path, is_platform_file(path));
        }
    }
    entries.count = 0;
    if (read_entire_dir("src/platform", &entries)) {
        for (size_t i = 0; i < entries.count; i++) {
            if (has_source_ext(entries.items[i])) watch(temp_sprintf("src/platform/%s", entries.items[i]), true);
        }
    }
    watch("build.c", true);
    free(entries.items);
    temp_reset();
}

// 0 = nothing changed, 1 = only game code, 2 = platform layer
static int poll_changes(void) {
    int result = 0;
    for (int i = 0; i < num_files; i++) {
        int64_t mtime = file_mtime(files[i].path);
        if (mtime == files[i].mtime) continue;
        files[i].mtime = mtime;
        nob_log(INFO, "%s changed", files[i].path);
        int kind = files[i].platform ? 2 : 1;
        if (kind > result) result = kind;
    }
    return result;
}

//== main ======================================================================
int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "third_party/nob.h");

    Cmd cmd = {0};
    cmd_append(&cmd, "subl", ".");
    if (!cmd_run(&cmd)) nob_log(WARNING, "could not start Sublime Text (is `subl` on PATH?)");

    scan_files();
    if (build()) engine_launch();
    nob_log(INFO, "watching %d files; save game code to hot reload, platform code to restart (Ctrl+C quits)", num_files);

    for (;;) {
        sleep_ms(POLL_MS);
        int change = poll_changes();
        if (change == 0) continue;
        sleep_ms(SETTLE_MS);
        int again = poll_changes();
        if (again > change) change = again;
        scan_files();   // pick up added/removed files; resets mtimes to now

        if (change == 2) {
            // the engine executable and libs are locked while it runs
            engine_close();
            if (build()) engine_launch();
        } else {
            if (build() && !engine_running()) engine_launch();
        }
    }
}
