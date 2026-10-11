#pragma once

#include "window/rgfw_shit.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

// basic vulkan boilerplate: instance -> device -> swapchain -> frames -> present
// header only, one global context, no pipeline - the clear pass is the frame
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

    // every vulkan handle the renderer owns, {} zeroes all of it
    struct Context {
        RGFW_window* win;

        VkInstance instance;
        VkDebugUtilsMessengerEXT messenger;
        VkSurfaceKHR surface;

        VkPhysicalDevice gpu;
        VkDevice device;
        VkQueue graphics_queue;
        VkQueue present_queue;
        u32 graphics_family;
        u32 present_family;

        VkSwapchainKHR swapchain;
        VkSurfaceFormatKHR surface_format;
        VkExtent2D extent;
        u32 image_count;
        VkImage images[MAX_SWAPCHAIN_IMAGES];
        VkImageView views[MAX_SWAPCHAIN_IMAGES];
        VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
        VkRenderPass render_pass;

        VkCommandPool command_pool;
        VkCommandBuffer commands[MAX_FRAMES_IN_FLIGHT];

        VkSemaphore image_available[MAX_FRAMES_IN_FLIGHT];
        VkSemaphore render_finished[MAX_SWAPCHAIN_IMAGES];
        VkFence fences[MAX_FRAMES_IN_FLIGHT];

        float clear_color[4];
        bool animate;
        u32 frame;
    };

    inline Context ctx = {};

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

        for (u32 i = 0; i < count; i++) {
            if (strcmp(layers[i].layerName, name) == 0)
                return true;
        }

        return false;
    }

    inline bool hasInstanceExtension(const char* name) {
        u32 count = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);

        VkExtensionProperties extensions[128] = {};
        if (count > 128)
            count = 128;
        vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);

        for (u32 i = 0; i < count; i++) {
            if (strcmp(extensions[i].extensionName, name) == 0)
                return true;
        }

        return false;
    }

    inline bool supportsSwapchain(VkPhysicalDevice gpu) {
        u32 count = 0;
        vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL);

        VkExtensionProperties extensions[128] = {};
        if (count > 128)
            count = 128;
        vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, extensions);

        for (u32 i = 0; i < count; i++) {
            if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                return true;
        }

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

        // one family that does graphics and present is the easy case
        for (u32 i = 0; i < count; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);

            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present == VK_TRUE) {
                out.graphics = i;
                out.present = i;
                return out;
            }
        }

        // otherwise remember the first of each kind
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

    inline VkPhysicalDevice pickDevice(VkSurfaceKHR surface) {
        u32 count = 0;
        vkEnumeratePhysicalDevices(ctx.instance, &count, NULL);
        if (count == 0)
            return VK_NULL_HANDLE;

        VkPhysicalDevice gpus[8] = {};
        if (count > 8)
            count = 8;
        vkEnumeratePhysicalDevices(ctx.instance, &count, gpus);

        VkPhysicalDevice best = VK_NULL_HANDLE;
        int best_score = -1;

        for (u32 i = 0; i < count; i++) {
            QueueFamilies queues = findQueueFamilies(gpus[i], surface);
            if (queues.graphics == ~0u || queues.present == ~0u)
                continue;
            if (!supportsSwapchain(gpus[i]))
                continue;

            u32 format_count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpus[i], surface, &format_count, NULL);
            u32 mode_count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpus[i], surface, &mode_count, NULL);
            if (format_count == 0 || mode_count == 0)
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

    inline bool createInstance(const InitDesc& desc) {
        bool want_validation = desc.validation && hasLayer("VK_LAYER_KHRONOS_validation");
        if (desc.validation && !want_validation)
            fprintf(stderr, "casino: validation layer missing, running without it\n");

        VkApplicationInfo app = {};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = desc.app_name != NULL ? desc.app_name : "nah_engine";
        app.pEngineName = "nah_engine";
        app.apiVersion = VK_API_VERSION_1_2;

        const char* extensions[16] = {};
        u32 extension_count = 0;

        u32 rgfw_count = 0;
        const char* const* rgfw_extensions = rgfw::getRequiredInstanceExtensions(&rgfw_count);
        for (u32 i = 0; i < rgfw_count && extension_count < 16; i++)
            extensions[extension_count++] = rgfw_extensions[i];

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

        if (!check(vkCreateInstance(&info, NULL, &ctx.instance), "vkCreateInstance"))
            return false;

        if (!want_validation || !hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            return true;

        VkDebugUtilsMessengerCreateInfoEXT messenger = {};
        messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = onDebugMessage;

        PFN_vkCreateDebugUtilsMessengerEXT create =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create == NULL)
            return true;

        return check(
            create(ctx.instance, &messenger, NULL, &ctx.messenger),
            "vkCreateDebugUtilsMessengerEXT");
    }

    inline bool createDevice() {
        float priority = 1.0f;

        VkDeviceQueueCreateInfo queues[2] = {};
        queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[0].queueFamilyIndex = ctx.graphics_family;
        queues[0].queueCount = 1;
        queues[0].pQueuePriorities = &priority;

        u32 queue_count = 1;
        if (ctx.present_family != ctx.graphics_family) {
            queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queues[1].queueFamilyIndex = ctx.present_family;
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

        if (!check(vkCreateDevice(ctx.gpu, &info, NULL, &ctx.device), "vkCreateDevice"))
            return false;

        vkGetDeviceQueue(ctx.device, ctx.graphics_family, 0, &ctx.graphics_queue);
        vkGetDeviceQueue(ctx.device, ctx.present_family, 0, &ctx.present_queue);
        return true;
    }

    inline bool createSwapchain(VkSwapchainKHR old_swapchain) {
        VkSurfaceCapabilitiesKHR caps = {};
        if (!check(
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.gpu, ctx.surface, &caps),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
            return false;

        u32 format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.gpu, ctx.surface, &format_count, NULL);
        VkSurfaceFormatKHR formats[64] = {};
        if (format_count > 64)
            format_count = 64;
        vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.gpu, ctx.surface, &format_count, formats);

        // sRGB when the surface offers it, whatever is first otherwise
        VkSurfaceFormatKHR chosen = formats[0];
        for (u32 i = 0; i < format_count; i++) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = formats[i];
                break;
            }
        }

        // FIFO is guaranteed to exist on every conformant driver, vsync for free
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == 0xFFFFFFFFu) {
            i32 width = 0;
            i32 height = 0;
            rgfw::getSize(ctx.win, &width, &height);

            extent.width = width > 0 ? (u32)width : 0;
            extent.height = height > 0 ? (u32)height : 0;

            if (extent.width < caps.minImageExtent.width)
                extent.width = caps.minImageExtent.width;
            if (extent.height < caps.minImageExtent.height)
                extent.height = caps.minImageExtent.height;
            if (extent.width > caps.maxImageExtent.width)
                extent.width = caps.maxImageExtent.width;
            if (extent.height > caps.maxImageExtent.height)
                extent.height = caps.maxImageExtent.height;
        }

        // minimized window, caller retries later
        if (extent.width == 0 || extent.height == 0)
            return false;

        u32 image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
            image_count = caps.maxImageCount;
        if (image_count > MAX_SWAPCHAIN_IMAGES)
            image_count = MAX_SWAPCHAIN_IMAGES;

        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ((caps.supportedCompositeAlpha & alpha) == 0) {
            static const VkCompositeAlphaFlagBitsKHR candidates[] = {
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            };
            for (u32 i = 0; i < 3; i++) {
                if (caps.supportedCompositeAlpha & candidates[i]) {
                    alpha = candidates[i];
                    break;
                }
            }
        }

        VkSwapchainCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = ctx.surface;
        info.minImageCount = image_count;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (ctx.graphics_family != ctx.present_family) {
            u32 families[2] = { ctx.graphics_family, ctx.present_family };
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = families;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = alpha;
        info.presentMode = present_mode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old_swapchain;

        VkSwapchainKHR next = VK_NULL_HANDLE;
        if (!check(vkCreateSwapchainKHR(ctx.device, &info, NULL, &next), "vkCreateSwapchainKHR"))
            return false;

        if (old_swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(ctx.device, old_swapchain, NULL);

        ctx.swapchain = next;
        ctx.surface_format = chosen;
        ctx.extent = extent;

        u32 actual = 0;
        vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &actual, NULL);
        // drivers hand back what was asked, MAX is far above minImageCount + 1
        ctx.image_count = actual < MAX_SWAPCHAIN_IMAGES ? actual : MAX_SWAPCHAIN_IMAGES;
        vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &actual, ctx.images);
        return true;
    }



    inline bool createRenderPass() {
        VkAttachmentDescription attachment = {};
        attachment.format = ctx.surface_format.format;
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

        return check(vkCreateRenderPass(ctx.device, &info, NULL, &ctx.render_pass), "vkCreateRenderPass");
    }

    inline bool createViews() {
        for (u32 i = 0; i < ctx.image_count; i++) {
            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = ctx.images[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = ctx.surface_format.format;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;

            if (!check(vkCreateImageView(ctx.device, &info, NULL, &ctx.views[i]), "vkCreateImageView"))
                return false;
        }

        return true;
    }

    inline bool createFramebuffers() {
        for (u32 i = 0; i < ctx.image_count; i++) {
            VkImageView attachments[1] = { ctx.views[i] };

            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = ctx.render_pass;
            info.attachmentCount = 1;
            info.pAttachments = attachments;
            info.width = ctx.extent.width;
            info.height = ctx.extent.height;
            info.layers = 1;

            if (!check(vkCreateFramebuffer(ctx.device, &info, NULL, &ctx.framebuffers[i]), "vkCreateFramebuffer"))
                return false;
        }

        return true;
    }

    inline bool createCommands() {
        VkCommandPoolCreateInfo pool = {};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = ctx.graphics_family;

        if (!check(vkCreateCommandPool(ctx.device, &pool, NULL, &ctx.command_pool), "vkCreateCommandPool"))
            return false;

        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = ctx.command_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

        return check(
            vkAllocateCommandBuffers(ctx.device, &info, ctx.commands),
            "vkAllocateCommandBuffers");
    }

    inline bool createSync() {
        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence = {};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (!check(vkCreateSemaphore(ctx.device, &semaphore, NULL, &ctx.image_available[i]), "vkCreateSemaphore"))
                return false;
            if (!check(vkCreateFence(ctx.device, &fence, NULL, &ctx.fences[i]), "vkCreateFence"))
                return false;
        }

        // indexed by swapchain image in drawFrame, not by frame slot
        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (!check(vkCreateSemaphore(ctx.device, &semaphore, NULL, &ctx.render_finished[i]), "vkCreateSemaphore"))
                return false;
        }

        return true;
    }

    inline bool recordCommands(VkCommandBuffer command, u32 image) {
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer"))
            return false;

        // slow pulse so a live frame is visible without a screenshot
        float t = (float)ctx.frame * 0.02f;
        VkClearValue clear = {};
        clear.color.float32[0] = 0.5f + 0.5f * sinf(t);
        clear.color.float32[1] = 0.5f + 0.5f * sinf(t + 2.0943951f);
        clear.color.float32[2] = 0.5f + 0.5f * sinf(t + 4.1887902f);
        clear.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo pass = {};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = ctx.render_pass;
        pass.framebuffer = ctx.framebuffers[image];
        pass.renderArea.offset.x = 0;
        pass.renderArea.offset.y = 0;
        pass.renderArea.extent = ctx.extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;

        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(command);

        return check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    }

    inline void recreateSwapchain() { // BLACKBOX
        if (ctx.device == VK_NULL_HANDLE)
            return;

        i32 width = 0;
        i32 height = 0;
        rgfw::getSize(ctx.win, &width, &height);
        if (width == 0 || height == 0)
            return;

        vkDeviceWaitIdle(ctx.device);

        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (ctx.framebuffers[i] != VK_NULL_HANDLE)
                vkDestroyFramebuffer(ctx.device, ctx.framebuffers[i], NULL);
            if (ctx.views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(ctx.device, ctx.views[i], NULL);
            ctx.framebuffers[i] = VK_NULL_HANDLE;
            ctx.views[i] = VK_NULL_HANDLE;
        }

        VkFormat old_format = ctx.surface_format.format;
        if (!createSwapchain(ctx.swapchain)) {
            // old swapchain is still alive, the next frame retries
            return;
        }

        if (ctx.surface_format.format != old_format) {
            vkDestroyRenderPass(ctx.device, ctx.render_pass, NULL);
            if (!createRenderPass())
                return;
        }

        if (!createViews() || !createFramebuffers())
            return;
    }

    inline void drawFrame() { // BLACKBOX
        if (ctx.device == VK_NULL_HANDLE || ctx.render_pass == VK_NULL_HANDLE)
            return;

        u32 frame = ctx.frame % MAX_FRAMES_IN_FLIGHT;
        vkWaitForFences(ctx.device, 1, &ctx.fences[frame], VK_TRUE, UINT64_MAX);

        u32 image = 0;
        VkResult result = vkAcquireNextImageKHR(
            ctx.device, ctx.swapchain, UINT64_MAX, ctx.image_available[frame], VK_NULL_HANDLE, &image);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "casino: vkAcquireNextImageKHR failed (%d)\n", (int)result);
            return;
        }

        if (!recordCommands(ctx.commands[frame], image))
            return;

        // reset right before submit, a failed record must not eat the signal
        vkResetFences(ctx.device, 1, &ctx.fences[frame]);

        VkSemaphore wait[1] = { ctx.image_available[frame] };
        VkPipelineStageFlags stages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkCommandBuffer commands[1] = { ctx.commands[frame] };
        VkSemaphore signal[1] = { ctx.render_finished[image] };

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = wait;
        submit.pWaitDstStageMask = stages;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = commands;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = signal;

        if (!check(vkQueueSubmit(ctx.graphics_queue, 1, &submit, ctx.fences[frame]), "vkQueueSubmit"))
            return;

        VkSwapchainKHR swapchains[1] = { ctx.swapchain };

        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = signal;
        present.swapchainCount = 1;
        present.pSwapchains = swapchains;
        present.pImageIndices = &image;

        result = vkQueuePresentKHR(ctx.present_queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            recreateSwapchain();
        else if (result != VK_SUCCESS)
            fprintf(stderr, "casino: vkQueuePresentKHR failed (%d)\n", (int)result);

        ctx.frame += 1;
    }

    inline void shutdown();

    inline bool init(const InitDesc& desc) { // BLACKBOX
        if (desc.win == NULL)
            return false;

        shutdown();
        ctx.win = desc.win;

        if (!createInstance(desc)) {
            shutdown();
            return false;
        }

        if (!check(rgfw::createSurface(desc.win, ctx.instance, &ctx.surface), "rgfw::createSurface")) {
            shutdown();
            return false;
        }

        ctx.gpu = pickDevice(ctx.surface);
        if (ctx.gpu == VK_NULL_HANDLE) {
            fprintf(stderr, "casino: no vulkan device with swapchain support\n");
            shutdown();
            return false;
        }

        QueueFamilies queues = findQueueFamilies(ctx.gpu, ctx.surface);
        ctx.graphics_family = queues.graphics;
        ctx.present_family = queues.present;

        bool ok = createDevice() &&
            createSwapchain(VK_NULL_HANDLE) &&
            createRenderPass() &&
            createViews() &&
            createFramebuffers() &&
            createCommands() &&
            createSync();
        if (!ok) {
            shutdown();
            return false;
        }

        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(ctx.gpu, &props);
        printf("casino: %s, %ux%u, %u images\n",
            props.deviceName, ctx.extent.width, ctx.extent.height, ctx.image_count);
        return true;
    }

    inline void shutdown() {
        if (ctx.device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(ctx.device);

        for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (ctx.image_available[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(ctx.device, ctx.image_available[i], NULL);
            if (ctx.fences[i] != VK_NULL_HANDLE)
                vkDestroyFence(ctx.device, ctx.fences[i], NULL);
        }

        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (ctx.render_finished[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(ctx.device, ctx.render_finished[i], NULL);
        }

        if (ctx.command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(ctx.device, ctx.command_pool, NULL);

        for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
            if (ctx.framebuffers[i] != VK_NULL_HANDLE)
                vkDestroyFramebuffer(ctx.device, ctx.framebuffers[i], NULL);
            if (ctx.views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(ctx.device, ctx.views[i], NULL);
        }

        if (ctx.render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(ctx.device, ctx.render_pass, NULL);
        if (ctx.swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(ctx.device, ctx.swapchain, NULL);
        if (ctx.device != VK_NULL_HANDLE)
            vkDestroyDevice(ctx.device, NULL);

        if (ctx.messenger != VK_NULL_HANDLE && ctx.instance != VK_NULL_HANDLE) {
            PFN_vkDestroyDebugUtilsMessengerEXT destroy =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy != NULL)
                destroy(ctx.instance, ctx.messenger, NULL);
        }

        if (ctx.surface != VK_NULL_HANDLE && ctx.instance != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(ctx.instance, ctx.surface, NULL);
        if (ctx.instance != VK_NULL_HANDLE)
            vkDestroyInstance(ctx.instance, NULL);

        ctx = Context{};
    }

    inline VkDevice getDevice() { return ctx.device; }
    inline VkPhysicalDevice getPhysicalDevice() { return ctx.gpu; }
    inline VkSurfaceKHR getSurface() { return ctx.surface; }
    inline VkSwapchainKHR getSwapchain() { return ctx.swapchain; }
    inline VkRenderPass getRenderPass() { return ctx.render_pass; }
    inline VkExtent2D getExtent() { return ctx.extent; }
    inline u32 getGraphicsFamily() { return ctx.graphics_family; }
    inline VkQueue getGraphicsQueue() { return ctx.graphics_queue; }
    inline VkCommandBuffer getCommandBuffer(u32 frame) {
        return ctx.commands[frame % MAX_FRAMES_IN_FLIGHT];
    }

} // namespace casino
