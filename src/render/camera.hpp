#pragma once

#include <math.h>

// column-major 4x4 matrices and the camera that builds the 3d view-projection
namespace cam {

    struct Mat4 {
        float m[16];
    };

    struct Camera {
        float x, y, z;
        float yaw, pitch; // radians, yaw 0 looks down -z
        float fov;        // vertical, radians
        float near_plane, far_plane;
    };

    inline Camera defaultCamera() {
        return Camera{ 0.0f, 1.0f, 5.0f, 0.0f, 0.0f, 1.0471976f, 0.05f, 1000.0f };
    }

    inline Mat4 identity() {
        Mat4 o = {};
        o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f;
        return o;
    }

    // out = a * b
    inline Mat4 mul(const Mat4& a, const Mat4& b) {
        Mat4 o = {};
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) {
                float s = 0.0f;
                for (int k = 0; k < 4; k++)
                    s += a.m[k * 4 + r] * b.m[c * 4 + k];
                o.m[c * 4 + r] = s;
            }
        return o;
    }

    // maps (0,0)..(1,1) to vulkan ndc, z stays at 0
    inline Mat4 ortho01() {
        Mat4 o = identity();
        o.m[0] = 2.0f;
        o.m[5] = 2.0f;
        o.m[10] = 0.0f;
        o.m[12] = -1.0f;
        o.m[13] = -1.0f;
        return o;
    }

    // right-handed, depth 0..1, y flipped for vulkan
    inline Mat4 perspective(float fov, float aspect, float near_p, float far_p) {
        Mat4 o = {};
        float f = 1.0f / tanf(fov * 0.5f);
        o.m[0] = f / aspect;
        o.m[5] = -f;
        o.m[10] = far_p / (near_p - far_p);
        o.m[11] = -1.0f;
        o.m[14] = near_p * far_p / (near_p - far_p);
        return o;
    }

    // view matrix for eye looking along the unit vector fwd, world up is +y
    inline Mat4 viewFromForward(float ex, float ey, float ez, float fx, float fy, float fz) {
        // right = normalize(cross(fwd, up))
        float sx = -fz, sy = 0.0f, sz = fx;
        float sl = sqrtf(sx * sx + sy * sy + sz * sz);
        if (sl > 0.0f) { sx /= sl; sy /= sl; sz /= sl; }
        // true up = cross(right, fwd)
        float ux = sy * fz - sz * fy;
        float uy = sz * fx - sx * fz;
        float uz = sx * fy - sy * fx;

        Mat4 o = identity();
        o.m[0] = sx;  o.m[4] = sy;  o.m[8] = sz;
        o.m[1] = ux;  o.m[5] = uy;  o.m[9] = uz;
        o.m[2] = -fx; o.m[6] = -fy; o.m[10] = -fz;
        o.m[12] = -(sx * ex + sy * ey + sz * ez);
        o.m[13] = -(ux * ex + uy * ey + uz * ez);
        o.m[14] = fx * ex + fy * ey + fz * ez;
        return o;
    }

    inline Mat4 viewProj(const Camera& c, float aspect) {
        float pitch = c.pitch;
        if (pitch > 1.5f) pitch = 1.5f;
        if (pitch < -1.5f) pitch = -1.5f;
        float fx = sinf(c.yaw) * cosf(pitch);
        float fy = sinf(pitch);
        float fz = -cosf(c.yaw) * cosf(pitch);
        Mat4 view = viewFromForward(c.x, c.y, c.z, fx, fy, fz);
        Mat4 proj = perspective(c.fov, aspect > 0.0f ? aspect : 1.0f, c.near_plane, c.far_plane);
        return mul(proj, view);
    }

} // namespace cam
