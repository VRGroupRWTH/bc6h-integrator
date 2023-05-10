#version 450 core

layout(location = 0) in vec2 inTexCoords;

layout(location = 0) out vec4 outFragColor ;

layout(set = 0, binding = 0) uniform sampler3D texture_sampler;

layout(push_constant) uniform uConstants {
    float minimum;
    float difference;
    float depth;
} constants;

void main() {
    vec4 sampled = texture(texture_sampler, vec3(inTexCoords, constants.depth));
    outFragColor = (sampled - constants.minimum) / constants.difference;
    outFragColor .a = 1.0;
    /* outFragColor = vec4(1.0, 0.0, 1.0, 1.0); */
}
