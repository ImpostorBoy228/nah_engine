#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
layout(location = 3) in uint texIndex;

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 proj;
    mat4 view_proj;        // proj * view, baked on CPU
    vec3 camera_pos;       // for specular / IBL
    float _pad0;
    vec3 sun_dir;          // light direction
    float _pad1;
    float time;
    float _pad2;
    float _pad3;
    float _pad4;
} frame;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out uint fragTexIndex;

void main() {
    gl_Position = frame.view_proj * vec4(position, 0.0, 1.0);
    fragUv = uv;
    fragColor = color;
    fragTexIndex = texIndex;
}
