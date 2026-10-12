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
        tri3d,
        quad3d,
        box3d,
    };

    struct DrawOp {
        OpKind kind;
        Anchor anchor;
        bool fit; // image only: keep the pixel aspect at replay time
        TextureId tex;
        Color color;
        float x, y, w, h;
        float ax, ay, bx, by, cx, cy;
        float p3[12]; // 3d: triangle uses 9, quad uses 12, box uses x,y,z,w,h,d in p3[0..5]
    };

    class DrawList {
    public:
        static const u32 MAX_OPS = 4096;
        DrawOp ops[MAX_OPS];
        u32 count = 0;
        cam::Camera camera = cam::defaultCamera();

        DrawList() = default;
        void clear() { count = 0; }

        void setCamera(const cam::Camera& c) { camera = c; }

        void rect(float x, float y, float w, float h, Color c);
        void triangle(float ax, float ay, float bx, float by, float cx, float cy, Color c);
        void image(TextureId id, float x, float y, float w, float h, Anchor a = Anchor::top_left);
        void imageFit(TextureId id, float cx, float cy, float w, Anchor a = Anchor::center);

        void triangle3d(const float p[9], Color c);
        void quad3d(const float p[12], Color c);
        void box3d(float x, float y, float z, float w, float h, float d, Color c);

        void replay(Primitives& prims);

    private:
        DrawOp* next() {
            if (count >= MAX_OPS)
                return NULL;
            return &ops[count++];
        }
    };

    inline void DrawList::rect(float x, float y, float w, float h, Color c) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::rect;
        o->color = c;
        o->x = x; o->y = y; o->w = w; o->h = h;
    }

    inline void DrawList::triangle(float ax, float ay, float bx, float by,
        float cx, float cy, Color c) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::triangle;
        o->color = c;
        o->ax = ax; o->ay = ay; o->bx = bx; o->by = by; o->cx = cx; o->cy = cy;
    }

    inline void DrawList::image(TextureId id, float x, float y, float w, float h, Anchor a) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::image;
        o->tex = id;
        o->anchor = a;
        o->fit = false;
        o->x = x; o->y = y; o->w = w; o->h = h;
    }

    inline void DrawList::imageFit(TextureId id, float cx, float cy, float w, Anchor a) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::image;
        o->tex = id;
        o->anchor = a;
        o->fit = true;
        o->x = cx; o->y = cy; o->w = w;
    }

    inline void DrawList::triangle3d(const float p[9], Color c) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::tri3d;
        o->color = c;
        for (int i = 0; i < 9; i++) o->p3[i] = p[i];
    }

    inline void DrawList::quad3d(const float p[12], Color c) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::quad3d;
        o->color = c;
        for (int i = 0; i < 12; i++) o->p3[i] = p[i];
    }

    inline void DrawList::box3d(float x, float y, float z, float w, float h, float d, Color c) {
        DrawOp* o = next();
        if (!o) return;
        o->kind = OpKind::box3d;
        o->color = c;
        o->p3[0] = x; o->p3[1] = y; o->p3[2] = z;
        o->p3[3] = w; o->p3[4] = h; o->p3[5] = d;
    }

    inline void DrawList::replay(Primitives& prims) {
        prims.setCamera(camera);
        for (u32 i = 0; i < count; i++) {
            const DrawOp& o = ops[i];
            switch (o.kind) {
                case OpKind::rect:
                    prims.drawRect(o.x, o.y, o.w, o.h, o.color);
                    break;
                case OpKind::triangle:
                    prims.drawTriangle(o.ax, o.ay, o.bx, o.by, o.cx, o.cy, o.color);
                    break;
                case OpKind::image:
                    if (o.fit)
                        prims.drawImageFit(o.tex, o.x, o.y, o.w, o.anchor);
                    else
                        prims.drawImage(o.tex, o.x, o.y, o.w, o.h, o.anchor);
                    break;
                case OpKind::tri3d:
                    prims.drawTriangle3d(o.p3, o.color);
                    break;
                case OpKind::quad3d:
                    prims.drawQuad3d(o.p3, o.color);
                    break;
                case OpKind::box3d:
                    prims.drawBox3d(o.p3[0], o.p3[1], o.p3[2], o.p3[3], o.p3[4], o.p3[5], o.color);
                    break;
            }
        }
    }

} // namespace prim
