#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;

layout(set = 0, binding = 0) uniform sampler2D u_tex;

layout(location = 0) out vec4 out_color;

void main() {
    // white texel for untextured prims gives v_color, image gives the texture
    out_color = v_color * texture(u_tex, v_uv);
}
