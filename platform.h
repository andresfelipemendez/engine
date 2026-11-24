#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "error.h"
#include "string.h"

typedef enum PlatformKind {
    KIND_WINDOWS,
    KIND_POSIX
} PlatformKind;


typedef uint64_t FileTimeStamp;

typedef struct PlatformFile {
    PlatformKind kind;
    union {
        void* win_handle;
        void* c_file;
    } native;
} PlatformFile;

typedef struct PlatformLibrary {
    PlatformKind kind;
    union {
        void* win_module;
        void* posix_handle;
    } native;
    char name[256];
} PlatformLibrary;

void load_env(void);
bool open_file(String path, PlatformFile* out, Error* err);
void close_file(PlatformFile* file, Error* err);
bool create_directory(String path, Error* err);
bool get_file_time(PlatformFile* file, FileTimeStamp* out, Error* err);
bool load_library(const char* name, PlatformLibrary* out, Error* err);

#endif
