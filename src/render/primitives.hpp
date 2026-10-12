#pragma once

#include "render/textures.hpp"
#include "render/camera.hpp"

// generated spirv from shadermake (in binshaders, on the include path)
#include "prim_vert.bin.h"
#include "prim_frag.bin.h"

// 3d primitives in world space (depth tested), 2d primitives in relative coords
// (0,0)=top-left, (1,1)=bottom-right, y-down (drawn after 3d, always on top).
// both use one shader pair, the 2d pipeline just skips the depth test.
// textures come from a prim::TextureCache, the white 1x1 backs untextured prims.
namespace prim {

    struct Color {
        float r, g, b, a;
    };

    inline Color rgba(float r, float g, float b, float a) { return Color{ r, g, b, a }; }

    // matches the pipeline vertex layout: vec3 pos, vec4 color, vec2 uv
    struct Vertex {
        float x, y, z;
        float r, g, b, a;
        float u, v;
    };

    inline Vertex vtx(float x, float y, float z, Color c, float u, float v) {
        return Vertex{ x, y, z, c.r, c.g, c.b, c.a, u, v };
    }

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

    // one vertex stream (3d or 2d) with its own texture batches
    struct Stream {
        Vertex* verts = NULL;
        u32 vert_count = 0;
        Batch* batches = NULL;
        u32 batch_count = 0;
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
        casino::Pipeline pipe3d;
        casino::Pipeline pipe2d;
        casino::GpuBuffer vbuf[casino::MAX_FRAMES_IN_FLIGHT];
        cam::Camera camera = cam::defaultCamera();

        Stream s3;
        Stream s2;

        Primitives() = default;
        ~Primitives() { shutdown(); }
        Primitives(const Primitives&) = delete;
        Primitives& operator=(const Primitives&) = delete;

        bool init(const casino::Device& device, const TextureCache& textures); // BLACKBOX
        void shutdown();

        void begin();
        void setCamera(const cam::Camera& c) { camera = c; }

        // 3d, world space, depth tested
        void drawTriangle3d(const float p[9], Color c);
        void drawQuad3d(const float p[12], Color c);
        void drawBox3d(float x, float y, float z, float w, float h, float d, Color c);

        // 2d, relative screen coords, drawn on top of the 3d pass
        void drawRect(float x, float y, float w, float h, Color c);
        void drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color c);
        void drawImage(TextureId id, float x, float y, float w, float h, Anchor a = Anchor::top_left);
        void drawImage(const casino::Texture& tex, float x, float y, float w, float h, Anchor a = Anchor::top_left);
        void drawImageFit(TextureId id, float cx, float cy, float w, Anchor a = Anchor::center); // BLACKBOX
        void render(VkCommandBuffer cmd, u32 frame_slot); // BLACKBOX

