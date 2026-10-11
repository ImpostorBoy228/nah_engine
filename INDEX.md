# INDEX

## build

| file | what |
|---|---|
| `Makefile` | thin wrapper over `citrus` targets: `all`, `fetch`, `depmake`, `protocols`, `shaders`, `run`, `clean` |
| `citrus/src/main.rs` | the whole build system: fetches RGFW.h, syncs git submodules, builds LuaJIT as a static lib, runs wayland-scanner for the wayland protocols, compiles C/C++ units, links |

## src

| file | what |
|---|---|
| `src/main.cpp` | entry point, only `main()` lives here |
| `src/window/rgfw_config.h` | the one place that decides how RGFW is configured: wayland only, no X11, vulkan pre-include |
| `src/window/rgfw.c` | RGFW 1.8.1 implementation TU, compiled as **C** (`gcc -std=c11`), silences the debug spam |
| `src/window/rgfw_shit.hpp` | public windowing API of the engine (`namespace rgfw`), grouped into lifecycle / events / window state / input / vulkan |
| `src/window/rgfw_shit.cpp` | implementation of that API: event translation, lifecycle bookkeeping, vulkan surface |
| `src/render/vulkan_casino_777.hpp` | empty placeholder for the renderer |
| `src/heck.hpp` | empty placeholder |

## style

the window code is the reference for the rules in `AGENTS.md`:

- `//` comments only, no `/* */`, no `///`, one short line per comment, only where needed
- one level of indentation, 4 spaces
- functions: snake_case arguments, a signature that does not fit in 80 columns gets one argument per
  line, closing paren on its own line
- structs and `enum class`: first letter uppercase
- classes would be camelCase and are only defined in `.hpp` files (the window layer is namespace +
  free functions on purpose, so there is no class to follow the rule with)
- `// BLACKBOX` marks the public functions that hide real work