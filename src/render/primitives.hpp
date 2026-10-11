#pragma once

#include "render/image.hpp"
#include "render/vulkan_casino_777.hpp"

// generated spirv from shadermake (in binshaders, on the include path)
#include "prim_vert.bin.h"
#include "prim_frag.bin.h"

// 2d primitives in relative coords: (0,0)=top-left, (1,1)=bottom-right, y-down.
// one pipeline (vertex color * texture), white 1x1 texture for untextured prims.
namespace prim {

    struct Color {
        float r, g, b, a;
    };

    inline Color rgba(float r, float g, float b, float a) { return Color{ r, g, b, a }; }

    // matches the pipeline vertex layout: vec2 pos, vec4 color, vec2 uv
    struct Vertex2D {
        float x, y;
        float r, g, b, a;
        float u, v;
    };

    static const u32 MAX_VERTS = 65536;
    static const u32 MAX_TEXTURES = 128;

    struct Batch {
        VkDescriptorSet set;
        u32 first;
        u32 count;
    };

    class Primitives {
    public:
        const casino::Device* dev = NULL;
        casino::Pipeline pipeline;
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        casino::GpuBuffer vbuf[casino::MAX_FRAMES_IN_FLIGHT];
        casino::Texture white;

        Vertex2D* verts = NULL;
        u32 vert_count = 0;
        Batch* batches = NULL;
        u32 batch_count = 0;

        Primitives() = default;
        ~Primitives() { shutdown(); }
        Primitives(const Primitives&) = delete;
        Primitives& operator=(const Primitives&) = delete;

        bool init(const casino::Device& device); // BLACKBOX
        void shutdown();

        bool createTexture(casino::Texture* out, const img::Image& image); // BLACKBOX
        bool loadTexture(casino::Texture* out, const char* path); // BLACKBOX

        void begin();
        void drawRect(float x, float y, float w, float h, Color c);
        void drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color c);
        void drawImage(const casino::Texture& tex, float x, float y, float w, float h);
        void render(VkCommandBuffer cmd, u32 frame_slot);

