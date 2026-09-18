#ifndef HUINYA_RGFWSHIT_HPP
#define HUINYA_RGFWSHIT_HPP

#if !defined(RGFW_VULKAN)
    #define RGFW_VULKAN
#endif

#include "RGFW.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace rgfw {
    inline RGFW_window* createWindow(int width, int height, const char* title, bool resizable = true) {
        RGFW_windowFlags flags = 0;
        if (!resizable) {
            // TODO
        }
        (void)flags;
        return RGFW_createWindow(title, 0, 0, width, height, (RGFW_windowFlags)0);
    }

    inline void pollEvents(RGFW_window* win) {
        RGFW_event ev;
        while (RGFW_window_checkEvent(win, &ev)) {
            // TODO: events
        }
    }

    inline bool shouldClose(RGFW_window* win) {
        return (RGFW_window_shouldClose(win) == RGFW_TRUE);
    }

    inline void destroyWindow(RGFW_window* win) {
        if (win)
            RGFW_window_close(win);
    }

    inline std::vector<const char*> getRequiredInstanceExtensions() {
        size_t count = 0;
        const char** exts = RGFW_getRequiredInstanceExtensions_Vulkan(&count);
        return std::vector<const char*>(exts, exts + count);
    }

    inline VkResult createSurface(RGFW_window* win, VkInstance instance, VkSurfaceKHR* surface) {
        return RGFW_window_createSurface_Vulkan(win, instance, surface);
    }

    inline bool init() {
        return (RGFW_init() == 0);
    }
}

#endif
