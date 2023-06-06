#version 450 core
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 outFragColor ;

layout(push_constant) uniform Constants {
    layout(offset = 16 * 4) vec4 line_color;
    layout(offset = 20 * 4) uint colormap;
    layout(offset = 21 * 4) bool colormap_invert;
    layout(offset = 22 * 4) float velocity_min;
    layout(offset = 23 * 4) float velocity_max;
} constants;

layout(location = 0) in float inVelocityMagnitude;

#define COLORMAP_COLOR              0
#define COLORMAP_MATLAB_BONE        1
#define COLORMAP_MATLAB_HOT         2
#define COLORMAP_MATLAB_JET         3
#define COLORMAP_MATLAB_SUMMER      4
#define COLORMAP_IDL_RAINBOW        5
#define COLORMAP_IDL_MAC_STYLE      6
#define COLORMAP_IDL_CB_YIGN        7
#define COLORMAP_IDL_CB_YIGNBU      8
#define COLORMAP_IDL_CB_RDBU        9
#define COLORMAP_IDL_CB_RDYIGN      10
#define COLORMAP_IDL_CB_SPECTRAL    11
#define COLORMAP_TRANSFORM_RAINBOW  12

vec4 colormap_matlab_bone(float x);
vec4 colormap_matlab_hot(float x);
vec4 colormap_matlab_jet(float x);
vec4 colormap_matlab_summer(float x);
vec4 colormap_idl_rainbow(float x);
vec4 colormap_idl_mac_style(float x);
vec4 colormap_idl_cb_yign(float x);
vec4 colormap_idl_cb_yignbu(float x);
vec4 colormap_idl_cb_rdbu(float x);
vec4 colormap_idl_cb_rdyign(float x);
vec4 colormap_idl_cb_spectral(float x);
vec4 colormap_transform_rainbow(float x);

void main() {
    float colormap_value = clamp((inVelocityMagnitude - constants.velocity_min) / (constants.velocity_max - constants.velocity_min), 0.0, 1.0);

    if (constants.colormap_invert) {
        colormap_value = 1.0 - colormap_value;
    }

    switch (constants.colormap) {
    case COLORMAP_COLOR:
        outFragColor = constants.line_color;
        break;
    case COLORMAP_MATLAB_BONE:
        outFragColor = colormap_matlab_bone(colormap_value);
        break;
    case COLORMAP_MATLAB_HOT:
        outFragColor = colormap_matlab_hot(colormap_value);
        break;
    case COLORMAP_MATLAB_JET:
        outFragColor = colormap_matlab_jet(colormap_value);
        break;
    case COLORMAP_MATLAB_SUMMER:
        outFragColor = colormap_matlab_summer(colormap_value);
        break;
    case COLORMAP_IDL_RAINBOW:
        outFragColor = colormap_idl_rainbow(colormap_value);
        break;
    case COLORMAP_IDL_MAC_STYLE:
        outFragColor = colormap_idl_mac_style(colormap_value);
        break;
    case COLORMAP_IDL_CB_YIGN:
        outFragColor = colormap_idl_cb_yign(colormap_value);
        break;
    case COLORMAP_IDL_CB_YIGNBU:
        outFragColor = colormap_idl_cb_yignbu(colormap_value);
        break;
    case COLORMAP_IDL_CB_RDBU:
        outFragColor = colormap_idl_cb_rdbu(colormap_value);
        break;
    case COLORMAP_IDL_CB_RDYIGN:
        outFragColor = colormap_idl_cb_rdyign(colormap_value);
        break;
    case COLORMAP_IDL_CB_SPECTRAL:
        outFragColor = colormap_idl_cb_spectral(colormap_value);
        break;
    case COLORMAP_TRANSFORM_RAINBOW:
        outFragColor = colormap_transform_rainbow(colormap_value);
        break;
    default:
        break;
    }
}

#define colormap colormap_matlab_bone
#include "MATLAB_bone.frag"
#undef colormap

#define colormap colormap_matlab_hot
#include "MATLAB_hot.frag"
#undef colormap

#define colormap colormap_matlab_jet
#define colormap_red colormap_red_matlab_jet
#define colormap_green colormap_green_matlab_jet
#define colormap_blue colormap_blue_matlab_jet
#include "MATLAB_jet.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_matlab_summer
#include "MATLAB_summer.frag"
#undef colormap

#define colormap colormap_idl_rainbow
#define colormap_red colormap_red_idl_rainbow
#define colormap_green colormap_green_idl_rainbow
#define colormap_blue colormap_blue_idl_rainbow
#include "IDL_Rainbow.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_idl_mac_style
#include "IDL_Mac_Style.frag"
#undef colormap

#define colormap colormap_idl_cb_yign
#define colormap_red colormap_red_idl_cb_yign
#define colormap_green colormap_green_idl_cb_yign
#define colormap_blue colormap_blue_idl_cb_yign
#include "IDL_CB-YIGn.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_idl_cb_yignbu
#define colormap_red colormap_red_idl_cb_yignbu
#define colormap_green colormap_green_idl_cb_yignbu
#define colormap_blue colormap_blue_idl_cb_yignbu
#include "IDL_CB-YIGnBu.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_idl_cb_rdbu
#define colormap_red colormap_red_idl_cb_rdbu
#define colormap_green colormap_green_idl_cb_rdbu
#define colormap_blue colormap_blue_idl_cb_rdbu
#include "IDL_CB-RdBu.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_idl_cb_rdyign
#define colormap_red colormap_red_idl_cb_rdyign
#define colormap_green colormap_green_idl_cb_rdyign
#define colormap_blue colormap_blue_idl_cb_rdyign
#include "IDL_CB-RdYiGn.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_idl_cb_spectral
#define colormap_red colormap_red_idl_cb_spectral
#define colormap_green colormap_green_idl_cb_spectral
#define colormap_blue colormap_blue_idl_cb_spectral
#include "IDL_CB-Spectral.frag"
#undef colormap
#undef colormap_red
#undef colormap_green
#undef colormap_blue

#define colormap colormap_transform_rainbow
#include "transform_rainbow.frag"
#undef colormap
