#include "render/vulkan_casino_777.hpp"
#include "window/rgfw_shit.hpp"

int main() {
    if (!rgfw::init()) {
        printf("wayland init failed\n");
        return 1;
    }

    rgfw::WindowDesc desc = {};
    desc.title = "nah_engine";
    desc.width = 1280;
    desc.height = 720;
    desc.resizable = true;
    desc.fullscreen = false;
    desc.borderless = false;
    desc.exit_on_escape = true;

    RGFW_window* win = rgfw::createWindow(desc);
    if (win == NULL) {
        printf("window creation failed\n");
        rgfw::deinit();
        return 1;
    }

    casino::InitDesc render_desc = {};
    render_desc.win = win;
    render_desc.app_name = "nah_engine";
    render_desc.validation = true;

    if (!casino::init(render_desc)) {
        printf("vulkan init failed\n");
        rgfw::destroyWindow(win);
        rgfw::deinit();
        return 1;
    }

    rgfw::Event event = {};
    while (!rgfw::shouldClose(win)) {
        rgfw::waitEvent(win, 16);

        while (rgfw::nextEvent(&event)) {
            switch (event.kind) {
                case rgfw::EventKind::resize:
                    casino::recreateSwapchain();
                    break;
                case rgfw::EventKind::quit:
                    printf("quit\n");
                    break;
                default:
                    break;
            }
        }

        casino::drawFrame();
    }

    casino::shutdown();
    rgfw::destroyWindow(win);
    rgfw::deinit();
    return 0;
}
