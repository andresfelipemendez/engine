// End-to-end hot reload tests. Built and run by the build script:
//
//     ./build test            run all tests (on Windows: Windows + WSL)
//     ./build test model      only tests whose name contains "model"
//
// Each test drives the real engine (out/engine --frames N), reads its log
// (out/test.log) and rebuilds the game library while it runs.
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "../third_party/nob.h"

#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
    #include <sys/utime.h>
    #define EXE      "./out/engine.exe"   /* ./ : CreateProcess searches the caller's dir first */
    #define LIB_EXT  "dll"
#else
    #include <signal.h>
    #include <time.h>
    #include <sys/wait.h>
    #include <unistd.h>
    #include <utime.h>
    #define EXE      "./out/engine"
    #define LIB_EXT  "so"
#endif

#define LOG        "out/test.log"
#define BUILD_LOG  "out/test_build.log"

//== helpers ===================================================================
#define CHECK(cond, ...) do { if (!(cond)) { \
        printf("    %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); \
        return false; } } while (0)

static Proc engine = INVALID_PROC;

static void sleep_ms(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

// waits up to timeout_ms for the engine to exit; returns its exit code,
// or -1 if it is still running (it is killed in that case)
static int engine_wait(int timeout_ms) {
    if (engine == INVALID_PROC) return -1;
    int code = -1;
#if defined(_WIN32)
    if (WaitForSingleObject(engine, (DWORD)timeout_ms) == WAIT_OBJECT_0) {
        DWORD c = 1;
        GetExitCodeProcess(engine, &c);
        code = (int)c;
    } else {
        TerminateProcess(engine, 1);
        WaitForSingleObject(engine, INFINITE);
    }
    CloseHandle(engine);
#else
    int status = 0;
    for (int waited = 0; ; waited += 50) {
        if (waitpid(engine, &status, WNOHANG) == engine) {
            code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        if (waited >= timeout_ms) {
            kill(engine, SIGKILL);
            waitpid(engine, &status, 0);
            break;
        }
        sleep_ms(50);
    }
#endif
    engine = INVALID_PROC;
    return code;
}

static void engine_stop(void) {
    engine_wait(0);
}

static bool engine_start(int frames) {
    remove(LOG);
    Cmd cmd = {0};
#if defined(_WIN32)
    // not nob's .stdout_path: it opens the file without FILE_SHARE_READ, so
    // the log could not be read until the engine exits
    (void)cmd;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE log = CreateFileA(LOG, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) return false;
    STARTUPINFOA si = { .cb = sizeof(si), .dwFlags = STARTF_USESTDHANDLES,
                        .hStdInput = GetStdHandle(STD_INPUT_HANDLE), .hStdOutput = log, .hStdError = log };
    PROCESS_INFORMATION pi;
    char cmdline[128];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" --frames %d", EXE, frames);
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(log);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    engine = pi.hProcess;
    return true;
#else
    Procs procs = {0};
    cmd_append(&cmd, EXE, "--frames", temp_sprintf("%d", frames));
    if (!cmd_run(&cmd, .async = &procs, .stdout_path = LOG)) return false;
    engine = procs.items[0];
    free(procs.items);
    return true;
#endif
}

// runs the engine to completion (blocking); returns its exit code
static int engine_run(int frames) {
    if (!engine_start(frames)) return -1;
    return engine_wait(30000);
}

static bool log_contains(const char *needle) {
    String_Builder sb = {0};
    if (!read_entire_file(LOG, &sb)) { return false; }
    sb_append_null(&sb);
    bool found = strstr(sb.items, needle) != NULL;
    sb_free(sb);
    return found;
}

static int log_count(const char *needle) {
    String_Builder sb = {0};
    if (!read_entire_file(LOG, &sb)) return 0;
    sb_append_null(&sb);
    int count = 0;
    for (const char *p = sb.items; (p = strstr(p, needle)); p += strlen(needle)) count++;
    sb_free(sb);
    return count;
}

// first integer following needle in the log, or -1
static int log_int_after(const char *needle) {
    String_Builder sb = {0};
    if (!read_entire_file(LOG, &sb)) return -1;
    sb_append_null(&sb);
    const char *p = strstr(sb.items, needle);
    int value = p ? atoi(p + strlen(needle)) : -1;
    sb_free(sb);
    return value;
}

static bool wait_for(const char *needle, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 100) {
        if (log_contains(needle)) return true;
        sleep_ms(100);
    }
    printf("    timeout waiting for '%s'\n", needle);
    return false;
}

static void touch(const char *path) {
    utime(path, NULL);
}

static void set_env(const char *name, const char *value) {
#if defined(_WIN32)
    SetEnvironmentVariableA(name, value);   // NULL removes it
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}

// rebuilds the game (output in BUILD_LOG); game_cflags may be NULL
static bool rebuild(const char *game_cflags) {
    set_env("GAME_CFLAGS", game_cflags);
    Cmd cmd = {0};
#if defined(_WIN32)
    cmd_append(&cmd, "./build.exe", "win");
#else
    cmd_append(&cmd, "./build");
#endif
    bool ok = cmd_run(&cmd, .stdout_path = BUILD_LOG, .stderr_path = BUILD_LOG ".err");
    set_env("GAME_CFLAGS", NULL);
    return ok;
}

//== tests =====================================================================
static bool test_starts_and_quits(void) {
    CHECK(engine_run(30) == 0, "engine did not exit cleanly");
    CHECK(log_contains("[host] loaded out/game_"), "no host load line");
    CHECK(log_contains("[game] init"), "no game init line");
    return true;
}

static bool test_reload_keeps_state(void) {
    CHECK(engine_start(6000), "cannot start engine");
    CHECK(wait_for("[game] frame=60", 20000), "game not running");
    touch("src/game.c");
    CHECK(rebuild(NULL), "build failed, see " BUILD_LOG ".err");
    CHECK(wait_for("_2." LIB_EXT, 20000), "no second load");
    CHECK(wait_for("[game] reloaded frame=", 5000), "reloaded() not called");
    int n = log_int_after("[game] reloaded frame=");
    CHECK(n > 0, "frame counter was %d at reload", n);
    int next = (n / 60 + 1) * 60;
    CHECK(wait_for(temp_sprintf("[game] frame=%d", next), 5000), "counter did not continue from %d", n);
    CHECK(log_count("[game] init") == 1, "game was re-initialized");
    return true;
}

static bool test_build_while_running(void) {
    CHECK(engine_start(6000), "cannot start engine");
    CHECK(wait_for("[game] init", 20000), "game not running");
    CHECK(rebuild(NULL), "build failed while engine running, see " BUILD_LOG ".err");
    return true;
}

static bool test_compile_error_keeps_old(void) {
    const char *lib = "out/game." LIB_EXT;
    String_Builder before = {0}, after = {0};
    CHECK(read_entire_file(lib, &before), "cannot read %s", lib);
    CHECK(engine_start(6000), "cannot start engine");
    CHECK(wait_for("[game] init", 20000), "game not running");
    CHECK(!rebuild("-DGAME_TEST_COMPILE_ERROR"), "build should have failed");
    CHECK(read_entire_file(lib, &after), "cannot read %s", lib);
    CHECK((before.count == after.count) && (0 == memcmp(before.items, after.items, before.count)),
          "%s was replaced by a failed build", lib);
    sb_free(before); sb_free(after);
    // the engine is still alive and still running the old code
    int frames = log_count("[game] frame=");
    sleep_ms(1500);
    CHECK(log_count("[game] frame=") > frames, "engine stopped after the failed build");
    CHECK(!log_contains("_2." LIB_EXT), "engine reloaded after a failed build");
    return true;
}

static bool test_state_size_change_reinits(void) {
    CHECK(engine_start(6000), "cannot start engine");
    CHECK(wait_for("[game] init", 20000), "game not running");
    bool built = rebuild("-DGAME_TEST_STATE_PAD=64");
    bool changed = built && wait_for("[host] state size changed", 20000);
    sleep_ms(500);
    int inits = log_count("[game] init");
    rebuild(NULL);  // restore the normal game library
    CHECK(built, "build failed");
    CHECK(changed, "size change not detected");
    CHECK(inits == 2, "expected 2 inits, got %d", inits);
    return true;
}

// swaps two same-size fields in game_state_t: size is unchanged, layout is not
static bool test_state_layout_change_reinits(void) {
    const char *src = "src/game.c";
    const char *a = "int num_models;", *b = "int cur;";
    String_Builder original = {0};
    CHECK(read_entire_file(src, &original), "cannot read %s", src);
    sb_append_null(&original);
    original.count--;
    const char *pa = strstr(original.items, a);
    const char *pb = pa ? strstr(pa, b) : NULL;
    CHECK(pa && pb, "%s: fields '%s' then '%s' not found", src, a, b);

    CHECK(engine_start(6000), "cannot start engine");
    bool ready = wait_for("[game] init", 20000);
    // swap the two declarations, keeping whatever line endings the file uses
    String_Builder swapped = {0};
    sb_append_buf(&swapped, original.items, (size_t)(pa - original.items));
    sb_append_cstr(&swapped, b);
    sb_append_buf(&swapped, pa + strlen(a), (size_t)(pb - (pa + strlen(a))));
    sb_append_cstr(&swapped, a);
    sb_append_cstr(&swapped, pb + strlen(b));
    bool wrote = ready && write_entire_file(src, swapped.items, swapped.count);
    bool built = wrote && rebuild(NULL);
    bool restored = write_entire_file(src, original.items, original.count);
    bool changed = built && wait_for("[host] state layout changed", 20000);
    sleep_ms(500);
    int inits = log_count("[game] init");
    rebuild(NULL);  // rebuild from the restored source
    sb_free(original); sb_free(swapped);

    CHECK(restored, "COULD NOT RESTORE %s - restore it by hand", src);
    CHECK(ready, "game not running");
    CHECK(built, "build failed");
    CHECK(changed, "layout change not detected");
    CHECK(inits == 2, "expected 2 inits, got %d", inits);
    return true;
}

static bool test_stale_copies_cleaned(void) {
    const char *stale = "out/game_99999_1." LIB_EXT;
    CHECK(write_entire_file(stale, "", 0), "cannot create %s", stale);
    CHECK(engine_start(300), "cannot start engine");
    bool ready = wait_for("[game] init", 20000);
    bool gone = !file_exists(stale);
    remove(stale);
    CHECK(ready, "game not running");
    CHECK(gone, "%s still exists", stale);
    return true;
}

static bool test_copies_removed_on_exit(void) {
    CHECK(engine_run(60) == 0, "engine did not exit cleanly");
    String_Builder sb = {0};
    CHECK(read_entire_file(LOG, &sb), "no log");
    sb_append_null(&sb);
    const char *p = strstr(sb.items, "out/game_");
    CHECK(p, "no load line");
    char copy[128];
    CHECK(sscanf(p, "%127s", copy) == 1, "cannot parse load line");
    sb_free(sb);
    CHECK(!file_exists(copy), "%s still exists after exit", copy);
    return true;
}

#define SHADER "src/shaders/model.glsl"

static bool test_shader_reload_ok(void) {
    CHECK(engine_start(6000), "cannot start engine");
    CHECK(wait_for("[game] init", 20000), "game not running");
    touch(SHADER);
    CHECK(wait_for("[shader] reloaded " SHADER, 20000), "shader not reloaded");
    return true;
}

static bool test_shader_error_keeps_old(void) {
    String_Builder original = {0};
    CHECK(read_entire_file(SHADER, &original), "cannot read " SHADER);
    CHECK(engine_start(6000), "cannot start engine");
    bool ready = wait_for("[game] init", 20000);

    // break the shader, then always restore it before checking anything
    // garbage inside the first @vs/@fs block (lines outside blocks are ignored by shdc)
    String_Builder broken = {0};
    sb_append_null(&original);
    original.count--;
    const char *first_end = strstr(original.items, "@end");
    CHECK(first_end, SHADER " has no @end");
    sb_append_buf(&broken, original.items, (size_t)(first_end - original.items));
    sb_append_cstr(&broken, "this is not glsl;\n");
    sb_append_cstr(&broken, first_end);
    bool wrote = write_entire_file(SHADER, broken.items, broken.count);
    bool got_error = ready && wrote && wait_for("[shader] error " SHADER, 20000);
    sleep_ms(200);
    bool restored = write_entire_file(SHADER, original.items, original.count);
    bool recovered = got_error && wait_for("[shader] reloaded " SHADER, 20000);
    int frame_before = log_count("[game] frame=");
    sleep_ms(1500);
    bool still_running = log_count("[game] frame=") > frame_before;
    sb_free(original); sb_free(broken);

    CHECK(restored, "COULD NOT RESTORE " SHADER " - restore it by hand");
    CHECK(ready, "game not running");
    CHECK(got_error, "no shader error reported");
    CHECK(recovered, "fixed shader not reloaded");
    CHECK(still_running, "engine stopped after the shader error");
    return true;
}

#define MODEL_DIR "assets/space-kit/"

static bool test_model_reload(void) {
    CHECK(engine_start(6000), "cannot start engine");
    CHECK(wait_for("[game] init", 20000), "game not running");
    touch(MODEL_DIR "alien.glb");
    CHECK(wait_for("[model] reloaded alien.glb", 20000), "model not reloaded");
    return true;
}

static bool test_selection_survives_list_change(void) {
    const char *extra = MODEL_DIR "aaa_test.glb";   // sorts before alien.glb
    CHECK(engine_start(6000), "cannot start engine");
    bool ready = wait_for("[game] init", 20000);
    bool copied = ready && copy_file(MODEL_DIR "alien.glb", extra);
    bool listed = copied && wait_for("[model] list 154", 20000);
    int selected_after_add = log_count("[model] selected alien.glb");
    bool removed = (remove(extra) == 0) || !file_exists(extra);
    bool relisted = listed && wait_for("[model] list 153", 20000);
    sleep_ms(300);
    int selected_after_remove = log_count("[model] selected alien.glb");

    CHECK(removed, "COULD NOT REMOVE %s - delete it by hand", extra);
    CHECK(ready, "game not running");
    CHECK(copied, "cannot copy alien.glb");
    CHECK(listed, "added model not listed");
    CHECK(selected_after_add >= 1, "selection moved off alien.glb after add");
    CHECK(relisted, "removed model still listed");
    CHECK(selected_after_remove >= 2, "selection moved off alien.glb after remove");
    CHECK(log_count("[model] selected aaa_test.glb") == 0, "selection jumped to the new file");
    return true;
}

//== runner ====================================================================
typedef struct { const char *name; bool (*fn)(void); } test_t;

static const test_t tests[] = {
    { "starts_and_quits",          test_starts_and_quits },
    { "reload_keeps_state",        test_reload_keeps_state },
    { "build_while_running",       test_build_while_running },
    { "compile_error_keeps_old",   test_compile_error_keeps_old },
    { "state_size_change_reinits", test_state_size_change_reinits },
    { "state_layout_change_reinits", test_state_layout_change_reinits },
    { "stale_copies_cleaned",      test_stale_copies_cleaned },
    { "copies_removed_on_exit",    test_copies_removed_on_exit },
    { "shader_reload_ok",          test_shader_reload_ok },
    { "shader_error_keeps_old",    test_shader_error_keeps_old },
    { "model_reload",              test_model_reload },
    { "selection_survives_list_change", test_selection_survives_list_change },
};

int main(int argc, char **argv) {
    nob_minimal_log_level = NOB_WARNING;   // don't echo every CMD
    const char *filter = (argc > 1) ? argv[1] : "";
    int passed = 0, failed = 0;
    for (size_t i = 0; i < ARRAY_LEN(tests); i++) {
        if (!strstr(tests[i].name, filter)) continue;
        bool ok = tests[i].fn();
        engine_stop();
        temp_reset();
        printf("%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
        fflush(stdout);
        if (ok) passed++; else failed++;
    }
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
