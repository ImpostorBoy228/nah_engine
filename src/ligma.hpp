#pragma once
#include <cstdio>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "luajit.h"
}

#include "render/textures.hpp"
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

struct GachiMan {
    int cumCounter;
    char* name;
};

extern "C" {
    GachiMan gachicreate(char* name) {
        return GachiMan {0,name};
    }
    void gachicum(GachiMan* g, int c) {
        g->cumCounter+=c;
    }
    void gachiprint(GachiMan* g) {
        printf("%s\n%d\n",g->name,g->cumCounter);
    }
}

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
}
