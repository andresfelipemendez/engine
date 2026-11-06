#ifndef PLATFORM_MACOS_H
#define PLATFORM_MACOS_H

#define LIB_EXT "dylib"
#define SHDC_PATH "tools/macos/sokol-shdc"
// Darwin's struct stat names the timespec field st_mtimespec
#define st_mtim st_mtimespec

#endif

