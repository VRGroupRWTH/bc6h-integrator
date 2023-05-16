#version 450 core

layout(location = 0) out vec4 outFragColor ;

layout(push_constant) uniform Constants {
    layout(offset = 16 * 4) vec4 line_color;
} constants;

void main() {
    outFragColor = constants.line_color;
}
