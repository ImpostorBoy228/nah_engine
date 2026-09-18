#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;
layout(location = 2) flat in uint fragTexIndex;

layout(set = 0, binding = 1) uniform sampler2D textures[16];

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(textures[fragTexIndex], fragUv) * fragColor;
}