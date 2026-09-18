#ifndef HUINYA_HECK_HPP
#define HUINYA_HECK_HPP

#include "rgfwshit.hpp"
#include "vulkanshit.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

// VBO: loc0-loc3
struct Vertex {
    vks::Vec2 position;
    vks::Vec2 uv;
    vks::Vec4 color;
    uint32_t tex_index;
};

// UBO: set=0, binding=0
struct FrameData {
    vks::Mat4 projection;
    vks::Vec2 screen_size;
    float time;
    float _pad;
};

class Hell_Machina { // BLACKBOX
private:
    static constexpr const char* SHADER_DIR = "shaders";

    RGFW_window* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vks::Instance instance;
    vks::Device device;
    vks::Swapchain swapchain;
    vks::FramePipeline pipeline;

    std::array<vks::Buffer, vks::k_max_frames_in_flight> vbos;
    std::array<vks::Buffer, vks::k_max_frames_in_flight> ubos;
    vks::Texture white_texture;
    vks::DescriptorSetLayout descriptor_set_layout;
    vks::DescriptorPool descriptor_pool;
    std::array<VkDescriptorSet, vks::k_max_frames_in_flight> descriptor_sets;

    std::vector<VkVertexInputBindingDescription> vertex_bindings; // descritions
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;

    std::vector<Vertex> vertices;

    bool initialized = false;
    std::chrono::steady_clock::time_point start_time;

    static std::vector<VkVertexInputBindingDescription> make_vertex_bindings() {
        return {{ { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX } }};
    }

