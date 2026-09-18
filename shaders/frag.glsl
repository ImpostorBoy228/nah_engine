#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;
layout(location = 2) flat in uint fragTexIndex;

layout(set = 0, binding = 1) uniform sampler2D textures[16];

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    vec3 camera_pos;
    float _pad0;
    vec3 sun_dir;
    float _pad1;
    float time;
    float _pad2;
    float _pad3;
    float _pad4;
} frame;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(textures[fragTexIndex], fragUv) * fragColor;
}
