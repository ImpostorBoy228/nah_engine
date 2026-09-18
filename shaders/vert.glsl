#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
layout(location = 3) in uint texIndex;

layout(set = 0, binding = 0) uniform FrameData {
    mat4 projection;
    vec2 screen_size;
    float time;
} frame;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out uint fragTexIndex;

void main() {
    gl_Position = frame.projection * vec4(position, 0.0, 1.0);
    fragUv = uv;
    fragColor = color;
    fragTexIndex = texIndex;
}