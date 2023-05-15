#version 450 core

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Constants {
    mat4 world_view_projection;
} constants;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};

void main() {
    gl_Position = constants.world_view_projection * vec4(inPos.xyz, 1.0);
    gl_PointSize = 10.0;
}
