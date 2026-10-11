#pragma once

#include "render/textures.hpp"

// generated spirv from shadermake (in binshaders, on the include path)
#include "prim_vert.bin.h"
#include "prim_frag.bin.h"

// 2d primitives in relative coords: (0,0)=top-left, (1,1)=bottom-right, y-down.
// one pipeline (vertex color * texture). textures come from a prim::TextureCache,
// the white 1x1 lives there too and backs every untextured prim.
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

    // where x,y land relative to the drawn box, so callers stop doing w*0.5 by hand
    enum class Anchor {
        top_left,
        top_right,
        bottom_left,
        bottom_right,
        center,
    };

    static const u32 MAX_VERTS = 65536;

    struct Batch {
        VkDescriptorSet set;
        u32 first;
        u32 count;
    };

    // shift x,y from the given anchor to the top-left corner of a w*h box
    inline void applyAnchor(float& x, float& y, float w, float h, Anchor a) {
        switch (a) {
            case Anchor::center: x -= w * 0.5f; y -= h * 0.5f; break;
            case Anchor::top_right: x -= w; break;
            case Anchor::bottom_left: y -= h; break;
            case Anchor::bottom_right: x -= w; y -= h; break;
            case Anchor::top_left:
            default: break;
        }
    }

    class Primitives {
    public:
        const casino::Device* dev = NULL;
        const TextureCache* cache = NULL;
        casino::Pipeline pipeline;
        casino::GpuBuffer vbuf[casino::MAX_FRAMES_IN_FLIGHT];

        Vertex2D* verts = NULL;
        u32 vert_count = 0;
        Batch* batches = NULL;
        u32 batch_count = 0;

        Primitives() = default;
        ~Primitives() { shutdown(); }
        Primitives(const Primitives&) = delete;
        Primitives& operator=(const Primitives&) = delete;

        bool init(const casino::Device& device, const TextureCache& textures); // BLACKBOX
        void shutdown();

        void begin();
        void drawRect(float x, float y, float w, float h, Color c);
        void drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color c);
        void drawImage(TextureId id, float x, float y, float w, float h, Anchor a = Anchor::top_left);
        void drawImage(const casino::Texture& tex, float x, float y, float w, float h, Anchor a = Anchor::top_left);
        void drawImageFit(TextureId id, float cx, float cy, float w, Anchor a = Anchor::center); // BLACKBOX
        void render(VkCommandBuffer cmd, u32 frame_slot);

    private:
        void pushVertex(VkDescriptorSet set, const Vertex2D& v);
    };

    inline bool Primitives::init(const casino::Device& device, const TextureCache& textures) { // BLACKBOX
        shutdown();
        dev = &device;
        cache = &textures;
        VkDevice d = device.device;

        if (!pipeline.create(device, prim_vert_data, prim_vert_size * 4,
                prim_frag_data, prim_frag_size * 4, textures.set_layout))
            return false;

        for (u32 i = 0; i < casino::MAX_FRAMES_IN_FLIGHT; i++) {
            if (!vbuf[i].create(d, device.gpu, MAX_VERTS * sizeof(Vertex2D),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return false;
        }

        verts = (Vertex2D*)malloc(MAX_VERTS * sizeof(Vertex2D));
        batches = (Batch*)malloc(MAX_TEXTURES * sizeof(Batch));
        return true;
    }

    inline void Primitives::shutdown() {
        if (dev && dev->device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(dev->device);
        pipeline.destroy();
        for (u32 i = 0; i < casino::MAX_FRAMES_IN_FLIGHT; i++)
            vbuf[i].destroy();
        free(verts);
        free(batches);
        verts = NULL;
        batches = NULL;
        vert_count = batch_count = 0;
        cache = NULL;
        dev = NULL;
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
        VkDescriptorSet set = cache->white().set;
        Vertex2D tl = { x, y, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D tr = { x + w, y, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D br = { x + w, y + h, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D bl = { x, y + h, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        pushVertex(set, tl);
        pushVertex(set, tr);
        pushVertex(set, br);
        pushVertex(set, tl);
        pushVertex(set, br);
        pushVertex(set, bl);
    }

    inline void Primitives::drawTriangle(float ax, float ay, float bx, float by,
        float cx, float cy, Color c) {
        VkDescriptorSet set = cache->white().set;
        Vertex2D a = { ax, ay, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D b = { bx, by, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        Vertex2D cc = { cx, cy, c.r, c.g, c.b, c.a, 0.0f, 0.0f };
        pushVertex(set, a);
        pushVertex(set, b);
        pushVertex(set, cc);
    }

    inline void Primitives::drawImage(const casino::Texture& tex, float x, float y,
        float w, float h, Anchor a) {
        applyAnchor(x, y, w, h, a);
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

    inline void Primitives::drawImage(TextureId id, float x, float y, float w, float h, Anchor a) {
        drawImage(cache->get(id), x, y, w, h, a);
    }

    inline void Primitives::drawImageFit(TextureId id, float cx, float cy, float w, Anchor a) { // BLACKBOX
        const casino::Texture& tex = cache->get(id);
        if (tex.width == 0 || tex.height == 0)
            return;
        // keep the pixel aspect square on a non square window
        VkExtent2D e = dev->currentExtent();
        float screen_aspect = (e.height != 0) ? (float)e.width / (float)e.height : 1.0f;
        float img_aspect = (float)tex.height / (float)tex.width;
        float h = w * screen_aspect * img_aspect;
        drawImage(tex, cx, cy, w, h, a);
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