    private:
        void pushVertex(Stream& s, VkDescriptorSet set, const Vertex& v);
        void drawBatches(VkCommandBuffer cmd, const casino::Pipeline& pipe, const Stream& s,
            VkBuffer buf, VkDeviceSize offset, const float* mat);
    };

    inline bool Primitives::init(const casino::Device& device, const TextureCache& textures) { // BLACKBOX
        shutdown();
        dev = &device;
        cache = &textures;
        VkDevice d = device.device;

        if (!pipe3d.create(device, prim_vert_data, prim_vert_size * 4,
                prim_frag_data, prim_frag_size * 4, textures.set_layout, true))
            return false;
        if (!pipe2d.create(device, prim_vert_data, prim_vert_size * 4,
                prim_frag_data, prim_frag_size * 4, textures.set_layout, false))
            return false;

        for (u32 i = 0; i < casino::MAX_FRAMES_IN_FLIGHT; i++) {
            // 3d stream first, 2d stream right after it
            if (!vbuf[i].create(d, device.gpu, 2ull * MAX_VERTS * sizeof(Vertex),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return false;
        }

        s3.verts = (Vertex*)malloc(MAX_VERTS * sizeof(Vertex));
        s3.batches = (Batch*)malloc(MAX_TEXTURES * sizeof(Batch));
        s2.verts = (Vertex*)malloc(MAX_VERTS * sizeof(Vertex));
        s2.batches = (Batch*)malloc(MAX_TEXTURES * sizeof(Batch));
        return true;
    }

    inline void Primitives::shutdown() {
        if (dev && dev->device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(dev->device);
        pipe3d.destroy();
        pipe2d.destroy();
        for (u32 i = 0; i < casino::MAX_FRAMES_IN_FLIGHT; i++)
            vbuf[i].destroy();
        free(s3.verts);
        free(s3.batches);
        free(s2.verts);
        free(s2.batches);
        s3 = Stream();
        s2 = Stream();
        cache = NULL;
        dev = NULL;
    }

    inline void Primitives::begin() {
        s3.vert_count = s3.batch_count = 0;
        s2.vert_count = s2.batch_count = 0;
    }

    inline void Primitives::pushVertex(Stream& s, VkDescriptorSet set, const Vertex& v) {
        if (s.vert_count >= MAX_VERTS)
            return; // stream full, dropped
        if (s.batch_count == 0 || s.batches[s.batch_count - 1].set != set) {
            if (s.batch_count >= MAX_TEXTURES)
                return;
            s.batches[s.batch_count].set = set;
            s.batches[s.batch_count].first = s.vert_count;
            s.batches[s.batch_count].count = 0;
            s.batch_count += 1;
        }
        s.verts[s.vert_count++] = v;
        s.batches[s.batch_count - 1].count += 1;
    }

    // ------------------------------------------------------------ 3d

    inline void Primitives::drawTriangle3d(const float p[9], Color c) {
        VkDescriptorSet set = cache->white().set;
        pushVertex(s3, set, vtx(p[0], p[1], p[2], c, 0.0f, 0.0f));
        pushVertex(s3, set, vtx(p[3], p[4], p[5], c, 0.0f, 0.0f));
        pushVertex(s3, set, vtx(p[6], p[7], p[8], c, 0.0f, 0.0f));
    }

    inline void Primitives::drawQuad3d(const float p[12], Color c) {
        VkDescriptorSet set = cache->white().set;
        Vertex a = vtx(p[0], p[1], p[2], c, 0.0f, 0.0f);
        Vertex b = vtx(p[3], p[4], p[5], c, 0.0f, 0.0f);
        Vertex cc = vtx(p[6], p[7], p[8], c, 0.0f, 0.0f);
        Vertex d = vtx(p[9], p[10], p[11], c, 0.0f, 0.0f);
        pushVertex(s3, set, a);
        pushVertex(s3, set, b);
        pushVertex(s3, set, cc);
        pushVertex(s3, set, a);
        pushVertex(s3, set, cc);
        pushVertex(s3, set, d);
    }

    // axis aligned box from (x,y,z) min corner, faces shaded by direction (no lighting yet)
    inline void Primitives::drawBox3d(float x, float y, float z, float w, float h, float d, Color c) {
        float x1 = x + w, y1 = y + h, z1 = z + d;
        // shade factor per face: top full, front and side darker, bottom darkest
        Color top = rgba(c.r, c.g, c.b, c.a);
        Color front = rgba(c.r * 0.8f, c.g * 0.8f, c.b * 0.8f, c.a);
        Color side = rgba(c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, c.a);
        Color bottom = rgba(c.r * 0.4f, c.g * 0.4f, c.b * 0.4f, c.a);

        // +y top
        float top_q[12] = { x, y1, z, x1, y1, z, x1, y1, z1, x, y1, z1 };
        // -y bottom
        float bot_q[12] = { x, y, z, x, y, z1, x1, y, z1, x1, y, z };
        // +z front
        float front_q[12] = { x, y, z1, x1, y, z1, x1, y1, z1, x, y1, z1 };
        // -z back
        float back_q[12] = { x, y, z, x, y1, z, x1, y1, z, x1, y, z };
        // +x right
        float right_q[12] = { x1, y, z, x1, y1, z, x1, y1, z1, x1, y, z1 };
        // -x left
        float left_q[12] = { x, y, z, x, y, z1, x, y1, z1, x, y1, z };
        drawQuad3d(top_q, top);
        drawQuad3d(bot_q, bottom);
        drawQuad3d(front_q, front);
        drawQuad3d(back_q, front);
        drawQuad3d(right_q, side);
        drawQuad3d(left_q, side);
    }

    // ------------------------------------------------------------ 2d

    inline void Primitives::drawRect(float x, float y, float w, float h, Color c) {
        VkDescriptorSet set = cache->white().set;
        Vertex tl = vtx(x, y, 0.0f, c, 0.0f, 0.0f);
        Vertex tr = vtx(x + w, y, 0.0f, c, 0.0f, 0.0f);
        Vertex br = vtx(x + w, y + h, 0.0f, c, 0.0f, 0.0f);
        Vertex bl = vtx(x, y + h, 0.0f, c, 0.0f, 0.0f);
        pushVertex(s2, set, tl);
        pushVertex(s2, set, tr);
        pushVertex(s2, set, br);
        pushVertex(s2, set, tl);
        pushVertex(s2, set, br);
        pushVertex(s2, set, bl);
    }

    inline void Primitives::drawTriangle(float ax, float ay, float bx, float by,
        float cx, float cy, Color c) {
        VkDescriptorSet set = cache->white().set;
        pushVertex(s2, set, vtx(ax, ay, 0.0f, c, 0.0f, 0.0f));
        pushVertex(s2, set, vtx(bx, by, 0.0f, c, 0.0f, 0.0f));
        pushVertex(s2, set, vtx(cx, cy, 0.0f, c, 0.0f, 0.0f));
    }

    inline void Primitives::drawImage(const casino::Texture& tex, float x, float y,
        float w, float h, Anchor a) {
        applyAnchor(x, y, w, h, a);
        // full image, uv (0,0)=top-left .. (1,1)=bottom-right, white tint
        Color white = rgba(1.0f, 1.0f, 1.0f, 1.0f);
        Vertex tl = vtx(x, y, 0.0f, white, 0.0f, 0.0f);
        Vertex tr = vtx(x + w, y, 0.0f, white, 1.0f, 0.0f);
        Vertex br = vtx(x + w, y + h, 0.0f, white, 1.0f, 1.0f);
        Vertex bl = vtx(x, y + h, 0.0f, white, 0.0f, 1.0f);
        VkDescriptorSet set = tex.set;
        pushVertex(s2, set, tl);
        pushVertex(s2, set, tr);
        pushVertex(s2, set, br);
        pushVertex(s2, set, tl);
        pushVertex(s2, set, br);
        pushVertex(s2, set, bl);
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

    // ------------------------------------------------------------ render

    inline void Primitives::drawBatches(VkCommandBuffer cmd, const casino::Pipeline& pipe,
        const Stream& s, VkBuffer buf, VkDeviceSize offset, const float* mat) {
        if (s.vert_count == 0)
            return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);
        vkCmdPushConstants(cmd, pipe.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mat);
        for (u32 i = 0; i < s.batch_count; i++) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipe.layout, 0, 1, &s.batches[i].set, 0, NULL);
            vkCmdDraw(cmd, s.batches[i].count, 1, s.batches[i].first, 0);
        }
    }

    inline void Primitives::render(VkCommandBuffer cmd, u32 frame_slot) { // BLACKBOX
        if (dev == NULL || (s3.vert_count == 0 && s2.vert_count == 0))
            return;

        // both streams go into the same mapped buffer: 3d at 0, 2d right after
        VkDeviceSize off2 = (VkDeviceSize)s3.vert_count * sizeof(Vertex);
        char* dst = (char*)vbuf[frame_slot].mapped;
        if (s3.vert_count)
            memcpy(dst, s3.verts, (size_t)s3.vert_count * sizeof(Vertex));
        if (s2.vert_count)
            memcpy(dst + off2, s2.verts, (size_t)s2.vert_count * sizeof(Vertex));

        VkExtent2D e = dev->currentExtent();
        float aspect = (e.height != 0) ? (float)e.width / (float)e.height : 1.0f;
        cam::Mat4 vp = cam::viewProj(camera, aspect);
        cam::Mat4 ortho = cam::ortho01();
        VkBuffer buf = vbuf[frame_slot].buffer;

        // 3d pass: depth tested, camera view-projection
        drawBatches(cmd, pipe3d, s3, buf, 0, vp.m);
        // 2d pass: no depth test, drawn over the 3d scene. firstVertex in the
        // batches is relative to the stream start, the bind offset covers the rest
        drawBatches(cmd, pipe2d, s2, buf, off2, ortho.m);
    }

} // namespace prim
