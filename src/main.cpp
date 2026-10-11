#include <iostream>

#include "heck.hpp"

#include "bgfx/bgfx.h"

int main() {
    bgfx::Init init;
    init.reset = 60;
    bgfx::init(init);

    std::cout << "bgfx initialized\n";
    std::cout << "render device: "
              << bgfx::getRendererName(bgfx::getRendererType())
              << "\n";

    bgfx::frame();
    bgfx::shutdown();
    return 0;
}
