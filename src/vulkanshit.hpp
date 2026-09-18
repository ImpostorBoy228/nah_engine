// vulkanshit.hpp — explicit Vulkan layer for huinya engine.
//
// No RAII, no vk-bootstrap: every create_* has a matching destroy_*.
// The structs below are plain containers of raw handles; the caller owns them.
#ifndef HUINYA_VULKANSHIT_HPP
#define HUINYA_VULKANSHIT_HPP

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vks {

constexpr uint32_t k_max_frames_in_flight = 2;

// ---------------- Math bricks ----------------

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

// Axis-aligned rect on a single UV or coordinate plane.
struct Rect { Vec2 min, max; };

struct Color { float r, g, b, a; };

// Column-major, matches GLSL mat4.
struct Mat4 { float m[16]; };

// 4x4 column-major matmul: a * b. Auto-vectorizable.
Mat4 mat4_mul(const Mat4& a, const Mat4& b);

// Orthographic projection mapping pixel space (origin at top-left) into NDC.
Mat4 ortho_projection(float width, float height);

// Vulkan instance + optional debug messenger.
struct Instance {
    VkInstance               handle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    bool                     validation = false;
};

// Physical + logical device, queue family indices and raw queues.
struct Device {
    VkInstance instance = VK_NULL_HANDLE; // for convenience in destroy paths
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         handle    = VK_NULL_HANDLE;

    uint32_t graphics_family = 0;
    uint32_t present_family  = 0;

    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue  = VK_NULL_HANDLE;
};

// ---------------- Buffer brick ----------------

// Raw GPU buffer with its allocation. host_visible buffers keep a mapped pointer.
struct Buffer {
    VkBuffer       handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    void*          mapped = nullptr;
};

// Aligns a size up to the given alignment (must be a power of two).
VkDeviceSize align_size(VkDeviceSize size, VkDeviceSize alignment);

// Queries minUniformBufferOffsetAlignment from the physical device limits.
VkDeviceSize min_ubo_alignment(const Device& device);

// Creates a buffer. host_visible implies VK_MEMORY_PROPERTY_HOST_VISIBLE |
// HOST_COHERENT and a persistent map; otherwise memory is DEVICE_LOCAL and
// data must be moved with update_buffer (staging copy).
Buffer create_buffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible);

// Copies data into the buffer. For host_visible buffers it is a plain memcpy;
// for device-local ones it routes through a staging buffer + one-shot transfer
// submit on the graphics queue.
void update_buffer(const Device& device, Buffer& buffer, const void* data, VkDeviceSize size);

void destroy_buffer(const Device& device, Buffer& buffer);

// ---------------- Texture brick ----------------

// Sampled 2D image owning its image, memory, view and sampler.
struct Texture {
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
    uint32_t       width   = 0;
    uint32_t       height  = 0;
};

// Uploads RGBA8 pixels into a device-local image and creates its view/sampler.
// An atlas is just one Texture whose UV rects are picked per draw.
Texture create_texture(const Device& device, uint32_t width, uint32_t height, const void* pixels);

void destroy_texture(const Device& device, Texture& texture);

// ---------------- Descriptor bricks ----------------

// Descriptor set layout + pool wrapper. Sets can be allocated from the pool,
// and the caller gets the raw VkDescriptorSet handles back.
struct DescriptorSetLayout { VkDescriptorSetLayout handle = VK_NULL_HANDLE; };
struct DescriptorPool {
    VkDescriptorPool handle = VK_NULL_HANDLE;
    uint32_t         layout_count = 0; // layouts this pool is allowed to make sets from
};

DescriptorSetLayout create_descriptor_layout(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings);

DescriptorPool create_descriptor_pool(
    const Device& device,
    std::span<const VkDescriptorPoolSize> pool_sizes,
    uint32_t max_sets,
    uint32_t layout_count = 1);

std::vector<VkDescriptorSet> allocate_descriptor_sets(
    const Device& device, const DescriptorPool& pool, const DescriptorSetLayout& layout, uint32_t count);

void destroy_descriptor_layout(const Device& device, DescriptorSetLayout& layout);
void destroy_descriptor_pool(const Device& device, DescriptorPool& pool);

