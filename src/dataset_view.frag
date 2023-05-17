#version 450 core

layout(location = 0) in vec2 inTexCoords;

layout(location = 0) out vec4 outFragColor ;

layout(set = 0, binding = 0) uniform sampler3D channels[];

layout(push_constant) uniform uConstants {
    float minimum;
    float difference;
    float depth;
    uint channel_count;
} constants;

void main() {
    vec4 sampled;
    if (constants.channel_count == 1) {
        sampled = texture(channels[0], vec3(inTexCoords, constants.depth));
    } else {
        sampled = vec4(
            texture(channels[0], vec3(inTexCoords, constants.depth)).r,
            texture(channels[1], vec3(inTexCoords, constants.depth)).r,
            texture(channels[2], vec3(inTexCoords, constants.depth)).r,
            1.0
        );
    }
    outFragColor = (sampled - constants.minimum) / constants.difference;
    outFragColor .a = 1.0;
}
