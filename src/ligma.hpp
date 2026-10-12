#pragma once
#include <cstdio>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "luajit.h"
}

#include "render/textures.hpp"
#include "render/camera.hpp"
#include "render/primitives.hpp"
#include "render/drawlist.hpp"

class Ligma {
private:
    lua_State* L;
public:
    Ligma() {
        L = luaL_newstate();
        if (L) luaL_openlibs(L);
    }

    void ligma_execute(const char* script) {
        if (luaL_dofile(L, script) != 0) {
            fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    ~Ligma() {
        if (L) lua_close(L);
    }

    Ligma(const Ligma&) = delete;
    Ligma& operator=(const Ligma&) = delete;
};

// the render objects main() owns, handed to lua through nah_bind. the ffi passes
// them back as opaque pointers so the wrappers below never touch globals.
struct NahCtx {
    casino::Device* dev;
    prim::TextureCache* tex;
    prim::Primitives* prims;
    prim::DrawList* list;
};

// set by nah_bind, read by every wrapper below
static NahCtx* g_nah = NULL;

extern "C" {

    void nah_bind(NahCtx* ctx) {
        g_nah = ctx;
    }

    // returns a packed texture handle (int64) or 0 on failure
    long long nah_tex(const char* path) {
        if (g_nah == NULL || g_nah->tex == NULL)
            return 0;
        return prim::packId(g_nah->tex->acquire(path));
    }

    void nah_clear() {
        if (g_nah != NULL && g_nah->list != NULL)
            g_nah->list->clear();
    }

    void nah_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
        if (g_nah != NULL && g_nah->list != NULL)
            g_nah->list->rect(x, y, w, h, prim::rgba(r, g, b, a));
    }

    void nah_triangle(float ax, float ay, float bx, float by, float cx, float cy,
        float r, float g, float b, float a) {
        if (g_nah != NULL && g_nah->list != NULL)
            g_nah->list->triangle(ax, ay, bx, by, cx, cy, prim::rgba(r, g, b, a));
    }

    // anchor: 0 top_left, 1 top_right, 2 bottom_left, 3 bottom_right, 4 center
    void nah_image(long long handle, float x, float y, float w, float h, int anchor) {
        if (g_nah != NULL && g_nah->list != NULL)
            g_nah->list->image(prim::unpackId(handle), x, y, w, h, (prim::Anchor)anchor);
    }

    // fit: keeps the pixel aspect on any window, anchor is a center-style point
    void nah_image_fit(long long handle, float cx, float cy, float w, int anchor) {
        if (g_nah != NULL && g_nah->list != NULL)
            g_nah->list->imageFit(prim::unpackId(handle), cx, cy, w, (prim::Anchor)anchor);
    }

    // 3d: world space, depth tested, drawn before the 2d overlay
    void nah_tri3d(float ax, float ay, float az, float bx, float by, float bz,
        float cx, float cy, float cz, float r, float g, float b, float a) {
        if (g_nah != NULL && g_nah->list != NULL) {
            float p[9] = { ax, ay, az, bx, by, bz, cx, cy, cz };
            g_nah->list->triangle3d(p, prim::rgba(r, g, b, a));
        }
    }

    void nah_quad3d(float ax, float ay, float az, float bx, float by, float bz,
        float cx, float cy, float cz, float dx, float dy, float dz,
        float r, float g, float b, float a) {
        if (g_nah != NULL && g_nah->list != NULL) {
            float p[12] = { ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz };
            g_nah->list->quad3d(p, prim::rgba(r, g, b, a));
        }
    }

    void nah_box3d(float x, float y, float z, float w, float h, float d,
        float r, float g, float b, float a) {
        if (g_nah != NULL && g_nah->list != NULL)
            g_nah->list->box3d(x, y, z, w, h, d, prim::rgba(r, g, b, a));
    }

    // camera: position, yaw/pitch in radians, vertical fov in radians
    void nah_camera(float x, float y, float z, float yaw, float pitch, float fov) {
        if (g_nah != NULL && g_nah->list != NULL) {
            cam::Camera& c = g_nah->list->camera;
            c.x = x; c.y = y; c.z = z;
            c.yaw = yaw; c.pitch = pitch;
            c.fov = fov;
        }
    }
}
