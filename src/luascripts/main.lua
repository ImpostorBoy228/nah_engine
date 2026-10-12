local ffi = require("ffi")
ffi.cdef[[
    typedef long long i64;
    typedef struct NahCtx NahCtx;
    void nah_bind(NahCtx* ctx);
    i64 nah_tex(const char* path);
    void nah_clear();
    void nah_rect(float x, float y, float w, float h, float r, float g, float b, float a);
    void nah_triangle(float ax, float ay, float bx, float by, float cx, float cy,
        float r, float g, float b, float a);
    void nah_image(i64 tex, float x, float y, float w, float h, int anchor);
    void nah_image_fit(i64 tex, float cx, float cy, float w, int anchor);
    void nah_tri3d(float ax, float ay, float az, float bx, float by, float bz,
        float cx, float cy, float cz, float r, float g, float b, float a);
    void nah_quad3d(float ax, float ay, float az, float bx, float by, float bz,
        float cx, float cy, float cz, float dx, float dy, float dz,
        float r, float g, float b, float a);
    void nah_box3d(float x, float y, float z, float w, float h, float d,
        float r, float g, float b, float a);
    void nah_camera(float x, float y, float z, float yaw, float pitch, float fov);
]]

-- anchors: 0 top_left, 1 top_right, 2 bottom_left, 3 bottom_right, 4 center
local CENTER = 4

local uv = ffi.C.nah_tex("assets/uv.png")

ffi.C.nah_clear()

-- camera: stands back and up, looks slightly down at the origin
ffi.C.nah_camera(0.0, 2.0, 6.0, 0.0, -0.3, 1.0471976)

-- 3d: a floor quad and two boxes, world units
ffi.C.nah_quad3d(-4.0, 0.0, -4.0, 4.0, 0.0, -4.0, 4.0, 0.0, 4.0, -4.0, 0.0, 4.0,
    0.20, 0.22, 0.25, 1.0)
ffi.C.nah_box3d(-1.0, 0.0, -1.0, 1.0, 1.0, 1.0, 0.90, 0.25, 0.35, 1.0)
ffi.C.nah_box3d(1.0, 0.0, 0.0, 0.8, 1.6, 0.8, 0.25, 0.70, 0.40, 1.0)
ffi.C.nah_tri3d(-2.0, 0.0, 1.5, -1.0, 0.0, 1.5, -1.5, 1.2, 1.5, 0.25, 0.45, 0.90, 1.0)

-- 2d HUD: always drawn over the 3d scene
ffi.C.nah_rect(0.0, 0.0, 1.0, 0.10, 0.15, 0.15, 0.20, 0.85)
ffi.C.nah_triangle(0.80, 0.20, 0.95, 0.20, 0.875, 0.42, 0.25, 0.75, 0.35, 1.0)

if uv ~= 0 then
    ffi.C.nah_image_fit(uv, 0.12, 0.85, 0.18, 0)
end
