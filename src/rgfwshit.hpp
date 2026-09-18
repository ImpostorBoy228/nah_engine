#ifndef HUINYA_RGFWSHIT_HPP
#define HUINYA_RGFWSHIT_HPP

#if !defined(RGFW_VULKAN)
    #define RGFW_VULKAN
#endif

#include "RGFW.h"
#include <vulkan/vulkan.h>
#include <vector>

#include <X11/Xlib.h>

// Workaround for an RGFW.h footgun: on unix the X11 accessors are declared
// `RGFWDEF` (which degrades to `inline` when RGFW_EXPORT is unset) but their
// definitions live in another translation unit. Under -O2 the compiler may
// emit calls against the inline declaration without a visible definition =>
// UB (typically NULL display / black screen). Forcing a real call via
// noinline ensures the linker resolves it against the definition in rgfw.o.
__attribute__((noinline))
inline void* rgfw_get_display() {
    return RGFW_getDisplay_X11();
}

__attribute__((noinline))
inline unsigned long rgfw_get_window(RGFW_window* win) {
    return static_cast<unsigned long>(RGFW_window_getWindow_X11(win));
}

// On Wayland+XWayland, RGFW's XMapWindow may not flush to the X server.
// Force a flush + sync so the window actually maps and is visible.
inline void flush_x11() {
    void* dpy = RGFW_getDisplay_X11();
    if (dpy) {
        XFlush(static_cast<Display*>(dpy));
        XSync(static_cast<Display*>(dpy), False);
    }
}

// X11 surface creation can race with window mapping on XWayland.
// Force-map the window with XMapRaised to guarantee it is visible.
inline void force_map_window(RGFW_window* win) {
    void* dpy = RGFW_getDisplay_X11();
    if (dpy && win) {
        unsigned long xwin = static_cast<unsigned long>(RGFW_window_getWindow_X11(win));
        if (xwin) {
            Display* display = static_cast<Display*>(dpy);
            XRaiseWindow(display, xwin);
            XMapRaised(display, xwin);
            XFlush(display);
            XSync(display, False);
        }
    }
}

namespace rgfw {
    inline RGFW_window* createWindow(int width, int height, const char* title, bool resizable = true) {
        RGFW_windowFlags flags = 0;
        if (!resizable) {
            // TODO
        }
        (void)flags;
        RGFW_window* win = RGFW_createWindow(title, 0, 0, width, height, (RGFW_windowFlags)0);
        force_map_window(win);
        return win;
    }

    inline void pollEvents(RGFW_window* win) {
        RGFW_event ev;
        while (RGFW_window_checkEvent(win, &ev)) {
            // TODO: events
        }
        flush_x11();
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
