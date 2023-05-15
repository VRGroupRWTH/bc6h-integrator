layout(set = 0, binding = 0) uniform sampler3D dataset[400];

layout(std140, binding = 1) buffer line_buffer {
   vec4 vertices[];
};

layout(std140, binding = 2) buffer progress_buffer {
   uint progress;
};

struct DrawIndirectCommand {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

layout(std140, binding = 3) buffer indirect_buffer {
   DrawIndirectCommand indirect_draw[];
};

layout(push_constant) uniform Constants {
    vec4 dataset_resolution;
    vec4 dataset_dimensions;
    uvec3 seed_dimensions;
    uint step_count;
} constants;

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
    
    /* return vec3(1.0, 0.5, 0.1); */
}