    static std::vector<VkVertexInputAttributeDescription> make_vertex_attributes() {
        return {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position) },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
            { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) },
            { 3, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, tex_index) },
        };
    }

    static std::array<Vertex, 6> make_quad(vks::Vec2 pos, vks::Vec2 size,
                                           vks::Color tint, uint32_t tex_index) {
        vks::Vec2 min = pos;
        vks::Vec2 max = { pos.x + size.x, pos.y + size.y };
        vks::Vec4 color = { tint.r, tint.g, tint.b, tint.a };
        vks::Rect full_uv = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
        return {
            // A (top-left) -> B (top-right) -> C (bottom-right)
            Vertex{ min, { full_uv.min.x, full_uv.min.y }, color, tex_index },
            Vertex{ { max.x, min.y }, { full_uv.max.x, full_uv.min.y }, color, tex_index },
            Vertex{ max, { full_uv.max.x, full_uv.max.y }, color, tex_index },
            // A -> C -> D (bottom-left)
            Vertex{ min, { full_uv.min.x, full_uv.min.y }, color, tex_index },
            Vertex{ max, { full_uv.max.x, full_uv.max.y }, color, tex_index },
            Vertex{ { min.x, max.y }, { full_uv.min.x, full_uv.max.y }, color, tex_index },
        };
    }

    void destroy_all() {
        if (device.handle != VK_NULL_HANDLE) {
            vks::wait_device_idle(device);
        }
        for (vks::Buffer& vbo : vbos) {
            vks::destroy_buffer(device, vbo);
        }
        vbos = {};
        for (vks::Buffer& ubo : ubos) {
            vks::destroy_buffer(device, ubo);
        }
        ubos = {};
        vks::destroy_texture(device, white_texture);
        white_texture = {};
        vks::destroy_frame_pipeline(device, pipeline);
        pipeline = {};
        vks::destroy_descriptor_pool(device, descriptor_pool);
        descriptor_pool = {};
        vks::destroy_descriptor_layout(device, descriptor_set_layout);
        descriptor_set_layout = {};
        vks::destroy_swapchain(device, swapchain);
        swapchain = {};
        vks::destroy_device(device);
        device = {};
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance.handle, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        vks::destroy_instance(instance);
        instance = {};
        if (window != nullptr) {
            rgfw::destroyWindow(window);
            window = nullptr;
        }
        initialized = false;
    }

    void fuckup(const char* message) {
        std::cerr << message << "\n";
        destroy_all();
    }

    void update_ubo(size_t frame_index, uint32_t width, uint32_t height) {
        FrameData frame_data{};
        frame_data.projection = vks::ortho_projection((float)width, (float)height);
        frame_data.screen_size = { (float)width, (float)height };
        frame_data.time = std::chrono::duration<float>( std::chrono::steady_clock::now() - start_time).count();
        frame_data._pad = 0.0f;
        vks::update_buffer(device, ubos[frame_index], &frame_data, sizeof(FrameData));
    }

    bool create_internal(uint32_t width, uint32_t height) {
        if (!rgfw::init()) {
            std::cerr << "failed to init RGFW platform\n";
            return false;
        }
        window = rgfw::createWindow((int)width, (int)height, "huinya engine");
        if (window == nullptr) {
            std::cerr << "failed to create RGFW window\n";
            rgfw::destroyWindow(window);
            return false;
        }

        instance = vks::create_instance();
        if (instance.handle == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan instance");
            return false;
        }

        if (rgfw::createSurface(window, instance.handle, &surface) != VK_SUCCESS ||
            surface == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan surface from RGFW window");
            return false;
        }

        device = vks::create_device(instance, surface);
        if (device.handle == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan device");
            return false;
        }

        swapchain = vks::create_swapchain(device, surface, width, height);
        if (swapchain.handle == VK_NULL_HANDLE) {
            fuckup("failed to create Vulkan swapchain");
            return false;
        }

        vertex_bindings = make_vertex_bindings();
        vertex_attributes = make_vertex_attributes();

        const std::vector<VkDescriptorSetLayoutBinding> desc_bindings = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        };
        descriptor_set_layout = vks::create_descriptor_layout(device, desc_bindings);
        if (descriptor_set_layout.handle == VK_NULL_HANDLE) {
            fuckup("failed to create descriptor set layout");
            return false;
        }

        constexpr uint32_t k_frames = vks::k_max_frames_in_flight;

        const std::vector<VkDescriptorPoolSize> pool_sizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_frames },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 * k_frames },
        };
        descriptor_pool = vks::create_descriptor_pool(device, pool_sizes, k_frames, 1);
        if (descriptor_pool.handle == VK_NULL_HANDLE) {
            fuckup("failed to create descriptor pool");
            return false;
        }

        const std::vector<VkDescriptorSet> sets =
            vks::allocate_descriptor_sets(device, descriptor_pool,
                                          descriptor_set_layout, k_frames);
        if (sets.size() != k_frames) {
            fuckup("failed to allocate descriptor sets");
            return false;
        }
        std::copy(sets.begin(), sets.end(), descriptor_sets.begin());

        uint32_t white_pixel = 0xFFFFFFFFu;
        white_texture = vks::create_texture(device, 1, 1, &white_pixel);
        if (white_texture.view == VK_NULL_HANDLE) {
            fuckup("failed to create white texture");
            return false;
        }

        std::array<VkImageView, 16> texture_views;
        texture_views.fill(white_texture.view);
        for (size_t i = 0; i < k_frames; i++) {
            ubos[i] = vks::create_buffer(device, sizeof(FrameData),
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
            if (ubos[i].handle == VK_NULL_HANDLE) {
                fuckup("failed to create UBO");
                return false;
            }
            update_ubo(i, width, height);
            vks::write_descriptor_buffer(device, descriptor_sets[i], 0,
                                         ubos[i].handle, 0, sizeof(FrameData));
            vks::write_descriptor_textures(device, descriptor_sets[i], 1,
                                           texture_views, white_texture.sampler);
        }

        vks::PipelineConfig config;
        config.bindings = vertex_bindings;
        config.attributes = vertex_attributes;
        config.descriptor_set_layout = descriptor_set_layout.handle;
        config.alpha_blend = false;
        config.backface_cull = true;

        pipeline = vks::create_frame_pipeline(device, swapchain, SHADER_DIR, config);
        if (pipeline.handle == VK_NULL_HANDLE) {
            fuckup("failed to create frame pipeline");
            return false;
        }

        static constexpr VkDeviceSize k_vbo_bytes = 768 * sizeof(Vertex);
        for (vks::Buffer& vbo : vbos) {
            vbo = vks::create_buffer(device, k_vbo_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
            if (vbo.handle == VK_NULL_HANDLE) {
                fuckup("failed to create vertex buffer");
                return false;
            }
        }

        initialized = true;
        return true;
    }

public:
    Hell_Machina(uint32_t width, uint32_t height) {
        start_time = std::chrono::steady_clock::now();
        if (!create_internal(width, height)) {
            return;
        }
    }

    ~Hell_Machina() {
        if (initialized) {
            destroy_all();
        } else if (window != nullptr) {
            rgfw::destroyWindow(window);
            window = nullptr;
        }
    }

    Hell_Machina(const Hell_Machina&) = delete;
    Hell_Machina& operator=(const Hell_Machina&) = delete;

    bool draw() {
        if (!initialized) return false;

        const uint32_t w = swapchain.extent.width;
        const uint32_t h = swapchain.extent.height;
        if (w == 0 || h == 0) return true;

        const size_t frame = pipeline.current_frame;
        VkCommandBuffer cmd = vks::begin_frame(device, swapchain, pipeline);
        if (cmd == VK_NULL_HANDLE) return true;

        update_ubo(frame, w, h);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);

        if (!vertices.empty()) {
            vks::Buffer& vbo = vbos[frame];
            VkDeviceSize needed = vertices.size() * sizeof(Vertex);
            if (needed > vbo.size) {
                vks::destroy_buffer(device, vbo);
                vbo = vks::create_buffer(device, needed, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
                if (vbo.handle == VK_NULL_HANDLE) {
                    std::cerr << "failed to grow VBO\n";
                    vks::end_frame(device, swapchain, pipeline, cmd);
                    return false;
                }
            }
            vks::update_buffer(device, vbo, vertices.data(), needed);

            VkBuffer vertex_buffers[] = { vbo.handle };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
                                    0, 1, &descriptor_sets[frame], 0, nullptr);

            vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        }

        VkResult result = vks::end_frame(device, swapchain, pipeline, cmd);
        return result == VK_SUCCESS;
    }

    void pollEvents() {
        rgfw::pollEvents(window);
    }

    bool shouldClose() {
        return rgfw::shouldClose(window);
    }

    void clear() { vertices.clear(); }

    void push_rectangle(vks::Vec2 pos, vks::Vec2 size, vks::Color tint, uint32_t tex_index = 0) {
        for (const Vertex& v : make_quad(pos, size, tint, tex_index)) {
            vertices.push_back(v);
        }
    }

    RGFW_window* get_window() const { return window; }
};

#endif
