# Engine

I'm building a data-oriented, cross-platform game engine in C99, with hot reload and live memory migration between builds.

## How I'm building it

I use AI as a tutor to teach myself: it explains concepts, helps me understand errors, and writes [tutorials](tutorials/) that I can follow. **This project is not vibe-coded.** I type all of the project code myself. AI helps me learn how to implement it; it doesn't write the implementation for me.

## Build

```bash
cc devenv.c
cc -o devenvlib.dll devenvlib.c -shared
```

```bash
cl devenv.c
cl .\devenvlib.c /LD
```

