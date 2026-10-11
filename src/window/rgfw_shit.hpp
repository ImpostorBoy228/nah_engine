#ifndef NAH_WINDOW_RGFW_SHIT_HPP
#define NAH_WINDOW_RGFW_SHIT_HPP

#include "rgfw_config.h"

#include "RGFW.h"

namespace rgfw {

    using Key = RGFW_key;
    using KeyMod = RGFW_keymod;
    using MouseButton = RGFW_mouseButton;

    enum class EventKind {
        none,
        quit,
        resize,
        move,
        focus_gained,
        focus_lost,
        refresh,
        key_pressed,
        key_released,
        mouse_pressed,
        mouse_released,
        mouse_moved,
        mouse_scroll,
        scale_changed,
        file_drop,
        file_drag,
    };

    struct Event {
        RGFW_window* win;
        EventKind kind;

        Key key;
        KeyMod mods;
        u8 key_char;
        bool repeat;

        i32 mouse_x;
        i32 mouse_y;
        MouseButton button;
        float scroll_x;
        float scroll_y;

        i32 x;
        i32 y;
        i32 width;
        i32 height;
        float scale_x;
        float scale_y;
    };

    struct WindowDesc {
        const char* title;
        i32 width;
        i32 height;
        bool resizable;
        bool fullscreen;
        bool borderless;
        bool exit_on_escape;
    };

    bool init(); // BLACKBOX
    void deinit();

    RGFW_window* createWindow(const WindowDesc& desc); // BLACKBOX
    void destroyWindow(RGFW_window* win);

    void pollEvents(RGFW_window* win); // BLACKBOX
    void waitEvent(RGFW_window* win, i32 timeout_ms); // BLACKBOX
    bool nextEvent(Event* out);

    bool shouldClose(RGFW_window* win);
    void setShouldClose(RGFW_window* win, bool value);

    void getSize(RGFW_window* win, i32* width, i32* height);
    void setTitle(RGFW_window* win, const char* title);
    void setMinSize(RGFW_window* win, i32 width, i32 height);
    void setExitKey(RGFW_window* win, Key key);
    bool setIcon(RGFW_window* win, u8* pixels, i32 width, i32 height, RGFW_format format);

    bool isFocused(RGFW_window* win);
    bool isMinimized(RGFW_window* win);
    bool isMaximized(RGFW_window* win);

    bool isKeyDown(RGFW_window* win, Key key);
    bool isKeyPressed(RGFW_window* win, Key key);
    bool isKeyReleased(RGFW_window* win, Key key);
    bool isMouseButtonDown(RGFW_window* win, MouseButton button);
    void getMousePos(RGFW_window* win, i32* x, i32* y);

    struct wl_display* getDisplay();
    struct wl_surface* getSurface(RGFW_window* win);

    const char* const* getRequiredInstanceExtensions(u32* count);
    VkResult createSurface(RGFW_window* win, VkInstance instance, VkSurfaceKHR* surface);

} // namespace rgfw

#endif