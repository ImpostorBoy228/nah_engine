local ffi = require("ffi")
ffi.cdef[[
    typedef long long i64;
    typedef struct GachiMan GachiMan;
    struct GachiMan { int cumCounter; char* name; };
    void gachicum(GachiMan* g, int c);
    GachiMan gachicreate(const char* name);
    void gachiprint(GachiMan* g);

    typedef struct NahCtx NahCtx;
    void nah_bind(NahCtx* ctx);
    i64 nah_tex(const char* path);
    void nah_clear();
    void nah_rect(float x, float y, float w, float h, float r, float g, float b, float a);
    void nah_triangle(float ax, float ay, float bx, float by, float cx, float cy,
        float r, float g, float b, float a);
    void nah_image(i64 tex, float x, float y, float w, float h, int anchor);
    void nah_image_fit(i64 tex, float cx, float cy, float w, int anchor);
]]

-- anchors: 0 top_left, 1 top_right, 2 bottom_left, 3 bottom_right, 4 center
local CENTER = 4

-- old gachi demo still runs
local bo = ffi.C.gachicreate("Bo Sinn")
ffi.C.gachicum(bo, 666)
ffi.C.gachiprint(bo)

local uv = ffi.C.nah_tex("assets/uv.png")

ffi.C.nah_clear()

-- a background strip + a rect + a triangle, all relative coords
ffi.C.nah_rect(0.0, 0.0, 1.0, 0.12, 0.15, 0.15, 0.20, 1.0)
ffi.C.nah_rect(0.05, 0.25, 0.25, 0.25, 0.90, 0.25, 0.35, 1.0)
ffi.C.nah_triangle(0.65, 0.20, 0.90, 0.20, 0.775, 0.48, 0.25, 0.75, 0.35, 1.0)

-- keep the image's pixel aspect on a non square window, centered
if uv ~= 0 then
    ffi.C.nah_image_fit(uv, 0.5, 0.72, 0.55, CENTER)
end
