# Setup · Create `memory.lib` and its Google Test project

Start here before Step 01. You'll build a C static library, link it into a Google Test project, and run a smoke test in Visual Studio Test Explorer. Do not create a Console App project or write a `main` function.

## 1. Create the two projects

Open Visual Studio on Windows with **Desktop development with C++** and **Test Adapter for Google Test** installed. From the repository root, work under `memorymigration/`. Create a blank solution named `memory_unit_tests` there (check the location in the dialog). This keeps the exercises independent of `memorymigration.slnx`, whose legacy projects aren't needed. If you already have both projects, reuse them in the test solution via **Add → Existing Project** instead of creating duplicates. A `.vcxproj.user` file by itself is not a project.

1. Add a **Static Library (C++)** project named `memory` at `memorymigration/memory/memory.vcxproj`. The template label doesn't require you to write C++. Remove template `.cpp` files from the project and set **C/C++ → Precompiled Headers → Not Using Precompiled Headers**. Use `.c` for implementation files; for those files, set **C/C++ → Advanced → Compile As → Compile as C Code (/TC)** and select `/std:c11` under **C/C++ → Language → C Language Standard**. The examples use C99 syntax; `/std:c11` is MSVC's compatible mode.
2. Add a **Google Test Project** named `memory_tests_gtest` at `memorymigration/memory_tests_gtest/memory_tests_gtest.vcxproj`. If prompted, select `memory` as the project under test, choose the static Google Test library and dynamic CRT. Keep the template's precompiled header, NuGet configuration and Google Test entry point.
3. In the Google Test project, verify **References** contains `memory` (otherwise use **Add Reference**). Under **C/C++ → General → Additional Include Directories**, add `$(ProjectDir)..\memory`, retaining inherited values. Use matching runtimes across the projects and package (`/MDd` in Debug, `/MD` in Release for the dynamic CRT).

## 2. Add the first library function

Create `memorymigration/memory/memory_block.h` and add it to the `memory` project. The `extern "C"` guards let C++ tests call the C implementation without C++ name mangling.

```c
#ifndef MEMORY_BLOCK_H
#define MEMORY_BLOCK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t front;
    size_t back;
} Memory;

/* base must be NULL or point to suitably aligned writable storage. */
int memory_init(void *base, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
```

Create `memorymigration/memory/memory_block.c` and add it to the `memory` project, not the Google Test project. The first version reserves only the header. Step 02 extends it to place regions before setting `front`.

```c
#include "memory_block.h"

int memory_init(void *base, size_t capacity)
{
    if (!base || capacity < sizeof(Memory))
        return 0;

    Memory *index = base;
    *index = (Memory){sizeof(Memory), capacity};
    return 1;
}
```

Build the `memory` project for **Debug | x64**. Expect `memory.lib` in the project's output directory. The static library has no entry point and isn't run on its own. If the linker asks for `main` when building `memory`, check **Configuration Type → Static Library (.lib)**.

Next: [Run the initialization unit tests](02b-google-test-tutorial.md).
