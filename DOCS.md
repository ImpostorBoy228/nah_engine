# DOCS

## windowing: wayland only

The engine window layer is wayland only. X11 and the XWayland fallback are gone:
there is no `Xlib.h` anywhere in the include graph and the linked binary has zero
X11/xcb dependencies (check with `ldd shit/build/game/nah_engine`).

### how it is put together

```
src/window/rgfw_config.h   RGFW_WAYLAND + RGFW_NO_X11 + VK_USE_PLATFORM_WAYLAND_KHR
        |                  RGFW_VULKAN + RGFW_SNPRINTF + RGFW_IMPORT
        +--> src/window/rgfw.c        RGFW_IMPLEMENTATION + RGFW_EXPORT, compiled by gcc as C
        +--> src/window/rgfw_shit.hpp the engine facing API, compiled by g++ as C++
```

### code style

`src/window/*` follows `AGENTS.md` and is the reference for the rest of `src`:

- `//` comments only, no `/* */`, no `///`, one short line per comment. the trap notes below used to be
  `/* */` blocks, now every one of them is a single line
- signatures that do not fit in 80 columns put one argument per line, closing paren on its own line
- structs (`Event`, `WindowDesc`) and `enum class EventKind` start with an uppercase letter, fields
  and function arguments are snake_case, indentation is 4 spaces
- the API is `namespace rgfw` + free functions on purpose, there is no class to name, but if one ever
  shows up it is camelCase and lives in the `.hpp`
- `// BLACKBOX` marks the functions that hide the real work: `init`, `createWindow`, `pollEvents`,
  `waitEvent`
- no dead stores: `Event event = {}` already zeroes every field, so only the non zero defaults
  (`EventKind::none`, `RGFW_keyNULL`, `RGFW_mouseLeft`, `scale = 1.0f`) are assigned

traps that had to be worked around:

- **RGFW's wayland backend is not C++ clean.** void* conversions and out of order
  designated initializers all over `RGFW_window_createWindowPlatform_Wayland`, so the
  implementation has to be a C translation unit. `-fpermissive` was not an option.
- **`RGFWDEF` degrades to `inline`.** Without `RGFW_EXPORT`/`RGFW_IMPORT` the
  declarations in `RGFW.h` become `inline` with no definition in the TU, so `-O2`
  is free to emit calls against an undefined inline function. `RGFW_IMPORT` in
  `rgfw_config.h` (and `RGFW_EXPORT` in `rgfw.c`) turns `RGFWDEF` into a plain
  external declaration and the UB disappears. No `noinline` hacks needed anymore.
- **`RGFW_WAYLAND` hard-defines `RGFW_DEBUG`** (`RGFW.h:207`), which printf()s every
  internal step. `rgfw.c` hijacks `RGFW_PRINTF`, the only sink it uses, and drops
  messages with a `strncmp(fmt, "RGFW I", 6)` prefix match, keeping warnings and errors
  on stderr.
- **`RGFW_VK_SURFACE` on wayland also defines `VK_USE_PLATFORM_XLIB_KHR`**
  (`RGFW.h:423`), which makes `vulkan.h` pull `<X11/Xlib.h>` in. `rgfw_config.h`
  includes `<vulkan/vulkan.h>` *before* `RGFW.h` with only
  `VK_USE_PLATFORM_WAYLAND_KHR` set, so vulkan's `VULKAN_H_` guard makes the later
  include a no-op and X11 headers never appear.
- **RGFW needs 6 generated wayland protocol headers** (`RGFW.h:7356`). `citrus`
  runs `wayland-scanner client-header` + `private-code` for each into
  `shit/build/protocols` and compiles the `-protocol.c` files. The `private-code`
  part matters: `RGFW.h` references `xdg_wm_base_interface` and friends.
- **`RGFW_window_close` already deinits.** It calls `RGFW_deinit` when the last
  window dies (`RGFW.h:3395`), and `RGFW_deinit` is not idempotent: the second
  call dereferences a NULL `_RGFW`. The lifecycle is therefore tracked in
  `rgfw_shit.cpp` (`started` / `alive_windows`) instead of guessing from RGFW.
- **`RGFW_window_checkEvent` blocks.** Its wayland `RGFW_pollEvents` calls
  `wl_display_dispatch`, which sleeps until the compositor sends something. A game
  loop must not use it, so `rgfw::pollEvents` drives `wl_display_dispatch_pending`
  itself and drains RGFW's ring with `RGFW_window_checkQueuedEvent`.
