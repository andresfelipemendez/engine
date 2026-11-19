#ifndef PLATFORM_H
#define PLATFORM_H

#include "time.h"
#include <ctime>
#include <time.h>

typedef enum PlatformKind {
    KIND_WINDOWS,
    KIND_POSIX
} PlatformKind;

typedef struct error{
    char message[256];
} error;

typedef struct file_time {
    time_t created;
    time_t accessed;
    time_t modified;
} file_time;

typedef struct PlatformFile {
    PlatformKind kind;
    union {
        void* win_handle;   /* HANDLE  – opaque here, cast in .c */
        void* c_file;       /* FILE*  */
    } native;
} PlatformFile;

typedef struct PlatformLibrary {
    PlatformKind kind;
    union {
        void* win_module;   /* HMODULE – opaque here, cast in .c */
        void* posix_handle; /* dlopen */
    } native;
    char name[256];
} PlatformLibrary;

void load_env(void);
int get_file_time(PlatformFile* file, error* err);
int load_library(const char* name, PlatformLibrary* library, error* err);

#endif
