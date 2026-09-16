#ifndef HUINYA_HECK_HPP
#define HUINYA_HECK_HPP

#include "rgfwshit.hpp"
#include "vulkanshit.hpp"

#include <cstdint>
#include <iostream>

class Hell_Machina {    // BLACKBOX
private:
    static constexpr const char* SHADER_DIR = "shaders";

    RGFW_window* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vks::Instance instance;
    vks::Device device;
    vks::Swapchain swapchain;
    vks::FramePipeline pipeline;
    bool initialized = false;

public:
    void fuckup(const char* message) {
        std::cerr << message << "\n";
        if (device.handle != VK_NULL_HANDLE) {
            vks::wait_device_idle(device);
        }
        vks::destroy_frame_pipeline(device, pipeline);
        vks::destroy_swapchain(device, swapchain);
        vks::destroy_device(device);
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance.handle, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        vks::destroy_instance(instance);
        if (window != nullptr) {
            rgfw::destroyWindow(window);
            window = nullptr;
        }
    }

    Hell_Machina(uint32_t width, uint32_t height) {
        if (!rgfw::init()) {
            std::cerr << "failed to init RGFW platform\n";
            return;
        }

        window = rgfw::createWindow((int)width, (int)height, "huinya engine - hello triangle");
        if (window == nullptr) {
            std::cerr << "failed to create RGFW window\n";
            return;
        }

        instance = vks::create_instance();
        if (instance.handle == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan instance");
            return;
        }

        if (rgfw::createSurface(window, instance.handle, &surface) != VK_SUCCESS || surface == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan surface from RGFW window");
            return;
        }

        device = vks::create_device(instance, surface);
        if (device.handle == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan device");
            return;
        }

        swapchain = vks::create_swapchain(device, surface, width, height);
        if (swapchain.handle == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan swapchain");
            return;
        }

        pipeline = vks::create_frame_pipeline(device, swapchain, SHADER_DIR);
        if (pipeline.handle == VK_NULL_HANDLE) {
            fuckup("failed to create frame pipeline");
            return;
        }

        initialized = true;
    }

    ~Hell_Machina() {
        if (initialized) {
            if (device.handle != VK_NULL_HANDLE) {
                vks::wait_device_idle(device);
            }
            vks::destroy_frame_pipeline(device, pipeline);
            vks::destroy_swapchain(device, swapchain);
            vks::destroy_device(device);
            if (surface != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(instance.handle, surface, nullptr);
                surface = VK_NULL_HANDLE;
            }
            vks::destroy_instance(instance);
            initialized = false;
        }
        if (window != nullptr) {
            rgfw::destroyWindow(window);
            window = nullptr;
        }
    }

    bool draw() {
        if (!initialized) return false;
        return vks::draw_frame(device, swapchain, pipeline) == VK_SUCCESS;
    }

    void pollEvents() {
        rgfw::pollEvents(window);
    }

    bool shouldClose() {
        return rgfw::shouldClose(window);
    }
};

#endif
