#version 450 core

layout(location = 0) in vec2 inTexCoords;

layout(location = 0) out vec4 outFragColor ;

layout(set = 0, binding = 0) uniform sampler2D texture_sampler;

void main() {
    outFragColor = texture(texture_sampler, inTexCoords);
}
