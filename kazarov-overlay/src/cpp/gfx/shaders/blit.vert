#version 460

// A colour blit as a draw (NativeBlit in draw.cpp): a strip of four vertices covering the viewport, which is
// the destination rectangle. The fragment shader works out where each pixel samples.

void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
