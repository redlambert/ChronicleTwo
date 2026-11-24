#version 460
#extension GL_GOOGLE_include_directive : require
// One-pass outline composite: an outline image's four offset copies and its body in one draw, dual-source blended.
#define KZ_OUTLINE_COMPOSITE
#include "draw_body.glsl"