- **`MAX_QUEUED_EVENTS` must stay a power of two.** The event ring indexes with
  `& (MAX_QUEUED_EVENTS - 1)` instead of `%`, a `static_assert` guards it.
- **`RGFW_c89_snprintf`.** C++ has no `__STDC_VERSION__`, so RGFW assumes C89 and
  ships an unused `vsprintf` based snprintf shim. `RGFW_SNPRINTF` in the config
  header kills it.

### vulkan

`rgfw::getRequiredInstanceExtensions()` returns `VK_KHR_surface` +
`VK_KHR_wayland_surface`, `rgfw::createSurface()` calls RGFW's wrapper, which ends up
in `vkCreateWaylandSurfaceKHR`. Verified on this machine: instance + surface both
return `VK_SUCCESS` and surface capabilities come back from 2 physical devices.

The engine still owns the swapchain: create it with
`rgfw::getDisplay()` (the `wl_display*`) and `rgfw::getSurface()` (the `wl_surface*`),
recreate it on `EventKind::resize`, and flush the display after present
(`rgfw::pollEvents` / `rgfw::waitEvent` already do `wl_display_flush`).

### renderer: casino

`src/render/vulkan_casino_777.hpp` is a header-only vulkan layer in
`namespace casino`. It knows nothing about 2d, that lives in
`render/primitives.hpp`. The core is a `class Device` (RAII, not copyable,
destructor tears it down) that owns every handle end to end:

- instance with `VK_EXT_debug_utils` + `VK_LAYER_KHRONOS_validation` when present
  (`InitDesc.validation`), errors/warnings routed to stderr
- surface via `rgfw::createSurface`, physical device pick (discrete GPU scores
  higher, needs graphics+present queues and `VK_KHR_swapchain`)
- logical device, swapchain (sRGB if offered, FIFO/vsync, `minImageCount+1`)
- swapchain image views, one color render pass, framebuffers
- `MAX_FRAMES_IN_FLIGHT = 2`: command pool + per-frame command buffers, fences,
  per-frame `image_available` and per-swapchain-image `render_finished` semaphores
- `beginFrame` waits the fence, acquires an image, begins the render pass and
  sets viewport + scissor; `endFrame` ends the pass, submits and presents.
  `OUT_OF_DATE` / `SUBOPTIMAL` trigger `recreateSwapchain` (also called on
  `EventKind::resize`)

`// BLACKBOX` marks `init`, `recreateSwapchain`, `beginFrame`, `endFrame`. The
clear color is fixed `{ 0.08, 0.08, 0.10, 1.0 }`, the 2d layer draws on top.

reusable RAII helpers live here too: `casino::Pipeline` (graphics pipeline over
the device render pass, spirv vertex + fragment), `casino::GpuBuffer` (device
local, host visible or staging), `casino::Texture` (image + view + sampler +
descriptor set, uploaded through a one-shot command buffer). verified on this
machine: RTX 3050, 1280x720, 3 images, zero validation errors.

### API

```c++
#include "render/vulkan_casino_777.hpp"

casino::Device device;
casino::InitDesc render_desc = {};
render_desc.win = win;              // an RGFW_window from rgfw::createWindow
render_desc.app_name = "nah_engine";
render_desc.validation = true;

if (!device.init(render_desc))
    return 1;

while (!rgfw::shouldClose(win)) {
    rgfw::waitEvent(win, 16);
    while (rgfw::nextEvent(&event)) {
        if (event.kind == rgfw::EventKind::resize)
            device.recreateSwapchain();
    }

    if (!device.beginFrame())
        continue;

    /* draw here: device.currentCommand(), device.frameSlot(), device.currentExtent() */

    device.endFrame();
}

device.shutdown();                   // runs before rgfw::destroyWindow
```

accessors: `frameSlot()`, `currentCommand()`, `currentIndex()`,
`currentExtent()`, `currentRenderPass()`. The handles are public members
(`device`, `gpu`, `graphics_queue`, `command_pool`, ...) so the 2d layer can
create its own buffers, pipelines and descriptor sets.

### renderer: primitives

`src/render/primitives.hpp` (`namespace prim`) is a 2d immediate-mode batcher on
top of `casino::Device`. Coordinates are relative: `(0,0)` top-left, `(1,1)`
bottom-right, y-down. One pipeline (vertex color * texture), a white 1x1 texture
stands in for untextured prims. `// BLACKBOX` on `init`, `createTexture`,
`loadTexture`.

- `drawRect`, `drawTriangle`, `drawImage` push vertices into a flat array
- verts are grouped into batches by descriptor set (one batch per texture),
  `render` copies the array into the frame's host-visible vertex buffer and
  issues one `vkCmdDraw` per batch
