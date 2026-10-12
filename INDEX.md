# INDEX

## build

| file | what |
|---|---|
| `Makefile` | thin wrapper over `citrus` targets: `all`, `fetch`, `depmake`, `protocols`, `shaders`, `run`, `clean` |
| `citrus/src/main.rs` | the whole build system: fetches RGFW.h, syncs git submodules, builds LuaJIT as a static lib, runs `glslc` on `shaders/` into spirv `.bin.h` headers, runs wayland-scanner for the wayland protocols, compiles C/C++ units (`-std=c2x` / `-std=c++2b`), links (`-Wl,--whole-archive` for LuaJIT, `-rdynamic` for the LuaJIT FFI), then deploys `src/luascripts` and `assets/` next to the executable; cmake-style `[n/N]` progress with a red counter and a green label, full command line printed under every step, compiler stdout muted (`NO_COLOR` disables the color) |

## src

| file | what |
|---|---|
| `src/main.cpp` | entry point, only `main()` lives here |
| `src/window/rgfw_config.h` | the one place that decides how RGFW is configured: wayland only, no X11, vulkan pre-include |
| `src/window/rgfw.c` | RGFW 1.8.1 implementation TU, compiled as **C** (`gcc -std=c2x`), silences the debug spam |
| `src/window/rgfw_shit.hpp` | public windowing API of the engine (`namespace rgfw`), grouped into lifecycle / events / window state / input / vulkan |
| `src/window/rgfw_shit.cpp` | implementation of that API: event translation, lifecycle bookkeeping, vulkan surface |
| `src/render/vulkan_casino_777.hpp` | header-only vulkan layer (`namespace casino`): `class Device` (instance, device, swapchain, `beginFrame`/`endFrame` loop) plus RAII `Pipeline`, `GpuBuffer` and `Texture` helpers |
| `src/render/textures.hpp` | header-only texture manager (`namespace prim`): `TextureId` handle + `TextureCache`, a fixed pool of 128 gpu textures behind refcounted slots recycled through a freelist, path-keyed so the same png loads once, gen-tagged so a stale handle can never crash a draw |
| `src/render/camera.hpp` | header-only camera math (`namespace cam`): column-major `Mat4`, `Camera` (position, yaw/pitch, fov), `viewProj` (perspective, depth 0..1) and `ortho01` for the 2d layer |
| `src/render/primitives.hpp` | header-only batcher (`namespace prim`, `class Primitives`): 3d prims (triangle, quad, box) depth tested with the camera, 2d prims (rect, triangle, image) in relative coords drawn over the 3d pass, two pipelines, texture batching, `Anchor` enum and aspect-correct `drawImageFit` |
| `src/render/drawlist.hpp` | header-only retained draw list (`namespace prim`, `class DrawList`): records 2d and 3d ops plus the camera once and replays them into the batcher every frame, so a static scene is not re-issued from lua each frame |
| `src/render/image.hpp` | header-only dependency-free PNG loader (`namespace img`): hand-written inflate + filters, decodes to rgba8 |
| `src/paths.hpp` | resolves asset/script paths relative to the executable (`namespace paths`), so the game runs from any cwd |
| `src/ligma.hpp` | thin LuaJIT wrapper (`class Ligma`) plus the `nah_*` render C ABI (`NahCtx` + `nah_bind`/`nah_tex`/`nah_clear`/`nah_camera`/`nah_tri3d`/`nah_quad3d`/`nah_box3d`/`nah_rect`/`nah_triangle`/`nah_image`/`nah_image_fit`) that drives the texture cache and draw list from lua |

## assets + shaders

| file | what |
|---|---|
| `shaders/prim.vert`, `shaders/prim.frag` | GLSL 450 sources for both pipelines (3d and 2d), vertex stage takes a `mat4` push constant, compiled by `make shaders` into `shit/build/binshaders/*.bin.h` |
| `src/luascripts/main.lua` | LuaJIT script run at startup, builds the 3d + 2d demo scene through the `nah_*` C API via `ffi.C` |
| `assets/` | runtime assets (e.g. `uv.png`), copied into `<game>/assets/` on build |

## style

the window code is the reference for the rules in `AGENTS.md`:

- `//` comments only, no `/* */`, no `///`, one short line per comment, only where needed
- one level of indentation, 4 spaces
- functions: snake_case arguments, a signature that does not fit in 80 columns gets one argument per
  line, closing paren on its own line
- structs and `enum class`: first letter uppercase
- classes are camelCase and only defined in `.hpp` files (the renderer and primitives layers use
  `casino::Device` / `prim::Primitives`, the window layer is namespace + free functions on purpose)
- `// BLACKBOX` marks the public functions that hide real work