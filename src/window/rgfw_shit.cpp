#include "rgfw_shit.hpp"

#include <errno.h>
#include <wayland-client.h>

namespace rgfw {

    static const u32 MAX_QUEUED_EVENTS = 256;

    static Event queued_events[MAX_QUEUED_EVENTS];
    static u32 queued_head = 0;
    static u32 queued_count = 0;

    /* RGFW tears the platform down by itself when the last window closes, and
       RGFW_deinit is not idempotent, so the lifecycle is tracked here */
    static bool started = false;
    static u32 alive_windows = 0;

    static void push_event(const Event* event) {
        if (queued_count == MAX_QUEUED_EVENTS)
            return;

        u32 slot = (queued_head + queued_count) % MAX_QUEUED_EVENTS;
        queued_events[slot] = *event;
        queued_count += 1;
    }

    static void drain_queue(RGFW_window* win) {
        RGFW_event raw;

        while (RGFW_window_checkQueuedEvent(win, &raw)) {
            Event event = {};
            event.win = win;
            event.kind = EventKind::none;
            event.key = RGFW_keyNULL;
            event.mods = 0;
            event.key_char = 0;
            event.repeat = false;
            event.mouse_x = 0;
            event.mouse_y = 0;
            event.button = RGFW_mouseLeft;
            event.scroll_x = 0.0f;
            event.scroll_y = 0.0f;
            event.x = 0;
            event.y = 0;
            event.width = 0;
            event.height = 0;
            event.scale_x = 1.0f;
            event.scale_y = 1.0f;

            switch (raw.type) {
                case RGFW_quit:
                    event.kind = EventKind::quit;
                    break;
                case RGFW_windowResized:
                    event.kind = EventKind::resize;
                    RGFW_window_getPosition(win, &event.x, &event.y);
                    RGFW_window_getSize(win, &event.width, &event.height);
                    break;
                case RGFW_windowMoved:
                    event.kind = EventKind::move;
                    RGFW_window_getPosition(win, &event.x, &event.y);
                    RGFW_window_getSize(win, &event.width, &event.height);
                    break;
                case RGFW_focusIn:
                    event.kind = EventKind::focus_gained;
                    break;
                case RGFW_focusOut:
                    event.kind = EventKind::focus_lost;
                    break;
                case RGFW_windowRefresh:
                    event.kind = EventKind::refresh;
                    break;
                case RGFW_keyPressed:
                    event.kind = EventKind::key_pressed;
                    event.key = raw.key.value;
                    event.mods = raw.key.mod;
                    event.key_char = raw.key.sym;
                    event.repeat = raw.key.repeat == RGFW_TRUE;
                    break;
                case RGFW_keyReleased:
                    event.kind = EventKind::key_released;
                    event.key = raw.key.value;
                    event.mods = raw.key.mod;
                    event.key_char = raw.key.sym;
                    break;
                case RGFW_mouseButtonPressed:
                    event.kind = EventKind::mouse_pressed;
                    event.button = (MouseButton)raw.button.value;
                    RGFW_window_getMouse(win, &event.mouse_x, &event.mouse_y);
                    break;
                case RGFW_mouseButtonReleased:
                    event.kind = EventKind::mouse_released;
                    event.button = (MouseButton)raw.button.value;
                    RGFW_window_getMouse(win, &event.mouse_x, &event.mouse_y);
                    break;
                case RGFW_mousePosChanged:
                    event.kind = EventKind::mouse_moved;
                    event.mouse_x = raw.mouse.x;
                    event.mouse_y = raw.mouse.y;
                    break;
                case RGFW_mouseScroll:
                    event.kind = EventKind::mouse_scroll;
                    event.scroll_x = raw.scroll.x;
                    event.scroll_y = raw.scroll.y;
                    RGFW_window_getMouse(win, &event.mouse_x, &event.mouse_y);
                    break;
                case RGFW_scaleUpdated:
                    event.kind = EventKind::scale_changed;
                    event.scale_x = raw.scale.x;
                    event.scale_y = raw.scale.y;
                    break;
                case RGFW_dataDrop:
                    event.kind = EventKind::file_drop;
                    RGFW_window_getPosition(win, &event.x, &event.y);
                    break;
                case RGFW_dataDrag:
                    event.kind = EventKind::file_drag;
                    event.x = raw.drag.x;
                    event.y = raw.drag.y;
                    break;
                default:
                    continue;
            }

            push_event(&event);
        }
    }

    bool init() { // BLACKBOX
        if (started)
            return true;

        if (RGFW_init() < 0)
            return false;

        if (RGFW_usingWayland() == RGFW_FALSE)
            return false;

        if (RGFW_getDisplay_Wayland() == NULL)
            return false;

        started = true;
        return true;
    }

    void deinit() {
        queued_head = 0;
        queued_count = 0;

        if (alive_windows != 0 || !started)
            return;

        started = false;
        RGFW_deinit();
    }

