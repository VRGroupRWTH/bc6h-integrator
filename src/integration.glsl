layout(push_constant) uniform Constants {
    vec4 dataset_dimensions;
    uvec3 seed_dimensions;
    float dt;
    uint total_step_count;
    uint first_step;
    uint step_count;
}
constants;

layout(std140, set = 0, binding = 0) buffer line_buffer {
    vec4 vertices[];
};

layout(std140, set = 0, binding = 1) buffer max_velocity_magnitude_buffer {
    uint max_velocity_magnitude;
};

struct DrawIndirectCommand {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

layout(std140, set = 0, binding = 2) buffer indirect_buffer {
    DrawIndirectCommand indirect_draw[];
};

layout(local_size_x_id = 0) in;
layout(local_size_y_id = 1) in;
layout(local_size_z_id = 2) in;

layout(constant_id = 3) const uint TIME_STEPS = 400;

layout(constant_id = 4) const bool EXPLICIT_INTERPOLATION = false;

#if defined(DATA_RAW_TEXTURES)
layout(set = 0, binding = 3) uniform sampler3D dataset_x[TIME_STEPS];
layout(set = 0, binding = 4) uniform sampler3D dataset_y[TIME_STEPS];
layout(set = 0, binding = 5) uniform sampler3D dataset_z[TIME_STEPS];
#elif defined(DATA_BC6H_TEXTURE)
layout(set = 0, binding = 3) uniform sampler2DArray dataset[TIME_STEPS];
#elif defined(DATA_ANALYTIC)
#include "analytic_vector_field.glsl"
#else
#error "define something"
#endif

#if defined(DATA_RAW_TEXTURES)
float sample_explicit(sampler3D dataset_sampler, vec3 coordinates) {
    ivec3 base_coordinate = ivec3(floor(coordinates));
    vec3 filter_weight = fract(coordinates);

    float sample_000 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 0, 0), 0).x;
    float sample_100 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 0, 0), 0).x;
    float sample_w00 = mix(sample_000, sample_100, filter_weight.x);

    float sample_010 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 1, 0), 0).x;
    float sample_110 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 1, 0), 0).x;
    float sample_w10 = mix(sample_010, sample_110, filter_weight.x);
    float sample_ww0 = mix(sample_w00, sample_w10, filter_weight.y);

    float sample_001 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 0, 1), 0).x;
    float sample_101 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 0, 1), 0).x;
    float sample_w01 = mix(sample_001, sample_101, filter_weight.x);

    float sample_011 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 1, 1), 0).x;
    float sample_111 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 1, 1), 0).x;
    float sample_w11 = mix(sample_011, sample_111, filter_weight.x);
    float sample_ww1 = mix(sample_w01, sample_w11, filter_weight.y);

    float sample_www = mix(sample_ww0, sample_ww1, filter_weight.z);

    return sample_www;
}

vec3 sample_dataset(vec4 coordinates) {
    const float sampler_index = min(coordinates.w, constants.dataset_dimensions.w - 1.0);
    const int sampler_index_floored = int(floor(sampler_index));
    const int sampler_index_ceiled = int(ceil(sampler_index));

    if (EXPLICIT_INTERPOLATION) {
        vec3 sample_www0 = vec3(0.0);
        sample_www0.x = sample_explicit(dataset_x[sampler_index_floored], coordinates.xyz);
        sample_www0.y = sample_explicit(dataset_y[sampler_index_floored], coordinates.xyz);
        sample_www0.z = sample_explicit(dataset_z[sampler_index_floored], coordinates.xyz);

        vec3 sample_www1 = vec3(0.0);
        sample_www1.x = sample_explicit(dataset_x[sampler_index_ceiled], coordinates.xyz);
        sample_www1.y = sample_explicit(dataset_y[sampler_index_ceiled], coordinates.xyz);
        sample_www1.z = sample_explicit(dataset_z[sampler_index_ceiled], coordinates.xyz);

        return mix(sample_www0, sample_www1, fract(coordinates.w));
    } else {
        const vec3 texture_coordinates = (coordinates.xyz + vec3(0.5)) / constants.dataset_dimensions.xyz;
        const vec3 sample_www0 = vec3(
            texture(dataset_x[sampler_index_floored], texture_coordinates).r,
            texture(dataset_y[sampler_index_floored], texture_coordinates).r,
            texture(dataset_z[sampler_index_floored], texture_coordinates).r
        );
        const vec3 sample_www1 = vec3(
            texture(dataset_x[sampler_index_ceiled], texture_coordinates).r,
            texture(dataset_y[sampler_index_ceiled], texture_coordinates).r,
            texture(dataset_z[sampler_index_ceiled], texture_coordinates).r
        );

        return mix(sample_www0, sample_www1, fract(coordinates.w));
    }
}
#elif defined(DATA_BC6H_TEXTURE)
vec3 sample_explicit(sampler2DArray dataset_sampler, vec3 coordinates) {
    ivec3 base_coordinate = ivec3(floor(coordinates));
    vec3 filter_weight = fract(coordinates);

    vec3 sample_000 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 0, 0), 0).xyz;
    vec3 sample_100 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 0, 0), 0).xyz;
    vec3 sample_w00 = mix(sample_000, sample_100, filter_weight.x);

    vec3 sample_010 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 1, 0), 0).xyz;
    vec3 sample_110 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 1, 0), 0).xyz;
    vec3 sample_w10 = mix(sample_010, sample_110, filter_weight.x);
    vec3 sample_ww0 = mix(sample_w00, sample_w10, filter_weight.y);

    vec3 sample_001 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 0, 1), 0).xyz;
    vec3 sample_101 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 0, 1), 0).xyz;
    vec3 sample_w01 = mix(sample_001, sample_101, filter_weight.x);

    vec3 sample_011 = texelFetch(dataset_sampler, base_coordinate + ivec3(0, 1, 1), 0).xyz;
    vec3 sample_111 = texelFetch(dataset_sampler, base_coordinate + ivec3(1, 1, 1), 0).xyz;
    vec3 sample_w11 = mix(sample_011, sample_111, filter_weight.x);
    vec3 sample_ww1 = mix(sample_w01, sample_w11, filter_weight.y);

    vec3 sample_www = mix(sample_ww0, sample_ww1, filter_weight.z);

    return sample_www;
}

