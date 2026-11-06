//------------------------------------------------------------------------------
//  host.c -- the platform half. Started once: owns the window (sokol_app),
//  the GPU device (sokol_gfx), the game memory block, and loads the game
//  library (out/game.dll / game.so) from a pid-named copy.
//
//  usage: out/engine [--frames N] [--verbose]
//      --frames N   quit after N frames (tests); implies --verbose
//      --verbose    periodic status lines ([game] frame=N)
//------------------------------------------------------------------------------
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/win32.h"
#else
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include "platform/macos.h"
#else
#include "platform/linux.h"
#endif
#endif

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_debugtext.h"
#include "dbgui.h"
#include "game_api.h"

#define GAME_LIB "out/game." LIB_EXT
#define GAME_MEMORY_SIZE ((size_t)64 * 1024 * 1024)

//== platform services =========================================================
static void plat_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

static int64_t plat_file_mtime(const char* path) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return -1;
    }
    return ((int64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
}

static int cmp_names(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static int plat_list_dir(const char* dir, const char* ext, char (*names)[64], int max_names) {
    int count = 0;
    size_t ext_len = strlen(ext);
#if defined(_WIN32)
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s*%s", dir, ext);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        size_t n = strlen(fd.cFileName);
        // FindFirstFile also matches 8.3 short names; re-check the real suffix
        if ((count < max_names) && (n < 64) && (n > ext_len) && (0 == strcmp(fd.cFileName + n - ext_len, ext))) {
            strcpy(names[count++], fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (!d) {
        return 0;
    }
    struct dirent* e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if ((count < max_names) && (n < 64) && (n > ext_len) && (0 == strcmp(e->d_name + n - ext_len, ext))) {
            strcpy(names[count++], e->d_name);
        }
    }
    closedir(d);
#endif
    qsort(names, (size_t)count, 64, cmp_names);
    return count;
}

static int plat_run_process(const char* const* argv, char* output, size_t output_size) {
    size_t used = 0;
    if (output_size > 0) output[0] = 0;
#if defined(_WIN32)
    // CreateProcess resolves a relative program path against the *engine's*
    // directory first; make it relative to the working directory like POSIX
    char program[MAX_PATH];
    const char* prog = argv[0];
    if ((strchr(prog, '/') || strchr(prog, '\\')) && GetFullPathNameA(prog, MAX_PATH, program, NULL)) {
        prog = program;
    }
    // build a quoted command line
    char cmdline[4096];
    size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        int n = snprintf(cmdline + pos, sizeof(cmdline) - pos, "%s\"%s\"", i ? " " : "", i ? argv[i] : prog);
        if ((n < 0) || ((size_t)n >= sizeof(cmdline) - pos)) return -1;
        pos += (size_t)n;
    }
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = { .cb = sizeof(si), .dwFlags = STARTF_USESTDHANDLES,
                        .hStdInput = GetStdHandle(STD_INPUT_HANDLE), .hStdOutput = wr, .hStdError = wr };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    char buf[1024];
    DWORD n;
    while (ReadFile(rd, buf, sizeof(buf), &n, NULL) && (n > 0)) {
        for (DWORD i = 0; (i < n) && (used + 1 < output_size); i++) output[used++] = buf[i];
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (output_size > 0) output[used] = 0;
    return (int)code;
#else
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        dup2(fds[1], 1); dup2(fds[1], 2);
        close(fds[0]); close(fds[1]);
        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }
    close(fds[1]);
    char buf[1024];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; (i < n) && (used + 1 < output_size); i++) output[used++] = buf[i];
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (output_size > 0) output[used] = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static platform_api_t platform = {   // not const: main() sets .verbose
    .file_mtime = plat_file_mtime,
    .list_dir = plat_list_dir,
    .run_process = plat_run_process,
    .log = plat_log,
};

//== game library ==============================================================
typedef struct {
    void* handle;
    game_api_t api;         // copied: the library's own table goes away on unload
    char path[256];         // the pid-named copy we loaded
} game_lib_t;

static int current_pid(void) {
#if defined(_WIN32)
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

static bool copy_file(const char* src, const char* dst) {
#if defined(_WIN32)
    return CopyFileA(src, dst, FALSE);
#else
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[64 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
#endif
}

static void* lib_open(const char* path) {
#if defined(_WIN32)
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* lib_symbol(void* handle, const char* name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
}

static void lib_close(void* handle) {
#if defined(_WIN32)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

// copies GAME_LIB to out/game_<pid>_<n>.<ext> and loads it; on failure the
// copy is removed and *out is untouched
static bool game_lib_load(int n, game_lib_t* out, char* err, size_t err_size) {
    game_lib_t lib = {0};
    snprintf(lib.path, sizeof(lib.path), "out/game_%d_%d." LIB_EXT, current_pid(), n);
    if (!copy_file(GAME_LIB, lib.path)) {
        snprintf(err, err_size, "cannot copy %s to %s", GAME_LIB, lib.path);
        return false;
    }
    lib.handle = lib_open(lib.path);
    if (!lib.handle) {
        snprintf(err, err_size, "cannot load %s", lib.path);
        remove(lib.path);
        return false;
    }
    game_get_api_fn get_api = (game_get_api_fn)lib_symbol(lib.handle, GAME_GET_API_SYMBOL);
    const game_api_t* api = get_api ? get_api() : NULL;
    if (!api || (api->version != GAME_API_VERSION)) {
        snprintf(err, err_size, "%s: missing %s or API version mismatch", lib.path, GAME_GET_API_SYMBOL);
        lib_close(lib.handle);
        remove(lib.path);
        return false;
    }
    lib.api = *api;
    *out = lib;
    return true;
}

static void game_lib_unload(game_lib_t* lib) {
    if (lib->handle) {
        lib_close(lib->handle);
        remove(lib->path);
    }
    *lib = (game_lib_t){0};
}

// removes out/game_<pid>_<n>.<ext> copies left behind by crashed sessions;
// copies still loaded by another running engine are locked (Windows) or
// already mapped (Linux), so removing them is harmless
static void remove_stale_copies(void) {
    static char names[256][64];
    int count = plat_list_dir("out/", "." LIB_EXT, names, 256);
    for (int i = 0; i < count; i++) {
        int pid, n;
        if (sscanf(names[i], "game_%d_%d.", &pid, &n) == 2) {
            char path[128];
            snprintf(path, sizeof(path), "out/%.63s", names[i]);
            remove(path);
        }
    }
}

//== app =======================================================================
static struct {
    game_memory_t memory;
    game_lib_t game;
    int64_t game_mtime;     // mtime of GAME_LIB when last (re)loaded
    int64_t failed_mtime;   // mtime whose load failed (already reported)
    int load_count;
    int frame_limit;        // 0 = run forever
    int frame_count;
} host;

static bool state_fits(size_t state_size) {
    if (state_size > host.memory.size) {
        plat_log("[host] game state (%zu bytes) larger than game memory (%zu)", state_size, host.memory.size);
        return false;
    }
    return true;
}

// called every frame: swaps in a new game library when GAME_LIB changed
static void check_reload(void) {
    int64_t mtime = plat_file_mtime(GAME_LIB);
    if ((mtime == -1) || (mtime == host.game_mtime)) {
        return;
    }

    // a just-renamed file can be briefly unreadable (seen on WSL's /mnt/c):
    // on a failed copy/load keep the old mtime so the next frame retries,
    // but report each new file version only once (and retry it ~2x per second)
    if ((mtime == host.failed_mtime) && ((host.frame_count % 30) != 0)) {
        return;
    }
    game_lib_t next;
    char err[512];
    if (!game_lib_load(host.load_count + 1, &next, err, sizeof(err))) {
        if (mtime != host.failed_mtime) {
            plat_log("[host] reload failed (retrying): %s", err);
            host.failed_mtime = mtime;
        }
        return;
    }
    host.load_count++;
    host.game_mtime = mtime;
    if (!state_fits(next.api.state_size)) {
        game_lib_unload(&next);
        return;
    }
    plat_log("[host] loaded %s", next.path);
    bool size_changed = next.api.state_size != host.game.api.state_size;
    bool layout_changed = next.api.state_layout != host.game.api.state_layout;
    if (size_changed || layout_changed) {
        // layout changed: let the old code release its resources, then start fresh
        if (size_changed) {
            plat_log("[host] state size changed (%zu -> %zu), reinit", host.game.api.state_size, next.api.state_size);
        } else {
            plat_log("[host] state layout changed (%016llx -> %016llx), reinit",
                     (unsigned long long)host.game.api.state_layout, (unsigned long long)next.api.state_layout);
        }
        host.game.api.shutdown(&host.memory, &platform);
        game_lib_unload(&host.game);
        memset(host.memory.base, 0, host.memory.size);
        host.game = next;
        host.game.api.init(&host.memory, &platform);
    } else {
        game_lib_unload(&host.game);
        host.game = next;
        host.game.api.reloaded(&host.memory, &platform);
    }
}

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sdtx_setup(&(sdtx_desc_t){
        .fonts[0] = sdtx_font_kc853(),
        .logger.func = slog_func,
    });
    _dbgui_setup();

    host.memory.size = GAME_MEMORY_SIZE;
    host.memory.base = calloc(1, host.memory.size);
    if (!host.memory.base) {
        plat_log("[host] cannot allocate game memory");
        sapp_quit();
        return;
    }

    remove_stale_copies();
    char err[512];
    host.game_mtime = plat_file_mtime(GAME_LIB);
    if (!game_lib_load(++host.load_count, &host.game, err, sizeof(err))) {
        plat_log("[host] %s", err);
        sapp_quit();
        return;
    }
    plat_log("[host] loaded %s", host.game.path);
    if (!state_fits(host.game.api.state_size)) {
        sapp_quit();
        return;
    }
    host.game.api.init(&host.memory, &platform);
}

static void frame(void) {
    if (host.game.handle) {
        check_reload();
        host.game.api.frame(&host.memory, &platform);
    }
    host.frame_count++;
    if ((host.frame_limit > 0) && (host.frame_count >= host.frame_limit)) {
        sapp_request_quit();
    }
}

static void event(const sapp_event* e) {
    if (host.game.handle) {
        host.game.api.event(&host.memory, &platform, e);
    }
}

static void cleanup(void) {
    if (host.game.handle) {
        host.game.api.shutdown(&host.memory, &platform);
        game_lib_unload(&host.game);
    }
    free(host.memory.base);
    _dbgui_shutdown();
    sdtx_shutdown();
    sg_shutdown();
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if ((0 == strcmp(argv[i], "--frames")) && (i + 1 < argc)) {
            host.frame_limit = atoi(argv[++i]);
            platform.verbose = true;
        } else if (0 == strcmp(argv[i], "--verbose")) {
            platform.verbose = true;
        }
    }
    sapp_run(&(sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = event,
        .width = 1024,
        .height = 768,
        .sample_count = 4,
        .window_title = "space-kit viewer",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    });
    return 0;
}
