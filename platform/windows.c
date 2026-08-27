#include "platform.h"

#include <arena.h>

#include <memoryapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


CommandResult run_command(Arena *arena, const char *cmd) {
    CommandResult result = {0};
    result.output = arena->base + arena->used;

    FILE *pipe = _popen(cmd, "r");
    if(!pipe) {
        result.exit_code = -1;
        return result;
    }

    char chunk[1024];
    size_t n;

    while((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        char *dst = arena_alloc(arena,n);
        if(!dst) break;
        memcpy(dst, chunk, n);
        result.output_len += n;
    }

    char *terminator = arena_alloc(arena, 1);
    if(terminator) *terminator = '\0';

    result.exit_code = _pclose(pipe);
    return result;
}

CommandResult compile(Arena *arena, const char *src_path, const char* out_path);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
    "clang -target x86_64-pc-windows-msvc -xc -std=c99 -Wl,/PDB:%s.pdb %s -o %s",
    out_path, src_path, out_path);
    return platform_run_command(arena, cmd);
}

unsigned char * platform_reserve(size_t capacity){
VirtualAlloc(NULL, capacity, MEM_RESERVE, PAGE_NOACCESS);
}