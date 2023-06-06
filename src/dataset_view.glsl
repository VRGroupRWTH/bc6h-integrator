layout(location = 0) in vec2 inTexCoords;

layout(location = 0) out vec4 outFragColor ;

#if defined(DATASET_VIEW_IMAGE)
layout(set = 0, binding = 0) uniform sampler3D channels[];
#endif

#if defined(DATASET_VIEW_ANALYTIC)
#include "analytic_vector_field.glsl"
#endif

layout(push_constant) uniform uConstants {
    vec2 resolution; //Resolution in cells / meter
    float minimum;
    float difference;
    float depth;
    float time;
    uint channel_count;
} constants;

void main() {
    vec4 sampled;

#if defined(DATASET_VIEW_IMAGE)
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
#elif defined(DATASET_VIEW_ANALYTIC)
    sampled.xyz = analytic_vector_field(vec4(inTexCoords * constants.resolution, constants.depth, constants.time));
#else
#error "define something"
#endif

    outFragColor = (sampled - constants.minimum) / constants.difference;
    outFragColor .a = 1.0;
}
