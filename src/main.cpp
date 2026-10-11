#include "nuts.hpp"
#include "render/primitives.hpp"
#include "window/rgfw_shit.hpp"
#include "ligma.hpp"
#include "paths.hpp"
#include <limits.h>
#include <stdio.h>

int main() {
    char lua_path[PATH_MAX];
    char uv_path[PATH_MAX];

    Ligma lua;
    lua.ligma_execute(paths::resolve("main.lua", lua_path, sizeof(lua_path)));

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

    prim::Primitives prims;
    if (!prims.init(device)) {
        fuck("primitives init failed");
        return 1;
    }

    casino::Texture uv;
    bool have_uv = prims.loadTexture(&uv, paths::resolve("./assets/uv.png", uv_path, sizeof(uv_path)));
    if (!have_uv) {
        printf("uv.png missing, drawing without it\n");
    }

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
        // a background strip + a rect + a triangle, all relative coords
        prims.drawRect(0.0f, 0.0f, 1.0f, 0.12f, prim::rgba(0.15f, 0.15f, 0.20f, 1.0f));
        prims.drawRect(0.05f, 0.25f, 0.25f, 0.25f, prim::rgba(0.90f, 0.25f, 0.35f, 1.0f));
        prims.drawTriangle(0.65f, 0.20f, 0.90f, 0.20f, 0.775f, 0.48f, prim::rgba(0.25f, 0.75f, 0.35f, 1.0f));

        if (have_uv) {
            // keep the image's pixel aspect on a non square window
            VkExtent2D e = device.currentExtent();
            float screen_aspect = (float)e.width / (float)e.height;
            float img_aspect = (float)uv.height / (float)uv.width;
            float w = 0.55f;
            float h = w * screen_aspect * img_aspect;
            prims.drawImage(uv, 0.5f - w * 0.5f, 0.72f - h * 0.5f, w, h);
        }

        prims.render(device.currentCommand(), device.frameSlot());
        device.endFrame();
    }

    return 0;
}
