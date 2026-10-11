#include "nuts.hpp"
#include "render/textures.hpp"
#include "render/primitives.hpp"
#include "render/drawlist.hpp"
#include "ligma.hpp"
#include "paths.hpp"
#include <limits.h>

int main() {
    char lua_path[PATH_MAX];

    Window win(1280, 720);

    casino::Device device;
    casino::InitDesc render_desc = {};
    render_desc.win = win.get();
    render_desc.app_name = "nah_engine";
    render_desc.validation = true;
    if (!device.init(render_desc)) {
        fuck("vulkan init failed");
        return 1;
    }

    prim::TextureCache tex;
    if (!tex.init(device)) {
        fuck("texture cache init failed");
        return 1;
    }

    prim::Primitives prims;
    if (!prims.init(device, tex)) {
        fuck("primitives init failed");
        return 1;
    }

    prim::DrawList scene;

    NahCtx ctx = { &device, &tex, &prims, &scene };
    nah_bind(&ctx);

    Ligma lua;
    lua.ligma_execute(paths::resolve("main.lua", lua_path, sizeof(lua_path)));

    rgfw::Event event = {};
    while (!win.shouldClose()) {
        rgfw::waitEvent(win.get(), 16);
        while (rgfw::nextEvent(&event)) {
            if (event.kind == rgfw::EventKind::resize)
                device.recreateSwapchain();
        }

        if (!device.beginFrame())
            continue;

        prims.begin();
        scene.replay(prims);
        prims.render(device.currentCommand(), device.frameSlot());
        device.endFrame();
    }

    return 0;
}
