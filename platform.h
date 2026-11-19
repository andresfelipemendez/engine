#ifndef PLATFORM_H
#define PLATFORM_H

typedef struct PlatformFile PlatformFile;

void load_env();

int get_file_time(PlatformFile* file);

#endif
