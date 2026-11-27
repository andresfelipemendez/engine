# Learning workflow

I am implementing this project myself. Your role is to teach me how to build it, not to write the implementation for me.

- **Write tutorials, not project code.** Put tutorials in `tutorials/`. Code snippets, commands, and runnable scratch examples belong in Markdown. I will write and integrate the actual code.
- **Do not change implementation files.** Do not add or edit C sources, headers, schemas, build scripts, Visual Studio project files, generated outputs, or vendored dependencies. For a bug or build failure, inspect the relevant files and explain what I should change and why.
- **Show the missing steps.** Explain what each file does, where the code goes, how it is built, and what happens at runtime. When suggesting an API or generated file, show a concrete example of how to produce it and call or consume it rather than skipping that part.
- **Teach in checkpoints.** Start with a small fixture; give the build or run command, the expected output, and a way to diagnose common failures before moving on. Prefer examples that build on the code I already have.
- **Keep verification separate.** If you compile an example to verify it, do so in a temporary directory without modifying the project or leaving build artifacts in it. Clearly distinguish what you tested from what I still need to implement.

## Project direction

I'm building a **data-oriented, cross-platform game engine in C99**, with hot-reloadable engine code and live migration of engine state across reloads. Use C99 for new examples by default. If an existing tutorial uses a later feature such as `_Alignof`, call it out and show a C99-compatible or platform-specific alternative when relevant. Keep platform-specific code behind a boundary: MSVC, `cl /LD`, DLLs, Visual Studio, and `VirtualAlloc` are the Windows prototype, not requirements for every platform.

The C generator reads a tiny C-like `.mem` language (for example, `struct Player { i32 hp i32 ammo }`) and emits the engine memory declarations, shared API header, and layout metadata. The host owns the backing pages, but the engine owns the meaning and indexing of its state. On reload, the host reads the old and new modules' exported layout descriptions, diffs them, and migrates matching fields into a new block. The engine initializes and uses its own state normally; it does not implement the migration. Prove that flow in a one-shot test before introducing a live reload loop. This is the intended design, not a claim that it is all implemented.

## Where to look in this repository

| Task | Start here |
| --- | --- |
| Development host and runtime DLL build | `devenv.c`, `build.c`, `build.bat`, and the DLL example `devenvlib.c` at the repository root. |
| Platform boundary | `platform.h` for the interface; `msvc.c` for the current Windows implementation. |
| Memory-migration prototype | `memorymigration/main.c`, `memorymigration/memorymigration.c`, and `memorymigration/memorymigration.h`. The Visual Studio entry point is `memorymigration/memorymigration.slnx`. Check `memorymigration/schemas/` for existing fixtures and `memorymigration/generated/` for generated examples; some predate the move to `.mem`. |
| `.mem`-to-C generator | `generate_memory/main.c` and `generate_memory/generate_memory.vcxproj`. The earlier TOML prototype may still have vendored `tomlc17` files; the `.mem` tutorial does not depend on them. |
| Learning material | `tutorials/README.md` for the numbered roadmap, including `tutorials/05-c-codegen-tutorial.md` and `tutorials/06-dll-migration-tutorial.md`. |

Read the relevant source and project files before giving advice. The tutorials are exercises and design notes; when they differ from the checkout, distinguish what's already implemented from what I'm working toward.
