#pragma once
// Contract between the host (engine executable, started once) and the game
// library (game.dll / game.so, hot reloaded).
//
// Rules for the game side:
//   - all persistent state lives in game_memory_t.base (owned by the host)
//   - never store pointers into the game library itself (function pointers,
//     string literals, static data): their addresses change on every reload
//   - OS-specific work goes through platform_api_t
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sokol_app.h"

#define GAME_API_VERSION 3

typedef struct {
    // modification time in nanoseconds-ish ticks, -1 if the file doesn't exist
    int64_t (*file_mtime)(const char* path);
    // fills names with the files in dir ending in ext (e.g. ".glb"), sorted; returns count
    int (*list_dir)(const char* dir, const char* ext, char (*names)[64], int max_names);
    // runs argv (NULL-terminated), captures stdout+stderr into output (NUL-terminated);
    // returns the exit code, or -1 if the process could not be started
    int (*run_process)(const char* const* argv, char* output, size_t output_size);
    // printf-style, prints to stdout with a trailing newline and flushes
    void (*log)(const char* fmt, ...);
    // true when started with --verbose or --frames (tests): log periodic status
    bool verbose;
} platform_api_t;

typedef struct {
    void* base;     // zeroed before init
    size_t size;
} game_memory_t;

typedef struct {
    uint32_t version;       // must equal GAME_API_VERSION
    size_t state_size;      // sizeof the game's state struct; a change forces re-init
    uint64_t state_layout;  // hash of the state struct's source text (computed by build.c);
                            // catches reordered/retyped fields of the same total size
    void (*init)(game_memory_t* mem, const platform_api_t* p);
    void (*reloaded)(game_memory_t* mem, const platform_api_t* p);  // code swapped, state kept
    void (*frame)(game_memory_t* mem, const platform_api_t* p);
    void (*event)(game_memory_t* mem, const platform_api_t* p, const sapp_event* e);
    void (*shutdown)(game_memory_t* mem, const platform_api_t* p);
} game_api_t;

typedef const game_api_t* (*game_get_api_fn)(void);
#define GAME_GET_API_SYMBOL "game_get_api"

#if defined(_WIN32)
#define GAME_EXPORT __declspec(dllexport)
#else
#define GAME_EXPORT __attribute__((visibility("default")))
#endif
