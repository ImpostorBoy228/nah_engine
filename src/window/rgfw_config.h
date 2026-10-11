#ifndef NAH_WINDOW_RGFW_CONFIG_H
#define NAH_WINDOW_RGFW_CONFIG_H

/* wayland only: no X11 backend, no XWayland fallback */
#define RGFW_WAYLAND
#define RGFW_NO_X11

/* RGFW hardcodes VK_USE_PLATFORM_XLIB_KHR on the wayland branch, so vulkan.h
   has to be pulled in first with only the wayland platform, otherwise vulkan.h
   drags <X11/Xlib.h> in and the whole "wayland only" thing is a lie */
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#define RGFW_VULKAN

/* C++ has no __STDC_VERSION__, so RGFW assumes C89 and pulls in a vsprintf-based
   snprintf shim that is both unused and unsafe */
#include <stdio.h>
#define RGFW_SNPRINTF snprintf

/* makes RGFWDEF extern on unix, so the inline-declaration-without-definition
   UB from RGFWDEF defaulting to `inline` is gone */
#define RGFW_IMPORT

#endif