- `MAX_VERTS = 65536`, `MAX_TEXTURES = 128`; an overflow drops the vertex

```c++
prim::Primitives prims;
prims.init(device);
casino::Texture uv;
prims.loadTexture(&uv, "assets/uv.png");

prims.begin();
prims.drawRect(0.05f, 0.25f, 0.25f, 0.25f, prim::rgba(0.90f, 0.25f, 0.35f, 1.0f));
prims.drawImage(uv, x, y, w, h);
prims.render(device.currentCommand(), device.frameSlot());
```

### renderer: image

`src/render/image.hpp` (`namespace img`) is a minimal dependency-free PNG
loader: 8-bit gray/rgb/graya/rgba, non-interlaced, inflated by a hand-written
inflate (bit reader, canonical huffman, LZ77 back-refs, the five PNG filters)
and expanded to rgba8. `img::loadPng` reads the file, `img::freeImage` releases
the `malloc`ed pixels. Unsupported files are reported to stderr and rejected.

### assets + scripts

`src/paths.hpp` (`namespace paths`) resolves asset and script paths relative to
the executable (`/proc/self/exe` + `dirname`), not the cwd, so the game runs from
any launcher or working directory. Absolute paths pass through, a missing
`/proc` falls back to the cwd. `// BLACKBOX` on `resolve`.

`src/ligma.hpp` is a thin `class Ligma` LuaJIT wrapper: `ligma_execute` runs a
script, the constructor opens the standard libs, the destructor closes the
state. It also exposes a small C API (`gachicreate` / `gachicum` / `gachiprint`
on a `GachiMan` struct) that `main.lua` calls through the LuaJIT FFI. For
`ffi.C` to resolve those symbols the binary is linked with `-rdynamic`.

the script (`src/luascripts/main.lua`) is deployed next to the executable and
`assets/` into `<game>/assets/`, both picked up by `paths::resolve` at runtime.

### windowing API

```c++
#include "window/rgfw_shit.hpp"

rgfw::init();

rgfw::WindowDesc desc = {};
desc.title = "nah";
desc.width = 1280;
desc.height = 720;
desc.resizable = true;
desc.fullscreen = false;
desc.borderless = false;
desc.exit_on_escape = true;

RGFW_window* win = rgfw::createWindow(desc);

while (!rgfw::shouldClose(win)) {
    rgfw::waitEvent(win, 16);              // or rgfw::pollEvents(win) for a non blocking frame

    rgfw::Event ev = {};
    while (rgfw::nextEvent(&ev)) { /* ev.kind, ev.key, ev.mouse_x, ... */ }

    /* render, recreate the swapchain on ev.kind == rgfw::EventKind::resize */
}

rgfw::destroyWindow(win);
rgfw::deinit();
```

input state is also queryable directly: `rgfw::isKeyDown`, `rgfw::isKeyPressed`,
`rgfw::isKeyReleased`, `rgfw::isMouseButtonDown`, `rgfw::getMousePos`.

### building

```
make          # shaders + protocols + compile + link, then deploy scripts + assets
make fetch    # RGFW.h + git submodules
make depmake  # LuaJIT static lib
make protocols
make shaders  # glslc compiles shaders/ into spirv .bin.h headers
make run      # runs shit/build/game/nah_engine
make clean
```

`make` runs `shadermake` (`glslc` on `shaders/*.vert|frag|comp` into
`shit/build/binshaders/*.bin.h`, spirv baked as a `static const uint8_t` array),
`protocols`, the C/C++ units, the link, then deploys `src/luascripts` next to the
executable and `assets/` into `<game>/assets/`.

needs `gcc g++ glslc make libwayland-dev wayland-protocols libxkbcommon-dev
libvulkan-dev`. wayland-protocols are located via
`pkg-config --variable=pkgdatadir wayland-protocols` (with a
`/usr/share/wayland-protocols` fallback).

units are built with `-std=c23` (C) / `-std=c++23` (C++). LuaJIT lives in the
`shit/external/luajit` submodule. `depmake` builds it with
`BUILDMODE=static CC=gcc -fPIC`, so the objects can be linked into the PIE
executable, and `make` pulls `libluajit.a` in with `-Wl,--whole-archive`:
`luaL_openlibs` registers the C modules by name, so the linker would otherwise
drop most of the archive. The link also passes `-rdynamic` so the LuaJIT FFI can
`dlsym` the `GachiMan` C API out of the main executable.