vec3 sample_dataset(vec4 coordinates) {
    const float sampler_index = min(coordinates.w, constants.dataset_dimensions.w - 1.0);
    const int sampler_index_floored = int(floor(sampler_index));
    const int sampler_index_ceiled = int(ceil(sampler_index));

    if (EXPLICIT_INTERPOLATION) {
        vec3 sample_www0 = sample_explicit(dataset[sampler_index_floored], coordinates.xyz);
        vec3 sample_www1 = sample_explicit(dataset[sampler_index_ceiled], coordinates.xyz);

        return mix(sample_www0, sample_www1, fract(coordinates.w));
    }

    else {
        const float z_floored = floor(coordinates.z);
        const float z_ceiled = ceil(coordinates.z);

        const vec2 texture_coordinates_xy = (coordinates.xy + vec2(0.5)) / constants.dataset_dimensions.xy;

        vec3 sample_ww00 = texture(dataset[sampler_index_floored], vec3(texture_coordinates_xy, z_floored)).xyz;
        vec3 sample_ww10 = texture(dataset[sampler_index_floored], vec3(texture_coordinates_xy, z_ceiled)).xyz;
        vec3 sample_www0 = mix(sample_ww00, sample_ww10, fract(coordinates.z));

        vec3 sample_ww01 = texture(dataset[sampler_index_ceiled], vec3(texture_coordinates_xy, z_floored)).xyz;
        vec3 sample_ww11 = texture(dataset[sampler_index_ceiled], vec3(texture_coordinates_xy, z_ceiled)).xyz;
        vec3 sample_www1 = mix(sample_ww01, sample_ww11, fract(coordinates.z));

        return mix(sample_www0, sample_www1, fract(coordinates.w));
    }
}
#elif defined(DATA_ANALYTIC)
vec3 sample_dataset(vec4 coordinates) {
    return analytic_vector_field(coordinates);
}
#else
#error "define something"
#endif

// Runge Kutta 4th Order Method
vec3 rungekutta4(vec4 coordinates) {
    vec4 k1 = coordinates;
    vec3 v1 = sample_dataset(k1);

    vec4 k2 = coordinates + vec4(v1 * 0.5f * constants.dt, 0.5f * constants.dt);
    vec3 v2 = sample_dataset(k2);

    vec4 k3 = coordinates + vec4(v2 * 0.5f * constants.dt, 0.5f * constants.dt);
    vec3 v3 = sample_dataset(k3);

    vec4 k4 = coordinates + vec4(v3 * constants.dt, constants.dt);
    vec3 v4 = sample_dataset(k4);

    return (v1 + 2 * v2 + 2 * v3 + v4) / 6.0f;
}

// Newton Midpoint Method / Modified Euler Method
vec3 newton_midpoint(vec4 coordinates) {
    vec4 k1 = coordinates;
    vec3 v1 = sample_dataset(k1);

    vec4 k2 = coordinates + vec4(v1 * 0.5f * constants.dt, 0.5f * constants.dt);
    vec3 v2 = sample_dataset(k2);

    return v2;
}

// Newton Method
vec3 newton(vec4 coordinates) {
    return sample_dataset(coordinates);
}

void main() {
    if (any(greaterThanEqual(gl_GlobalInvocationID, constants.seed_dimensions))) {
        return;
    }

    const uint seed_id =
        gl_GlobalInvocationID.x +
        gl_GlobalInvocationID.y * constants.seed_dimensions.x +
        gl_GlobalInvocationID.z * constants.seed_dimensions.x * constants.seed_dimensions.y;
    const uint line_buffer_offset = seed_id * (constants.total_step_count + 1) + constants.first_step;
    const uint vertex_count = indirect_draw[seed_id].vertex_count;

    vec3 position = vertices[seed_id * (constants.total_step_count + 1) + vertex_count - 1].xyz;

    float t = constants.first_step * constants.dt;
    for (uint s = 0; s < constants.step_count; ++s) {
        const vec4 sample_location = vec4(position, t);

        if (any(lessThan(sample_location, vec4(0))) || any(greaterThan(sample_location, constants.dataset_dimensions - vec4(1.0)))) {
            break;
        }

        const vec3 velocity = rungekutta4(sample_location);
        const float velocity_magnitude = length(velocity);

        atomicMax(max_velocity_magnitude, floatBitsToUint(velocity_magnitude));

        const vec3 next_position = position + constants.dt * velocity;
        vertices[line_buffer_offset + s + 1] = vec4(next_position, velocity_magnitude);
        position = next_position;
        t += constants.dt;

        atomicAdd(indirect_draw[seed_id].vertex_count, 1);
    }
}