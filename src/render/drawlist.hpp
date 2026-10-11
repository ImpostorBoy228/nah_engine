#pragma once

#include "render/primitives.hpp"

// retained draw list: record ops once, replay them into the batcher every frame.
// cheaper than re-issuing thousands of ffi calls from lua for a static scene,
// dynamic stuff can still skip this and hit the batcher directly.
namespace prim {

    enum class OpKind {
        rect,
        triangle,
        image,
    };

    struct DrawOp {
        OpKind kind;
        Anchor anchor;
        bool fit; // image only: keep the pixel aspect at replay time
        TextureId tex;
        Color color;
        float x, y, w, h;
        float ax, ay, bx, by, cx, cy;
    };

    class DrawList {
    public:
        static const u32 MAX_OPS = 4096;
        DrawOp ops[MAX_OPS];
        u32 count = 0;

        DrawList() = default;
        void clear() { count = 0; }

        void rect(float x, float y, float w, float h, Color c);
        void triangle(float ax, float ay, float bx, float by, float cx, float cy, Color c);
        void image(TextureId id, float x, float y, float w, float h, Anchor a = Anchor::top_left);
        void imageFit(TextureId id, float cx, float cy, float w, Anchor a = Anchor::center);
        void replay(Primitives& prims);
    };

    inline void DrawList::rect(float x, float y, float w, float h, Color c) {
        if (count >= MAX_OPS)
            return;
        DrawOp& o = ops[count++];
        o.kind = OpKind::rect;
        o.color = c;
        o.x = x; o.y = y; o.w = w; o.h = h;
    }

    inline void DrawList::triangle(float ax, float ay, float bx, float by,
        float cx, float cy, Color c) {
        if (count >= MAX_OPS)
            return;
        DrawOp& o = ops[count++];
        o.kind = OpKind::triangle;
        o.color = c;
        o.ax = ax; o.ay = ay; o.bx = bx; o.by = by; o.cx = cx; o.cy = cy;
    }

    inline void DrawList::image(TextureId id, float x, float y, float w, float h, Anchor a) {
        if (count >= MAX_OPS)
            return;
        DrawOp& o = ops[count++];
        o.kind = OpKind::image;
        o.tex = id;
        o.anchor = a;
        o.fit = false;
        o.x = x; o.y = y; o.w = w; o.h = h;
    }

    inline void DrawList::imageFit(TextureId id, float cx, float cy, float w, Anchor a) {
        if (count >= MAX_OPS)
            return;
        DrawOp& o = ops[count++];
        o.kind = OpKind::image;
        o.tex = id;
        o.anchor = a;
        o.fit = true;
        o.x = cx; o.y = cy; o.w = w;
    }

    inline void DrawList::replay(Primitives& prims) {
        for (u32 i = 0; i < count; i++) {
            const DrawOp& o = ops[i];
            if (o.kind == OpKind::rect) {
                prims.drawRect(o.x, o.y, o.w, o.h, o.color);
            } else if (o.kind == OpKind::triangle) {
                prims.drawTriangle(o.ax, o.ay, o.bx, o.by, o.cx, o.cy, o.color);
            } else if (o.fit) {
                prims.drawImageFit(o.tex, o.x, o.y, o.w, o.anchor);
            } else {
                prims.drawImage(o.tex, o.x, o.y, o.w, o.h, o.anchor);
            }
        }
    }

} // namespace prim