// Thin wrappers over vkUpdateDescriptorSets for the common 2D framebuffer case.
void write_descriptor_buffer(
    const Device& device, VkDescriptorSet set, uint32_t binding,
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

void write_descriptor_textures(
    const Device& device, VkDescriptorSet set, uint32_t binding,
    std::span<const VkImageView> views, VkSampler sampler);

// Swapchain created for an externally owned VkSurfaceKHR (owned by the caller).
struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent = {0, 0};
    std::vector<VkImage> images;

    // Depth buffer paired with this swapchain.
    VkImage        depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView    depth_view = VK_NULL_HANDLE;
    VkFormat       depth_format = VK_FORMAT_UNDEFINED;

    // Creation parameters, kept to rebuild the swapchain on VK_ERROR_OUT_OF_DATE_KHR.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    uint32_t     width   = 0;
    uint32_t     height  = 0;
};

// Full GPU frame graph for one swapchain: render pass, pipeline, framebuffers,
// command pool/buffers and synchronization objects.
struct FramePipeline {
    VkRenderPass     render_pass = VK_NULL_HANDLE;
    VkPipelineLayout layout      = VK_NULL_HANDLE;
    VkPipeline       handle      = VK_NULL_HANDLE;

    std::vector<VkImageView>   image_views;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool                command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;

    // One pair per frame in flight, signalled by vkAcquireNextImageKHR.
    std::vector<VkSemaphore> image_available;
    // One per swapchain image: the present engine holds it longer than the
    // submit fence (VUID-vkQueueSubmit-pSignalSemaphores-00067).
    std::vector<VkSemaphore> render_finished;
    // One per frame in flight.
    std::vector<VkFence> frame_fences;
    // One per swapchain image: which submit fence that image is waiting on.
    std::vector<VkFence> image_fences;

    std::string shader_dir;
    size_t      current_frame = 0;
    // Image acquired by the latest begin_frame, consumed by end_frame.
    uint32_t current_image_index = 0;
    // Deep copy of the config so recreate_swapchain() can rebuild the pipeline.
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    bool alpha_blend   = false;
    bool backface_cull = true;
};

// User-supplied vertex layout + rasterization options; the frame pipeline no
// longer hardcodes an empty vertex input.
struct PipelineConfig {
    std::span<const VkVertexInputBindingDescription>   bindings;
    std::span<const VkVertexInputAttributeDescription> attributes;
    // Descriptor set 0 used by the shaders; baked into the pipeline layout.
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    bool alpha_blend   = false;
    bool backface_cull = true;
};

// --- Life cycle: create_* returns a struct, destroy_* resets it to empty. ---

Instance create_instance();

Device create_device(const Instance& instance, VkSurfaceKHR surface);

Swapchain create_swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height);

FramePipeline create_frame_pipeline(
    const Device& device, const Swapchain& swapchain, const char* shader_dir, const PipelineConfig& config);

void destroy_instance(Instance& instance);
void destroy_device(Device& device);
void destroy_swapchain(const Device& device, Swapchain& swapchain);
void destroy_frame_pipeline(const Device& device, FramePipeline& pipeline);

// --- Frame ---

// Command buffers are recorded per frame (TRANSIENT pool). begin_frame
// acquires the next image, resets the pool, starts the render pass and returns
// the command buffer for the caller to record draws into. The caller must
// finish with end_frame(cmd) which ends the render pass, submits and presents.
// On VK_ERROR_OUT_OF_DATE_KHR begin/end call recreate_swapchain() and return
// VK_NULL_HANDLE / the new result; the caller then retries the frame.
VkCommandBuffer begin_frame(const Device& device, Swapchain& swapchain, FramePipeline& pipeline);
VkResult        end_frame(const Device& device, Swapchain& swapchain, FramePipeline& pipeline, VkCommandBuffer cmd);

// Rebuilds the swapchain together with every object that depends on its images.
VkResult recreate_swapchain(const Device& device, Swapchain& swapchain, FramePipeline& pipeline);

// --- Helpers ---

void wait_device_idle(Device& device);

} // namespace vks

#endif // HUINYA_VULKANSHIT_HPP