layout(set = 0, binding = 0) uniform sampler3D dataset[200];

layout(std140, binding = 1) buffer line_buffer {
   vec3 vertices[];
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
    uvec4 dataset_dimensions;
    uvec3 seed_dimensions;
    uint step_count;
} constants;

vec3 sample_dataset(vec4 coordinates) {
    const int t_floored = int(floor(coordinates.w));
    const int t_ceiled = int(ceil(coordinates.w));
    const vec3 texture_coordinates = coordinates.xyz / constants.dataset_dimensions.xyz;

    return mix(
        texture(dataset[t_floored], texture_coordinates).xyz,
        texture(dataset[t_floored], texture_coordinates).xyz,
        coordinates.w - t_floored
    );
    
    /* return vec3(1.0, 0.5, 0.1); */
}
