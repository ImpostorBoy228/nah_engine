#version 450

// 3d: world space, view_proj from the camera. 2d: ortho matrix over (0,0)..(1,1), y-down.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;

layout(push_constant) uniform Push {
    mat4 view_proj;
} pc;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main() {
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
    v_color = in_color;
    v_uv = in_uv;
}
