#version 460

// A colour blit as a draw (NativeBlit in draw.cpp), sampled exactly as vkCmdBlitImage samples: the pixel centre
// maps to source texels by (centre - dst0) * (src1 - src0) / (dst1 - dst0) + src0, from the blit's own offsets,
// so a mirrored blit runs backwards by itself. Nearest takes the texel the point falls in, bilinear blends the four
// around it, clamped at the image's edge. Every channel is written, nothing blended.
//
// xform:    dst0 in pixels (xy), texels per pixel (zw)
// uv_xform: src0 in texels (xy), one over the source size (zw)
// flags:    0, or the width (high 16 bits) and height of the image a scratch copy stands for, to clamp at its edges

layout(set = 0, binding = 0) uniform texture2D u_textures[8192];
layout(set = 0, binding = 1) uniform sampler u_samplers[24];

layout(push_constant, std430) uniform Push {
    vec4 xform;
    vec4 uv_xform;
    uint texture_slot;
    uint palette_slot;
    uint sampler_slot;
    uint flags;
    uint alpha;
    uint fog_color;
    uint face;
    uint mesh_index;
    uint prog0_index;
    uint palette_index;
}
pc;

layout(location = 0) out vec4 o_color;

void main() {
    vec2 texel = (gl_FragCoord.xy - pc.xform.xy) * pc.xform.zw + pc.uv_xform.xy;
    if (pc.flags != 0u) {
        // Drawn from a scratch copy: clamp at the edges of the image the copy came from.
        vec2 size = vec2(float(pc.flags >> 16), float(pc.flags & 0xffffu));
        texel = clamp(texel, vec2(0.5), size - 0.5);
    }
    o_color    = textureLod(sampler2D(u_textures[pc.texture_slot], u_samplers[pc.sampler_slot]),
                            texel * pc.uv_xform.zw, 0.0);
}
