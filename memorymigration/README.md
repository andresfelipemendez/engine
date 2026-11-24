# Memory migration: branch coverage demo

Open **this folder** in VS Code (`code .\memorymigration` from the `engine` root). Install Visual Studio or Build Tools with the MSVC x64 tools and **C++ Clang tools for Windows**. The script locates the toolchain and initializes the build environment; VS Code doesn't need to be launched from a Developer Prompt.

- **Ctrl+Shift+B** builds the instrumented C tests, runs them, and prints LLVM branch coverage. Open `build/coverage/index.html` in a browser for the HTML report.
- **F5** with the Microsoft **C/C++** extension runs the same test executable under the debugger and prints coverage after the process exits normally. Stopping the process can prevent LLVM from writing its profile.

From a PowerShell terminal in this folder, try smaller runs:

```powershell
.\coverage.ps1 -Scenario if-only  # Passes; True: 1, False: 0 (50% branch coverage)
.\coverage.ps1 -Scenario fail     # Fails on purpose; still generates coverage
.\coverage.ps1                    # Both paths; True: 1, False: 1 (100% branch coverage)
```

### Add another test

The runner uses [Greatest](https://github.com/silentbicycle/greatest), a single-header C test library. In `test_migration.c`, add a named test and register it with `RUN_TEST(new_test_name);` inside `SUITE(migration)`:

```c
TEST version_two_stays_two(void)
{
    ASSERT_EQ(2, migration_target_version(2));
    PASS();
}
```

No build-script changes are needed. Greatest is vendored at `11a6af1919049df502405913da64fb385c219361`; its license is at the top of `vendor/greatest.h`.

### See coverage in the editor

Install the recommended [Coverage Gutters](https://marketplace.visualstudio.com/items?itemName=ryanluker.vscode-coverage-gutters) extension, run the build task to create `build/lcov.info`, and run **Coverage Gutters: Watch** from the Command Palette. Open `migration.c` to see covered and uncovered lines. Run `-Scenario if-only` to make the uncovered `else` visible. Use the terminal or HTML report for the explicit true/false counts.

The report covers `migration.c`, not the test runner. Each run replaces the previous report. The failure demo expects `3` where the function returns `2`; coverage means the code ran, not that the test passed.
