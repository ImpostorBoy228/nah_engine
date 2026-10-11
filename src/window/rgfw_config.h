#ifndef NAH_WINDOW_RGFW_CONFIG_H
#define NAH_WINDOW_RGFW_CONFIG_H

// wayland only: no X11 backend, no XWayland fallback
#define RGFW_WAYLAND
#define RGFW_NO_X11

// RGFW hardcodes VK_USE_PLATFORM_XLIB_KHR on the wayland branch
// pull vulkan.h in first with only the wayland platform, or X11/Xlib.h sneaks in
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#define RGFW_VULKAN

// RGFW thinks it is C89 without __STDC_VERSION__ and shims snprintf with vsprintf
#include <stdio.h>
#define RGFW_SNPRINTF snprintf

// RGFWDEF stays extern, no "declared inline, never defined" UB at -O2
#define RGFW_IMPORT

#endif
