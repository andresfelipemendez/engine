#ifndef PLATFORM_H
#define PLATFORM_H

#include <arena.h>

#include <stddef.h>

typedef struct {
    int exit_code;
    char *output;
    size_t output_len;
} CommandResult;

 unsigned char * platform_reserve(size_t capacity);

CommandResult run_command(const char *cmd, char *out_buf, size_t out_size);
CommandResult compile(Arena *arena, const char *src_path, const char* out_path);

#endif