    RGFW_window* createWindow(const WindowDesc& desc) { // BLACKBOX
        if (!started)
            return NULL;

        RGFW_windowFlags flags = (RGFW_windowFlags)0;

        if (!desc.resizable)
            flags |= RGFW_windowNoResize;
        if (desc.fullscreen)
            flags |= RGFW_windowFullscreen;
        if (desc.borderless)
            flags |= RGFW_windowNoBorder;

        RGFW_window* win = RGFW_createWindow(desc.title, 0, 0, desc.width, desc.height, flags);
        if (win == NULL)
            return NULL;

        alive_windows += 1;
        RGFW_window_setExitKey(win, desc.exit_on_escape ? RGFW_escape : RGFW_keyNULL);
        return win;
    }

    void destroyWindow(RGFW_window* win) {
        if (win == NULL)
            return;

        RGFW_window_close(win);

        alive_windows -= 1;
        if (alive_windows == 0)
            started = false;
    }

    void pollEvents(RGFW_window* win) { // BLACKBOX
        if (win == NULL)
            return;

        struct wl_display* display = RGFW_getDisplay_Wayland();
        if (display == NULL)
            return;

        RGFW_setQueueEvents(RGFW_TRUE);

        if (wl_display_flush(display) == -1 && errno == EAGAIN)
            wl_display_dispatch_pending(display);

        if (wl_display_dispatch_pending(display) == -1)
            return;

        drain_queue(win);
    }

    void waitEvent(RGFW_window* win, i32 timeout_ms) { // BLACKBOX
        if (win == NULL)
            return;

        if (timeout_ms == 0) {
            pollEvents(win);
            return;
        }

        RGFW_setQueueEvents(RGFW_TRUE);
        RGFW_waitForEvent(timeout_ms);
        drain_queue(win);
    }

    bool nextEvent(Event* out) {
        if (queued_count == 0)
            return false;

        *out = queued_events[queued_head];
        queued_head = (queued_head + 1) % MAX_QUEUED_EVENTS;
        queued_count -= 1;
        return true;
    }

    bool shouldClose(RGFW_window* win) {
        if (win == NULL)
            return true;

        return RGFW_window_shouldClose(win) == RGFW_TRUE;
    }

    void setShouldClose(RGFW_window* win, bool value) {
        if (win == NULL)
            return;

        RGFW_window_setShouldClose(win, RGFW_BOOL(value));
    }

    void getSize(RGFW_window* win, i32* width, i32* height) {
        if (win == NULL)
            return;

        RGFW_window_getSize(win, width, height);
    }

    void setTitle(RGFW_window* win, const char* title) {
        if (win == NULL || title == NULL)
            return;

        RGFW_window_setName(win, title);
    }

    void setMinSize(RGFW_window* win, i32 width, i32 height) {
        if (win == NULL)
            return;

        RGFW_window_setMinSize(win, width, height);
    }

    void setExitKey(RGFW_window* win, Key key) {
        if (win == NULL)
            return;

        RGFW_window_setExitKey(win, key);
    }

    bool setIcon(RGFW_window* win, u8* pixels, i32 width, i32 height, RGFW_format format) {
        if (win == NULL || pixels == NULL)
            return false;

        return RGFW_window_setIcon(win, pixels, width, height, format) == RGFW_TRUE;
    }

    bool isFocused(RGFW_window* win) {
        if (win == NULL)
            return false;

        return RGFW_window_isInFocus(win) == RGFW_TRUE;
    }

    bool isMinimized(RGFW_window* win) {
        if (win == NULL)
            return true;

        return RGFW_window_isMinimized(win) == RGFW_TRUE;
    }

    bool isMaximized(RGFW_window* win) {
        if (win == NULL)
            return false;

        return RGFW_window_isMaximized(win) == RGFW_TRUE;
    }

    bool isKeyDown(RGFW_window* win, Key key) {
        if (win == NULL)
            return false;

        return RGFW_window_isKeyDown(win, key) == RGFW_TRUE;
    }

    bool isKeyPressed(RGFW_window* win, Key key) {
        if (win == NULL)
            return false;

        return RGFW_window_isKeyPressed(win, key) == RGFW_TRUE;
    }

    bool isKeyReleased(RGFW_window* win, Key key) {
        if (win == NULL)
            return false;

        return RGFW_window_isKeyReleased(win, key) == RGFW_TRUE;
    }

    bool isMouseButtonDown(RGFW_window* win, MouseButton button) {
        if (win == NULL)
            return false;

        return RGFW_window_isMouseDown(win, button) == RGFW_TRUE;
    }

    void getMousePos(RGFW_window* win, i32* x, i32* y) {
        if (win == NULL)
            return;

        RGFW_window_getMouse(win, x, y);
    }

    struct wl_display* getDisplay() {
        if (!started)
            return NULL;

        return RGFW_getDisplay_Wayland();
    }

    struct wl_surface* getSurface(RGFW_window* win) {
        if (win == NULL)
            return NULL;

        return RGFW_window_getWindow_Wayland(win);
    }

    const char* const* getRequiredInstanceExtensions(u32* count) {
        size_t total = 0;
        const char** extensions = RGFW_getRequiredInstanceExtensions_Vulkan(&total);

        if (count != NULL)
            *count = (u32)total;

        return extensions;
    }

    VkResult createSurface(RGFW_window* win, VkInstance instance, VkSurfaceKHR* surface) {
        if (win == NULL || surface == NULL)
            return VK_ERROR_INITIALIZATION_FAILED;

        return RGFW_window_createSurface_Vulkan(win, instance, surface);
    }

} // namespace rgfw