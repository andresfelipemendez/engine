# 00a · Create `memory.lib` and its Google Test project

Start here before Step 01. You'll build a C static library, link it into a Google Test project, and run a smoke test in Visual Studio Test Explorer. Do not create a Console App project or write a `main` function.

## 1. Create the two projects

Open Visual Studio on Windows with **Desktop development with C++** and **Test Adapter for Google Test** installed. From the repository root, work under `memorymigration/`. Create a blank solution named `memory_unit_tests` there (check the location in the dialog). This keeps the exercises independent of `memorymigration.slnx`, whose legacy projects aren't needed. If you already have both projects, reuse them in the test solution via **Add → Existing Project** instead of creating duplicates. A `.vcxproj.user` file by itself is not a project.

1. Add a **Static Library (C++)** project named `memory` at `memorymigration/memory/memory.vcxproj`. The template label doesn't require you to write C++. Remove template `.cpp` files from the project and set **C/C++ → Precompiled Headers → Not Using Precompiled Headers**. Use `.c` for implementation files; for those files, set **C/C++ → Advanced → Compile As → Compile as C Code (/TC)** and select `/std:c11` under **C/C++ → Language → C Language Standard**. The examples use C99 syntax; `/std:c11` is MSVC's compatible mode.
2. Use the existing Google Test project at `memorymigration/memory_test/memory_test.vcxproj`. In Solution Explorer, rename its display name to `memory_tests` if needed; the paths in these lessons use the checkout's `memory_test/` directory. If starting without that project, add a **Google Test Project** named `memory_tests` and use its `memory_tests/` directory for the test files instead. If prompted, select `memory` as the project under test, choose the static Google Test library and dynamic CRT. Keep the template's precompiled header, NuGet configuration and Google Test entry point.
3. In the Google Test project, verify **References** contains `memory` (otherwise use **Add Reference**). Under **C/C++ → General → Additional Include Directories**, add `$(ProjectDir)..\memory`, retaining inherited values. Use matching runtimes across the projects and package (`/MDd` in Debug, `/MD` in Release for the dynamic CRT).

## 2. Add the first library function

Open `memorymigration/memory/memory.h` in the `memory` project and replace the empty header with the following. This is the library header, not the separate prototype header at `memorymigration/memory.h`. `MemoryBlock` groups the base pointer and capacity without owning or copying the bytes. `Memory` is the header stored at byte zero. The C compiler skips the `extern "C"` guards; Google Test's C++ compiler sees them and links to the C symbol in `memory.lib`.

```c
#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char *base;
    size_t capacity;
} MemoryBlock;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t front;
    size_t back;
} Memory;

/* Returns false if block.base is NULL or the block is too small.
   Otherwise, block.base must point to suitably aligned writable storage
   of at least block.capacity bytes. */
bool memory_init(MemoryBlock block);

#ifdef __cplusplus
}
#endif
#endif
```

Open `memorymigration/memory/memory.c` in the `memory` project and replace its contents with the following. The first version reserves only the header. Step 02 extends it to place regions before setting `front`.

```c
#include "memory.h"

bool memory_init(MemoryBlock block)
{
    if (!block.base || block.capacity < sizeof(Memory))
        return false;

    Memory *index = (Memory *)block.base;
    *index = (Memory){sizeof(Memory), block.capacity};
    return true;
}
```

Build the `memory` project for **Debug | x64**. Expect `memory.lib` in the project's output directory. The static library has no entry point and isn't run on its own. If the linker asks for `main` when building `memory`, check **Configuration Type → Static Library (.lib)**.

Next: [00b · Run the initialization unit tests](00b-google-test-tutorial.md).
