layout(push_constant) uniform Constants {
    vec4 dataset_resolution;
    vec4 dataset_dimensions;
    uvec3 seed_dimensions;
    float dt;
    uint total_step_count;
    uint first_step;
    uint step_count;
} constants;

layout(std140, set = 0, binding = 0) buffer line_buffer {
   vec4 vertices[];
};

layout(std140, set = 0, binding = 1) buffer progress_buffer {
   uint progress;
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

layout (constant_id = 3) const uint TIME_STEPS = 400;

layout (constant_id = 4) const bool EXPLICIT_INTERPOLATION = false;

#if defined(DATA_RAW_TEXTURES)
layout(set = 0, binding = 3) uniform sampler3D dataset_x[TIME_STEPS];
layout(set = 0, binding = 4) uniform sampler3D dataset_y[TIME_STEPS];
layout(set = 0, binding = 5) uniform sampler3D dataset_z[TIME_STEPS];
#elif defined(DATA_BC6H_TEXTURE)
layout(set = 0, binding = 3) uniform sampler3D dataset[TIME_STEPS];
#else
#error "define something"
#endif

#if defined(DATA_RAW_TEXTURES)
vec3 sample_dataset(vec4 coordinates) {
    if (EXPLICIT_INTERPOLATION) {
        const vec4 global_coordinates = coordinates * constants.dataset_resolution;
        const ivec4 integer_coordinates[2] = {
            ivec4(floor(global_coordinates)),
            ivec4(ceil(global_coordinates)),
        };
        const vec4 mix_values = global_coordinates - integer_coordinates[0];

        /* const vec4 asd = */ 
        /* const uvec4 floored_coordinates = 
        /* const uvec4 ceiled_coordinates = uvec4(floor(global_coordinates)); */
    } else {
        const float sampler_index = coordinates.w * constants.dataset_resolution.w;
        const int sampler_index_floored = int(floor(sampler_index));
        const int sampler_index_ceiled = int(ceil(sampler_index));
        const vec3 texture_coordinates = coordinates.xyz / constants.dataset_dimensions.xyz;

        const vec3 vec_floored = vec3(
            texture(dataset_x[sampler_index_floored], texture_coordinates).r,
            texture(dataset_y[sampler_index_floored], texture_coordinates).r,
            texture(dataset_z[sampler_index_floored], texture_coordinates).r
        );
        const vec3 vec_ceiled = vec3(
            texture(dataset_x[sampler_index_ceiled], texture_coordinates).r,
            texture(dataset_y[sampler_index_ceiled], texture_coordinates).r,
            texture(dataset_z[sampler_index_ceiled], texture_coordinates).r
        );

        return mix(
            vec_floored,
            vec_ceiled,
            sampler_index - sampler_index_floored
        );
    }
}
#elif defined(DATA_BC6H_TEXTURE)
vec3 sample_dataset(vec4 coordinates) {
    const float sampler_index = coordinates.w * constants.dataset_resolution.w;
    const int sampler_index_floored = int(floor(sampler_index));
    const int sampler_index_ceiled = int(ceil(sampler_index));
    const vec3 texture_coordinates = coordinates.xyz / constants.dataset_dimensions.xyz;

    return mix(
        texture(dataset[sampler_index_floored], texture_coordinates).xyz,
        texture(dataset[sampler_index_floored], texture_coordinates).xyz,
        sampler_index - sampler_index_floored
    );
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

    return (v1 + 2*v2 + 2*v3 + v4) / 6.0f;
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
vec3 newton(vec4 coordinates)
{
	return sample_dataset(coordinates);
}
