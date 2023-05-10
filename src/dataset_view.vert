#version 450 core

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inTexCoords;

layout(location = 0) out vec2 outTexCoords;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    outTexCoords = inTexCoords;
    gl_Position = vec4(inPos.xyz, 1.0);
}
