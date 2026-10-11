#pragma once

#include "window/rgfw_shit.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

// vulkan shit: raii handles + device/swapchain/frame loop. no 2d knowledge,
// that lives in render/primitives.hpp. header only, every type is movable.
namespace casino {

    static const u32 MAX_FRAMES_IN_FLIGHT = 2;
    static const u32 MAX_SWAPCHAIN_IMAGES = 8;

    struct InitDesc {
        RGFW_window* win;
        const char* app_name;
        bool validation;
    };

    struct QueueFamilies {
        u32 graphics;
        u32 present;
    };

    inline bool check(VkResult result, const char* what) {
        if (result == VK_SUCCESS)
            return true;

        fprintf(stderr, "casino: %s failed (%d)\n", what, (int)result);
        return false;
    }

    inline VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user
        ) {
        (void)type;
        (void)user;
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            fprintf(stderr, "casino: %s\n", data->pMessage);

        return VK_FALSE;
    }

    inline bool hasLayer(const char* name) {
        u32 count = 0;
        vkEnumerateInstanceLayerProperties(&count, NULL);
        VkLayerProperties layers[64] = {};
        if (count > 64)
            count = 64;
        vkEnumerateInstanceLayerProperties(&count, layers);
        for (u32 i = 0; i < count; i++)
            if (strcmp(layers[i].layerName, name) == 0)
                return true;
        return false;
    }

    inline bool hasInstanceExtension(const char* name) {
        u32 count = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
        VkExtensionProperties exts[128] = {};
        if (count > 128)
            count = 128;
        vkEnumerateInstanceExtensionProperties(NULL, &count, exts);
        for (u32 i = 0; i < count; i++)
            if (strcmp(exts[i].extensionName, name) == 0)
                return true;
        return false;
    }

    inline bool supportsSwapchain(VkPhysicalDevice gpu) {
        u32 count = 0;
        vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL);
        VkExtensionProperties exts[128] = {};
        if (count > 128)
            count = 128;
        vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, exts);
        for (u32 i = 0; i < count; i++)
            if (strcmp(exts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                return true;
        return false;
    }

    inline QueueFamilies findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
        QueueFamilies out = { ~0u, ~0u };
        u32 count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, NULL);
        VkQueueFamilyProperties props[64] = {};
        if (count > 64)
            count = 64;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props);

        for (u32 i = 0; i < count; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present == VK_TRUE) {
                out.graphics = i;
                out.present = i;
                return out;
            }
        }
        for (u32 i = 0; i < count; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
            if (out.graphics == ~0u && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
                out.graphics = i;
            if (out.present == ~0u && present == VK_TRUE)
                out.present = i;
        }
        return out;
    }

    inline u32 findMemoryType(VkPhysicalDevice gpu, u32 mask, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mem = {};
        vkGetPhysicalDeviceMemoryProperties(gpu, &mem);
        for (u32 i = 0; i < mem.memoryTypeCount; i++) {
            if ((mask & (1u << i)) != 0 && (mem.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }
        return ~0u;
    }

    inline VkPhysicalDevice pickDevice(VkInstance instance, VkSurfaceKHR surface) {
        u32 count = 0;
        vkEnumeratePhysicalDevices(instance, &count, NULL);
        if (count == 0)
            return VK_NULL_HANDLE;

        VkPhysicalDevice gpus[8] = {};
        if (count > 8)
            count = 8;
        vkEnumeratePhysicalDevices(instance, &count, gpus);

        VkPhysicalDevice best = VK_NULL_HANDLE;
        int best_score = -1;
        for (u32 i = 0; i < count; i++) {
            QueueFamilies queues = findQueueFamilies(gpus[i], surface);
            if (queues.graphics == ~0u || queues.present == ~0u)
                continue;
            if (!supportsSwapchain(gpus[i]))
                continue;

            u32 formats = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpus[i], surface, &formats, NULL);
            u32 modes = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpus[i], surface, &modes, NULL);
            if (formats == 0 || modes == 0)
                continue;

            VkPhysicalDeviceProperties props = {};
            vkGetPhysicalDeviceProperties(gpus[i], &props);
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
            if (score > best_score) {
                best = gpus[i];
                best_score = score;
            }
        }
        return best;
    }

    // owns instance -> device -> swapchain -> frame loop. process wide singleton,
    // init() once, destructor tears it down. not copyable, not movable on purpose.
    class Device {
    public:
        RGFW_window* win = NULL;

        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;

        VkPhysicalDevice gpu = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkQueue present_queue = VK_NULL_HANDLE;
        u32 graphics_family = 0;
        u32 present_family = 0;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkSurfaceFormatKHR surface_format = {};
        VkExtent2D extent = {};
        u32 image_count = 0;
        VkImage images[MAX_SWAPCHAIN_IMAGES] = {};
        VkImageView views[MAX_SWAPCHAIN_IMAGES] = {};
        VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES] = {};
        VkRenderPass render_pass = VK_NULL_HANDLE;

        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer commands[MAX_FRAMES_IN_FLIGHT] = {};

        VkSemaphore image_available[MAX_FRAMES_IN_FLIGHT] = {};
        VkSemaphore render_finished[MAX_SWAPCHAIN_IMAGES] = {};
        VkFence fences[MAX_FRAMES_IN_FLIGHT] = {};

        float clear_color[4] = { 0.08f, 0.08f, 0.10f, 1.0f };
        u32 frame = 0;

    private:
        VkCommandBuffer active_cmd = VK_NULL_HANDLE;
        u32 active_index = 0;

        bool createInstance(const InitDesc& desc);
        bool createDevice();
        bool createSwapchain(VkSwapchainKHR old_swapchain);
        bool createRenderPass();
        bool createViews();
        bool createFramebuffers();
        bool createCommands();
        bool createSync();

    public:
        Device() = default;
        ~Device() { shutdown(); }
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        bool init(const InitDesc& desc); // BLACKBOX
        void shutdown();
        void recreateSwapchain(); // BLACKBOX

        bool beginFrame(); // BLACKBOX
        void endFrame(); // BLACKBOX

        u32 frameSlot() const { return frame % MAX_FRAMES_IN_FLIGHT; }
        VkCommandBuffer currentCommand() const { return active_cmd; }
        u32 currentIndex() const { return active_index; }
        VkExtent2D currentExtent() const { return extent; }
        VkRenderPass currentRenderPass() const { return render_pass; }
    };

    inline bool Device::createInstance(const InitDesc& desc) {
        bool want_validation = desc.validation && hasLayer("VK_LAYER_KHRONOS_validation");
        if (desc.validation && !want_validation)
            fprintf(stderr, "casino: validation layer missing, running without it\n");

        VkApplicationInfo app = {};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = desc.app_name ? desc.app_name : "nah_engine";
        app.pEngineName = "nah_engine";
        app.apiVersion = VK_API_VERSION_1_2;

        const char* extensions[16] = {};
        u32 extension_count = 0;
        u32 rgfw_count = 0;
        const char* const* rgfw_exts = rgfw::getRequiredInstanceExtensions(&rgfw_count);
        for (u32 i = 0; i < rgfw_count && extension_count < 16; i++)
            extensions[extension_count++] = rgfw_exts[i];
        if (want_validation && hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

        const char* layers[1] = { "VK_LAYER_KHRONOS_validation" };

        VkInstanceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = extension_count;
        info.ppEnabledExtensionNames = extensions;
        info.enabledLayerCount = want_validation ? 1 : 0;
        info.ppEnabledLayerNames = layers;

        if (!check(vkCreateInstance(&info, NULL, &instance), "vkCreateInstance"))
            return false;
        if (!want_validation || !hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            return true;

        VkDebugUtilsMessengerCreateInfoEXT messenger_info = {};
        messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger_info.pfnUserCallback = onDebugMessage;

        PFN_vkCreateDebugUtilsMessengerEXT create =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create == NULL)
            return true;
        return check(create(instance, &messenger_info, NULL, &messenger), "vkCreateDebugUtilsMessengerEXT");
    }

    inline bool Device::createDevice() {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queues[2] = {};
        queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[0].queueFamilyIndex = graphics_family;
        queues[0].queueCount = 1;
        queues[0].pQueuePriorities = &priority;
        u32 queue_count = 1;
        if (present_family != graphics_family) {
            queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queues[1].queueFamilyIndex = present_family;
            queues[1].queueCount = 1;
            queues[1].pQueuePriorities = &priority;
            queue_count = 2;
        }

        const char* extensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkPhysicalDeviceFeatures features = {};
        VkDeviceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = queue_count;
        info.pQueueCreateInfos = queues;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        info.pEnabledFeatures = &features;

        if (!check(vkCreateDevice(gpu, &info, NULL, &device), "vkCreateDevice"))
            return false;
        vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
        vkGetDeviceQueue(device, present_family, 0, &present_queue);
        return true;
    }

    inline bool Device::createSwapchain(VkSwapchainKHR old_swapchain) {
        VkSurfaceCapabilitiesKHR caps = {};
        if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps), "surface caps"))
            return false;

        u32 format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
        VkSurfaceFormatKHR formats[64] = {};
        if (format_count > 64)
            format_count = 64;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats);

        VkSurfaceFormatKHR chosen = formats[0];
        for (u32 i = 0; i < format_count; i++) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = formats[i];
                break;
            }
        }

        VkExtent2D new_extent = caps.currentExtent;
        if (new_extent.width == 0xFFFFFFFFu) {
            i32 width = 0;
            i32 height = 0;
            rgfw::getSize(win, &width, &height);
            new_extent.width = width > 0 ? (u32)width : 0;
            new_extent.height = height > 0 ? (u32)height : 0;
            if (new_extent.width < caps.minImageExtent.width) new_extent.width = caps.minImageExtent.width;
            if (new_extent.height < caps.minImageExtent.height) new_extent.height = caps.minImageExtent.height;
            if (new_extent.width > caps.maxImageExtent.width) new_extent.width = caps.maxImageExtent.width;
            if (new_extent.height > caps.maxImageExtent.height) new_extent.height = caps.maxImageExtent.height;
        }
        if (new_extent.width == 0 || new_extent.height == 0)
            return false;

        u32 want_images = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && want_images > caps.maxImageCount)
            want_images = caps.maxImageCount;
        if (want_images > MAX_SWAPCHAIN_IMAGES)
            want_images = MAX_SWAPCHAIN_IMAGES;

        VkSwapchainCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = want_images;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = new_extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (graphics_family != present_family) {
            u32 families[2] = { graphics_family, present_family };
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = families;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old_swapchain;

        VkSwapchainKHR next = VK_NULL_HANDLE;
        if (!check(vkCreateSwapchainKHR(device, &info, NULL, &next), "vkCreateSwapchainKHR"))
            return false;
        if (old_swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device, old_swapchain, NULL);

        swapchain = next;
        surface_format = chosen;
        extent = new_extent;

        u32 actual = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &actual, NULL);
        image_count = actual < MAX_SWAPCHAIN_IMAGES ? actual : MAX_SWAPCHAIN_IMAGES;
        vkGetSwapchainImagesKHR(device, swapchain, &actual, images);
        return true;
    }

    inline bool Device::createRenderPass() {
        VkAttachmentDescription attachment = {};
        attachment.format = surface_format.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference reference = {};
        reference.attachment = 0;
        reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        return check(vkCreateRenderPass(device, &info, NULL, &render_pass), "vkCreateRenderPass");
    }

    inline bool Device::createViews() {
        for (u32 i = 0; i < image_count; i++) {
            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = images[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = surface_format.format;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.layerCount = 1;
            if (!check(vkCreateImageView(device, &info, NULL, &views[i]), "vkCreateImageView"))
                return false;
        }
        return true;
    }

    inline bool Device::createFramebuffers() {
        for (u32 i = 0; i < image_count; i++) {
            VkImageView attachments[1] = { views[i] };
            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = render_pass;
            info.attachmentCount = 1;
            info.pAttachments = attachments;
            info.width = extent.width;
            info.height = extent.height;
            info.layers = 1;
            if (!check(vkCreateFramebuffer(device, &info, NULL, &framebuffers[i]), "vkCreateFramebuffer"))
                return false;
        }
        return true;
    }

    inline bool Device::createCommands() {
        VkCommandPoolCreateInfo pool = {};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = graphics_family;
        if (!check(vkCreateCommandPool(device, &pool, NULL, &command_pool), "vkCreateCommandPool"))
            return false;

        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = command_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        return check(vkAllocateCommandBuffers(device, &info, commands), "vkAllocateCommandBuffers");
    }

    inline bool Device::createSync() {
        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence = {};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (!check(vkCreateSemaphore(device, &semaphore, NULL, &image_available[i]), "sem"))
                return false;
            if (!check(vkCreateFence(device, &fence, NULL, &fences[i]), "fence"))
                return false;
        }
        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (!check(vkCreateSemaphore(device, &semaphore, NULL, &render_finished[i]), "sem"))
                return false;
        }
        return true;
    }

    inline bool Device::init(const InitDesc& desc) { // BLACKBOX
        if (desc.win == NULL)
            return false;
        shutdown();
        win = desc.win;

        if (!createInstance(desc)) { shutdown(); return false; }
        if (!check(rgfw::createSurface(win, instance, &surface), "rgfw::createSurface")) { shutdown(); return false; }

        gpu = pickDevice(instance, surface);
        if (gpu == VK_NULL_HANDLE) {
            fprintf(stderr, "casino: no vulkan device with swapchain support\n");
            shutdown();
            return false;
        }

        QueueFamilies queues = findQueueFamilies(gpu, surface);
        graphics_family = queues.graphics;
        present_family = queues.present;

        bool ok = createDevice() && createSwapchain(VK_NULL_HANDLE) && createRenderPass() &&
            createViews() && createFramebuffers() && createCommands() && createSync();
        if (!ok) { shutdown(); return false; }

        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(gpu, &props);
        printf("casino: %s, %ux%u, %u images\n", props.deviceName, extent.width, extent.height, image_count);
        return true;
    }

    inline void Device::shutdown() {
        if (device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device);

        for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (image_available[i]) vkDestroySemaphore(device, image_available[i], NULL);
            if (fences[i]) vkDestroyFence(device, fences[i], NULL);
        }
        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++)
            if (render_finished[i]) vkDestroySemaphore(device, render_finished[i], NULL);

        if (command_pool) vkDestroyCommandPool(device, command_pool, NULL);
        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (framebuffers[i]) vkDestroyFramebuffer(device, framebuffers[i], NULL);
            if (views[i]) vkDestroyImageView(device, views[i], NULL);
        }
        if (render_pass) vkDestroyRenderPass(device, render_pass, NULL);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
        if (device) vkDestroyDevice(device, NULL);

        if (messenger && instance) {
            PFN_vkDestroyDebugUtilsMessengerEXT destroy =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance, messenger, NULL);
        }
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, NULL);
        if (instance) vkDestroyInstance(instance, NULL);

        win = NULL;
        instance = VK_NULL_HANDLE;
        messenger = VK_NULL_HANDLE;
        surface = VK_NULL_HANDLE;
        gpu = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        graphics_queue = VK_NULL_HANDLE;
        present_queue = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        command_pool = VK_NULL_HANDLE;
        image_count = 0;
        frame = 0;
        memset(images, 0, sizeof(images));
        memset(views, 0, sizeof(views));
        memset(framebuffers, 0, sizeof(framebuffers));
        memset(commands, 0, sizeof(commands));
        memset(image_available, 0, sizeof(image_available));
        memset(render_finished, 0, sizeof(render_finished));
        memset(fences, 0, sizeof(fences));
    }

    inline void Device::recreateSwapchain() { // BLACKBOX
        if (device == VK_NULL_HANDLE)
            return;
        i32 width = 0, height = 0;
        rgfw::getSize(win, &width, &height);
        if (width == 0 || height == 0)
            return;

        vkDeviceWaitIdle(device);
        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (framebuffers[i]) vkDestroyFramebuffer(device, framebuffers[i], NULL);
            if (views[i]) vkDestroyImageView(device, views[i], NULL);
            framebuffers[i] = VK_NULL_HANDLE;
            views[i] = VK_NULL_HANDLE;
        }

        VkFormat old_format = surface_format.format;
        if (!createSwapchain(swapchain))
            return; // old swapchain still alive, retry next frame
        if (surface_format.format != old_format) {
            vkDestroyRenderPass(device, render_pass, NULL);
            if (!createRenderPass())
                return;
        }
        if (!createViews() || !createFramebuffers())
            return;
    }

    inline bool Device::beginFrame() { // BLACKBOX
        if (device == VK_NULL_HANDLE)
            return false;

        u32 slot = frameSlot();
        vkWaitForFences(device, 1, &fences[slot], VK_TRUE, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            image_available[slot], VK_NULL_HANDLE, &active_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return false;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "casino: acquire failed (%d)\n", (int)result);
            return false;
        }

        active_cmd = commands[slot];
        vkResetCommandBuffer(active_cmd, 0);
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!check(vkBeginCommandBuffer(active_cmd, &begin), "vkBeginCommandBuffer"))
            return false;

        VkClearValue clear = {};
        for (int i = 0; i < 4; i++) clear.color.float32[i] = clear_color[i];
        VkRenderPassBeginInfo pass = {};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffers[active_index];
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(active_cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)extent.width;
        viewport.height = (float)extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(active_cmd, 0, 1, &viewport);
        VkRect2D scissor = { { 0, 0 }, extent };
        vkCmdSetScissor(active_cmd, 0, 1, &scissor);
        return true;
    }

    inline void Device::endFrame() { // BLACKBOX
        if (active_cmd == VK_NULL_HANDLE)
            return;
        vkCmdEndRenderPass(active_cmd);
        if (!check(vkEndCommandBuffer(active_cmd), "vkEndCommandBuffer"))
            return;

        u32 slot = frameSlot();
        vkResetFences(device, 1, &fences[slot]);

        VkSemaphore wait[1] = { image_available[slot] };
        VkPipelineStageFlags stages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkCommandBuffer cmds[1] = { active_cmd };
        VkSemaphore signal[1] = { render_finished[active_index] };

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = wait;
        submit.pWaitDstStageMask = stages;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = cmds;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = signal;
        if (!check(vkQueueSubmit(graphics_queue, 1, &submit, fences[slot]), "vkQueueSubmit"))
            return;

        VkSwapchainKHR swaps[1] = { swapchain };
        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = signal;
        present.swapchainCount = 1;
        present.pSwapchains = swaps;
        present.pImageIndices = &active_index;

        VkResult res = vkQueuePresentKHR(present_queue, &present);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
            recreateSwapchain();
        else if (res != VK_SUCCESS)
            fprintf(stderr, "casino: present failed (%d)\n", (int)res);

        active_cmd = VK_NULL_HANDLE;
        frame += 1;
    }

    // ------------------------------------------------- raii resource wrappers

    // spirv words are uint32 but the .bin.h gives bytes, copy to aligned storage
    static uint32_t* alignedSpirv(const void* bytes, size_t nbytes, uint32_t* out_words) {
        uint32_t* buf = (uint32_t*)malloc(nbytes);
        memcpy(buf, bytes, nbytes);
        *out_words = (uint32_t)(nbytes / 4);
        return buf;
    }

    class ShaderModule {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkShaderModule module = VK_NULL_HANDLE;

        ShaderModule() = default;
        ShaderModule(VkDevice dev, const void* spirv, size_t bytes) { create(dev, spirv, bytes); }
        ~ShaderModule() { destroy(); }
        ShaderModule(const ShaderModule&) = delete;
        ShaderModule& operator=(const ShaderModule&) = delete;
        ShaderModule(ShaderModule&& o) { *this = static_cast<ShaderModule&&>(o); }
        ShaderModule& operator=(ShaderModule&& o) {
            if (this != &o) {
                destroy();
                device = o.device; module = o.module;
                o.device = VK_NULL_HANDLE; o.module = VK_NULL_HANDLE;
            }
            return *this;
        }

        bool create(VkDevice dev, const void* spirv, size_t bytes) { // BLACKBOX
            destroy();
            device = dev;
            uint32_t words = 0;
            uint32_t* aligned = alignedSpirv(spirv, bytes, &words);
            VkShaderModuleCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = bytes;
            info.pCode = aligned;
            bool ok = check(vkCreateShaderModule(device, &info, NULL, &module), "vkCreateShaderModule");
            free(aligned);
            return ok;
        }

        void destroy() {
            if (module) vkDestroyShaderModule(device, module, NULL);
            module = VK_NULL_HANDLE;
        }
    };

    class GpuBuffer {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = NULL;
        size_t size = 0;

        GpuBuffer() = default;
        ~GpuBuffer() { destroy(); }
        GpuBuffer(const GpuBuffer&) = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;
        GpuBuffer(GpuBuffer&& o) { *this = static_cast<GpuBuffer&&>(o); }
        GpuBuffer& operator=(GpuBuffer&& o) {
            if (this != &o) {
                destroy();
                device = o.device; buffer = o.buffer; memory = o.memory; mapped = o.mapped; size = o.size;
                o.device = VK_NULL_HANDLE; o.buffer = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE;
                o.mapped = NULL; o.size = 0;
            }
            return *this;
        }

        bool create(VkDevice dev, VkPhysicalDevice gpu, size_t bytes,
            VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props) { // BLACKBOX
            destroy();
            device = dev;
            size = bytes;
            VkBufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size = bytes;
            info.usage = usage;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (!check(vkCreateBuffer(device, &info, NULL, &buffer), "vkCreateBuffer"))
                return false;

            VkMemoryRequirements req = {};
            vkGetBufferMemoryRequirements(device, buffer, &req);
            u32 type = findMemoryType(gpu, req.memoryTypeBits, mem_props);
            if (type == ~0u)
                return false;

            VkMemoryAllocateInfo alloc = {};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = req.size;
            alloc.memoryTypeIndex = type;
            if (!check(vkAllocateMemory(device, &alloc, NULL, &memory), "vkAllocateMemory(buffer)"))
                return false;
            vkBindBufferMemory(device, buffer, memory, 0);

            if ((mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
                vkMapMemory(device, memory, 0, bytes, 0, &mapped);
            return true;
        }

        void destroy() {
            if (mapped) { vkUnmapMemory(device, memory); mapped = NULL; }
            if (buffer) vkDestroyBuffer(device, buffer, NULL);
            if (memory) vkFreeMemory(device, memory, NULL);
            buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; size = 0;
        }
    };

    class Texture {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        u32 width = 0;
        u32 height = 0;

        Texture() = default;
        ~Texture() { destroy(); }
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& o) { *this = static_cast<Texture&&>(o); }
        Texture& operator=(Texture&& o) {
            if (this != &o) {
                destroy();
                device = o.device; image = o.image; memory = o.memory; view = o.view;
                sampler = o.sampler; set = o.set; width = o.width; height = o.height;
                o.device = VK_NULL_HANDLE; o.image = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE;
                o.view = VK_NULL_HANDLE; o.sampler = VK_NULL_HANDLE; o.set = VK_NULL_HANDLE;
                o.width = o.height = 0;
            }
            return *this;
        }

        // rgba8 pixels uploaded through a staging buffer on the graphics queue
        bool create(const Device& dev, const uint8_t* rgba, u32 w, u32 h,
            VkDescriptorPool pool, VkDescriptorSetLayout layout) { // BLACKBOX
            destroy();
            device = dev.device;
            width = w;
            height = h;
            size_t bytes = (size_t)w * h * 4;

            GpuBuffer staging;
            if (!staging.create(device, dev.gpu, bytes,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return false;
            memcpy(staging.mapped, rgba, bytes);

            VkImageCreateInfo img = {};
            img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            img.imageType = VK_IMAGE_TYPE_2D;
            img.format = VK_FORMAT_R8G8B8A8_SRGB;
            img.extent = { w, h, 1 };
            img.mipLevels = 1;
            img.arrayLayers = 1;
            img.samples = VK_SAMPLE_COUNT_1_BIT;
            img.tiling = VK_IMAGE_TILING_OPTIMAL;
            img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (!check(vkCreateImage(device, &img, NULL, &image), "vkCreateImage"))
                return false;

            VkMemoryRequirements req = {};
            vkGetImageMemoryRequirements(device, image, &req);
            u32 type = findMemoryType(dev.gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkMemoryAllocateInfo alloc = {};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = req.size;
            alloc.memoryTypeIndex = type;
            if (!check(vkAllocateMemory(device, &alloc, NULL, &memory), "vkAllocateMemory(image)"))
                return false;
            vkBindImageMemory(device, image, memory, 0);

            // one shot copy on the graphics queue, then to shader read only
            VkCommandBuffer cmd = beginOneShot(dev);
            transition(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { w, h, 1 };
            vkCmdCopyBufferToImage(cmd, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            endOneShot(dev, cmd);

            VkImageViewCreateInfo vinfo = {};
            vinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vinfo.image = image;
            vinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vinfo.format = VK_FORMAT_R8G8B8A8_SRGB;
            vinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vinfo.subresourceRange.levelCount = 1;
            vinfo.subresourceRange.layerCount = 1;
            if (!check(vkCreateImageView(device, &vinfo, NULL, &view), "vkCreateImageView(texture)"))
                return false;

            VkSamplerCreateInfo sinfo = {};
            sinfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sinfo.magFilter = VK_FILTER_LINEAR;
            sinfo.minFilter = VK_FILTER_LINEAR;
            sinfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sinfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sinfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sinfo.maxLod = 1.0f;
            if (!check(vkCreateSampler(device, &sinfo, NULL, &sampler), "vkCreateSampler"))
                return false;

            VkDescriptorSetAllocateInfo dalloc = {};
            dalloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dalloc.descriptorPool = pool;
            dalloc.descriptorSetCount = 1;
            dalloc.pSetLayouts = &layout;
            if (!check(vkAllocateDescriptorSets(device, &dalloc, &set), "vkAllocateDescriptorSets"))
                return false;

            VkDescriptorImageInfo iinfo = {};
            iinfo.sampler = sampler;
            iinfo.imageView = view;
            iinfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &iinfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
            return true;
        }

        void destroy() {
            if (image) vkDestroyImage(device, image, NULL);
            if (memory) vkFreeMemory(device, memory, NULL);
            if (view) vkDestroyImageView(device, view, NULL);
            if (sampler) vkDestroySampler(device, sampler, NULL);
            image = VK_NULL_HANDLE; memory = VK_NULL_HANDLE;
            view = VK_NULL_HANDLE; sampler = VK_NULL_HANDLE; set = VK_NULL_HANDLE;
            width = height = 0;
        }

    private:
        static VkCommandBuffer beginOneShot(const Device& dev) {
            VkCommandBufferAllocateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = dev.command_pool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(dev.device, &info, &cmd);
            VkCommandBufferBeginInfo begin = {};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin);
            return cmd;
        }

        static void endOneShot(const Device& dev, VkCommandBuffer cmd) {
            vkEndCommandBuffer(cmd);
            VkSubmitInfo submit = {};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;
            vkQueueSubmit(dev.graphics_queue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(dev.graphics_queue);
        }

        static void transition(VkCommandBuffer cmd, VkImage img,
            VkImageLayout from, VkImageLayout to) {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = from;
            barrier.newLayout = to;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = img;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &barrier);
        }
    };

    // 2d graphics pipeline against the device render pass. expects vertices of
    // layout { vec2 pos, vec4 color, vec2 uv } and one combined-image-sampler set.
    class Pipeline {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        Pipeline() = default;
        ~Pipeline() { destroy(); }
        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;
        Pipeline(Pipeline&& o) { *this = static_cast<Pipeline&&>(o); }
        Pipeline& operator=(Pipeline&& o) {
            if (this != &o) {
                destroy();
                device = o.device; layout = o.layout; pipeline = o.pipeline;
                o.device = VK_NULL_HANDLE; o.layout = VK_NULL_HANDLE; o.pipeline = VK_NULL_HANDLE;
            }
            return *this;
        }

        bool create(const Device& dev, const void* vert, size_t vert_bytes,
            const void* frag, size_t frag_bytes, VkDescriptorSetLayout set_layout) { // BLACKBOX
            destroy();
            device = dev.device;

            ShaderModule v(dev.device, vert, vert_bytes);
            ShaderModule f(dev.device, frag, frag_bytes);
            if (!v.module || !f.module)
                return false;

            VkPipelineShaderStageCreateInfo stages[2] = {};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = v.module;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = f.module;
            stages[1].pName = "main";

            VkVertexInputBindingDescription binding = {};
            binding.binding = 0;
            binding.stride = 32; // vec2 + vec4 + vec2 floats
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            VkVertexInputAttributeDescription attrs[3] = {};
            attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
            attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8 };
            attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, 24 };

            VkPipelineVertexInputStateCreateInfo vertex_input = {};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &binding;
            vertex_input.vertexAttributeDescriptionCount = 3;
            vertex_input.pVertexAttributeDescriptions = attrs;

            VkPipelineInputAssemblyStateCreateInfo assembly = {};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewport = {};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo raster = {};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample = {};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState blend_attach = {};
            blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend_attach.blendEnable = VK_TRUE;
            blend_attach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attach.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attach.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo blend = {};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &blend_attach;

            VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamic = {};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamics;

            VkPipelineLayoutCreateInfo layout_info = {};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts = &set_layout;
            if (!check(vkCreatePipelineLayout(device, &layout_info, NULL, &layout), "vkCreatePipelineLayout"))
                return false;

            VkGraphicsPipelineCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = 2;
            info.pStages = stages;
            info.pVertexInputState = &vertex_input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = layout;
            info.renderPass = dev.render_pass;
            info.subpass = 0;
            return check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline),
                "vkCreateGraphicsPipelines");
        }

        void destroy() {
            if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
            if (layout) vkDestroyPipelineLayout(device, layout, NULL);
            pipeline = VK_NULL_HANDLE; layout = VK_NULL_HANDLE;
        }
    };

} // namespace casino