    private:
        void pushVertex(VkDescriptorSet set, const Vertex2D& v);
    };

    inline bool Primitives::init(const casino::Device& device) { // BLACKBOX
        shutdown();
        dev = &device;
        VkDevice d = device.device;

        // one combined-image-sampler binding, fragment stage
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo linfo = {};
        linfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        linfo.bindingCount = 1;
        linfo.pBindings = &binding;
        if (!casino::check(vkCreateDescriptorSetLayout(d, &linfo, NULL, &set_layout), "set layout"))
            return false;

        VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES };
        VkDescriptorPoolCreateInfo pinfo = {};
        pinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pinfo.maxSets = MAX_TEXTURES;
        pinfo.poolSizeCount = 1;
        pinfo.pPoolSizes = &pool_size;
        if (!casino::check(vkCreateDescriptorPool(d, &pinfo, NULL, &pool), "desc pool"))
            return false;

        if (!pipeline.create(device, prim_vert_data, prim_vert_size * 4,
                prim_frag_data, prim_frag_size * 4, set_layout))
            return false;

        for (u32 i = 0; i < casino::MAX_FRAMES_IN_FLIGHT; i++) {
            if (!vbuf[i].create(d, device.gpu, MAX_VERTS * sizeof(Vertex2D),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return false;
        }

        // white 1x1 so untextured prims sample a solid texel
        uint8_t px[4] = { 255, 255, 255, 255 };
        img::Image w = { 1, 1, px };
        if (!createTexture(&white, w))
            return false;

        verts = (Vertex2D*)malloc(MAX_VERTS * sizeof(Vertex2D));
        batches = (Batch*)malloc(MAX_TEXTURES * sizeof(Batch));
        return true;
    }

    inline void Primitives::shutdown() {
        if (dev && dev->device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(dev->device);
        white.destroy();
        pipeline.destroy();
        for (u32 i = 0; i < casino::MAX_FRAMES_IN_FLIGHT; i++)
            vbuf[i].destroy();
        if (pool) vkDestroyDescriptorPool(dev ? dev->device : VK_NULL_HANDLE, pool, NULL);
        if (set_layout) vkDestroyDescriptorSetLayout(dev ? dev->device : VK_NULL_HANDLE, set_layout, NULL);
        pool = VK_NULL_HANDLE;
        set_layout = VK_NULL_HANDLE;
        free(verts);
        free(batches);
        verts = NULL;
        batches = NULL;
        vert_count = batch_count = 0;
        dev = NULL;
    }

    inline bool Primitives::createTexture(casino::Texture* out, const img::Image& image) { // BLACKBOX
        if (!dev || image.pixels == NULL)
            return false;
        return out->create(*dev, image.pixels, image.width, image.height, pool, set_layout);
    }

    inline bool Primitives::loadTexture(casino::Texture* out, const char* path) { // BLACKBOX
        img::Image image = {};
        if (!img::loadPng(path, &image))
            return false;
        bool ok = createTexture(out, image);
        img::freeImage(&image);
        return ok;
    }

    inline void Primitives::begin() {
        vert_count = 0;
        batch_count = 0;
    }

    inline void Primitives::pushVertex(VkDescriptorSet set, const Vertex2D& v) {
        if (vert_count >= MAX_VERTS)
            return; // batch full, dropped
        if (batch_count == 0 || batches[batch_count - 1].set != set) {
            if (batch_count >= MAX_TEXTURES)
                return;
            batches[batch_count].set = set;
            batches[batch_count].first = vert_count;
            batches[batch_count].count = 0;
            batch_count += 1;
        }
        verts[vert_count++] = v;
        batches[batch_count - 1].count += 1;
    }

    inline void Primitives::drawRect(float x, float y, float w, float h, Color c) {
        Vertex2D tl = { x, y, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D tr = { x + w, y, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D br = { x + w, y + h, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D bl = { x, y + h, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        VkDescriptorSet set = white.set;
        pushVertex(set, tl);
        pushVertex(set, tr);
        pushVertex(set, br);
        pushVertex(set, tl);
        pushVertex(set, br);
        pushVertex(set, bl);
    }

    inline void Primitives::drawTriangle(float ax, float ay, float bx, float by,
        float cx, float cy, Color c) {
        Vertex2D a = { ax, ay, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D b = { bx, by, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D cc = { cx, cy, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        VkDescriptorSet set = white.set;
        pushVertex(set, a);
        pushVertex(set, b);
        pushVertex(set, cc);
    }

    inline void Primitives::drawImage(const casino::Texture& tex, float x, float y, float w, float h) {
        // full image, uv (0,0)=top-left .. (1,1)=bottom-right, white tint
        float cr = 1.0f, cg = 1.0f, cb = 1.0f, ca = 1.0f;
        Vertex2D tl = { x, y, cr, cg, cb, ca, 0.0f, 0.0f };
        Vertex2D tr = { x + w, y, cr, cg, cb, ca, 1.0f, 0.0f };
        Vertex2D br = { x + w, y + h, cr, cg, cb, ca, 1.0f, 1.0f };
        Vertex2D bl = { x, y + h, cr, cg, cb, ca, 0.0f, 1.0f };
        VkDescriptorSet set = tex.set;
        pushVertex(set, tl);
        pushVertex(set, tr);
        pushVertex(set, br);
        pushVertex(set, tl);
        pushVertex(set, br);
        pushVertex(set, bl);
    }

    inline void Primitives::render(VkCommandBuffer cmd, u32 frame_slot) {
        if (vert_count == 0 || dev == NULL)
            return;

        memcpy(vbuf[frame_slot].mapped, verts, (size_t)vert_count * sizeof(Vertex2D));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf[frame_slot].buffer, &offset);
        for (u32 i = 0; i < batch_count; i++) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline.layout, 0, 1, &batches[i].set, 0, NULL);
            vkCmdDraw(cmd, batches[i].count, 1, batches[i].first, 0);
        }
    }

} // namespace prim
