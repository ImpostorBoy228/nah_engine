#version 450

// relative coords: (0,0)=top-left, (1,1)=bottom-right, y-down.
// map to vulkan ndc: rel_y=0 -> ndc.y=-1 (top row), rel_y=1 -> +1 (bottom row)
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main() {
    vec2 ndc = vec2(in_pos.x * 2.0 - 1.0, in_pos.y * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = in_color;
    v_uv = in_uv;
}
