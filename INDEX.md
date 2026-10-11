# INDEX

## build

| file | what |
|---|---|
| `Makefile` | thin wrapper over `depfetch` targets: `all`, `fetch`, `protocols`, `shaders`, `clean` |
| `depfetch/src/main.rs` | the whole build system: fetches RGFW.h, runs wayland-scanner for the wayland protocols, compiles C/C++ units, links |

## src

| file | what |
|---|---|
| `src/main.cpp` | entry point, only `main()` lives here |
| `src/window/rgfw_config.h` | the one place that decides how RGFW is configured: wayland only, no X11, vulkan pre-include |
| `src/window/rgfw.c` | RGFW 1.8.1 implementation TU, compiled as **C** (`gcc -std=c11`), silences the debug spam |
| `src/window/rgfw_shit.hpp` | public windowing API of the engine (`namespace rgfw`) |
| `src/window/rgfw_shit.cpp` | implementation of that API, event translation + vulkan surface |
| `src/render/vulkan_casino_777.hpp` | empty placeholder for the renderer |
| `src/heck.hpp` | empty placeholder |