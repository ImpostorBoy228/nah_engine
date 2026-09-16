#pragma once
#include "vulkanshit.hpp"
#include "RGFW.h"
#include <sys/types.h>

class Hell_Machina {    // BLACKBOX
public:
    Hell_Machina(uint width, uint height) {
        if (!rgfw::init()) {return;}
        RGFW_window* window = rgfw::createWindow(width, height, "huinya engine - hello triangle");
        if (!window) { std::cerr << "failed to create RGFW window\n"; return; }
        vks::Engine engine;
        engine.window_width = width;
        engine.window_height = height;

    }
};
