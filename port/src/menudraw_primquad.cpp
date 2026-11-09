#include "common.h"

#include <algorithm>
#include <vector>

#include "mainloop.hpp"
#include "menu_mouse.hpp"
#include "menudraw.hpp"
#include "mg_drawprim.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "render/primitive_submission.hpp"
#include "render/renderer.hpp"
#include "text/menu_label.hpp"
#include "log/log.hpp"

namespace {

// Brightness the mirrored wallpaper fades to at the far edge of the widescreen sides (1 = no fade).
constexpr float kBackdropSideFloor = 0.15f;

/**
 * Brightness of the backdrop at screen x: full across the 4:3 area, easing linearly down to
 * kBackdropSideFloor at the edge of the visible screen on each side.
 */
float BackdropSideShade(float x, const mgPortExtent &extent) {
    float t = 0.0f;
    if (x < 0.0f && extent.left < 0) {
        t = x / static_cast<float>(extent.left);
    } else if (x > mgScreenWidth && extent.right > mgScreenWidth) {
        t = (x - mgScreenWidth) / static_cast<float>(extent.right - mgScreenWidth);
    }
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - kBackdropSideFloor) * t;
}

bool IsBackdrop(mgCDrawPrim *prim, mgRect<int> uv, char *name, int width, int height, int top = 0) {
    if (!prim || !prim->prim.TME || uv.left != 0 || uv.top != top ||
        uv.right != width || uv.bottom != height) {
        return false;
    }
    const auto *texture = mgTexManager.GetTexture(name, -1);
    return texture && prim->texture.tex0_bits == texture->tex0_bits;
}

void DrawBackdrop(mgCDrawPrim &prim, mgRect<float> rect, mgRect<int> uv, float artwork_width) {
    const auto extent = mgPortVisibleExtent();
    const bool wide = extent.left < 0 || extent.right > mgScreenWidth;
    uint8_t    color[4];
    mgPortPrimColor(prim, color);
    std::vector<gfx::Vertex2D> vertices;
    const int                  corners[6][2] = {
        {0, 0},
        {1, 0},
        {1, 1},
        {0, 0},
        {1, 1},
        {0, 1}
    };
    const auto append_piece = [&](float left, float right, float u0, float u1) {
        // Linear filtering must stay inside the artwork at a reflection seam;
        // otherwise its transparent padding leaves a dark hairline.
        if (wide) {
            u0 = std::clamp(u0, 0.5f, artwork_width - 0.5f);
            u1 = std::clamp(u1, 0.5f, artwork_width - 0.5f);
        }
        for (const auto &corner : corners) {
            const float x = corner[0] ? right : left;
            uint8_t     shaded[4];
            std::copy_n(color, 4, shaded);
            if (wide) {
                const float shade = BackdropSideShade(x, extent);
                for (int channel = 0; channel < 3; ++channel) {
                    shaded[channel] = static_cast<uint8_t>(static_cast<float>(color[channel]) * shade + 0.5f);
                }
            }
            vertices.push_back(mgPortPrimVertex(prim,
                                                x, corner[1] ? rect.top + rect.bottom : rect.top, 0,
                                                corner[0] ? u1 : u0, corner[1] ? uv.top + uv.bottom : uv.top, shaded));
        }
    };
    // Split each strip at the 4:3 edges so the side fade starts exactly there and the 4:3 area stays untouched.
    const auto append = [&](float left, float right, float u0, float u1) {
        if (!wide || right <= left) {
            append_piece(left, right, u0, u1);
            return;
        }
        const float cuts[2] = {0.0f, static_cast<float>(mgScreenWidth)};
        float       piece_left = left;
        float       piece_u = u0;
        for (const float cut : cuts) {
            if (cut <= piece_left || cut >= right) {
                continue;
            }
            const float cut_u = u0 + (u1 - u0) * (cut - left) / (right - left);
            append_piece(piece_left, cut, piece_u, cut_u);
            piece_left = cut;
            piece_u = cut_u;
        }
        append_piece(piece_left, right, piece_u, u1);
    };
    const float left = rect.left;
    const float right = left + rect.right;
    // bg16's last four columns and chr_bg's extra source column are transparent
    // padding. At wide aspects join the reflection to the artwork, not padding.
    const float u_end = wide ? artwork_width : uv.right;
    const float span = rect.right * u_end / uv.right;
    append(left, left + span, 0, u_end);

    if (wide && rect.right > 0 && rect.bottom > 0 && right > 0 && left < mgScreenWidth) {
        // Keep the reflection phase attached to the moving artwork. During a
        // horizontal slide extend only into the flanks, preserving the field
        // revealed inside the retail frame. Include the authored edge padding
        // at the settled joins (Make starts at 1, Character ends at 511).
        const float left_join = left <= 2 ? std::max(0.0f, left) : 0.0f;
        bool        reflected = true;
        for (float edge = left; extent.left < edge; edge -= span, reflected = !reflected) {
            const float end = std::max(extent.left, edge - span);
            const float start = std::min(left_join, edge);
            if (start <= end) {
                continue;
            }
            const float start_u = reflected ? 0 : u_end;
            const float rate = (reflected ? 1 : -1) * uv.right / rect.right;
            append(end, start, start_u + (edge - end) * rate, start_u + (edge - start) * rate);
        }
        const float right_join = right >= mgScreenWidth - 2 ? std::min<float>(mgScreenWidth, left + span) : mgScreenWidth;
        reflected = true;
        for (float edge = left + span; extent.right > edge; edge += span, reflected = !reflected) {
            const float end = std::min(extent.right, edge + span);
            const float start = std::max(right_join, edge);
            if (start >= end) {
                continue;
            }
            const float start_u = reflected ? u_end : 0;
            const float rate = (reflected ? -1 : 1) * uv.right / rect.right;
            append(start, end, start_u + (start - edge) * rate, start_u + (end - edge) * rate);
        }
    }
    // The side fade needs the vertex colours blended across each strip, so shading is on for this submit only.
    const auto shading = prim.prim.IIP;
    if (wide) {
        prim.prim.IIP = 1;
        static bool logged = false;
        if (!logged) {
            logged = true;
            CT_LOG_INFO(Menu, "menu backdrop: widescreen sides fade to {:.0f}% at the screen edge",
                        kBackdropSideFloor * 100.0f);
        }
    }
    mgPortPrimSubmit(prim, gfx::Primitive::Triangles, vertices);
    prim.prim.IIP = shading;
}

} // namespace

void PrimQuad(mgCTexture *texture, float x, float y, mgRect<int> uv,
              int alpha, int red, int green, int blue) {
    // The title and language menus use their own atlas rather than MenuCursorDraw.
    const bool         cursor = GetNowLoopNo() == LOOP_TITLE && uv.right == 40 && uv.bottom == 24 &&
                                ((uv.left == 0 && uv.top == 40 && texture == mgTexManager.GetTexture(const_cast<char *>("moji"), -1)) ||
                                 (uv.left == 216 && uv.top == 360));
    gfx::Interp2DScope tag(cursor ? gfx::Interp2D{.key = MenuMouseNextHandKey(), .generation = static_cast<uint64_t>(mgPortFrameCount())} : gfx::CurrentInterp2D());
    if (cursor) {
        MenuMouseNoteHand(mgPortBinding(texture),
                          {.x = static_cast<float>(uv.left), .y = static_cast<float>(uv.top), .w = static_cast<float>(uv.right), .h = static_cast<float>(uv.bottom)},
                          0, alpha);
    }
    mgCDrawPrim prim;
    SetSpriteEnv(&prim, MENU_SPRITE_ENV_TEXTURE);
    prim.Begin(MG_PRIM_SPRITE);
    prim.Texture(texture);
    prim.Color(red, green, blue, alpha);
    PrimQuad(&prim, x, y, uv);
    prim.End();
}

// Host specializations of the rectangle helper declared by menudraw.hpp.
template <>
void PrimQuad<float>(mgCDrawPrim *prim, mgRect<float> put_rect, mgRect<int> tex_rect) {
    if (prim != nullptr && dc2::text::DrawMenuLabel(*prim, put_rect, tex_rect)) {
        return;
    }
    static char character_name[] = "chr_bg";
    static char invent_name[] = "inv_bg";
    static char options_name[] = "op_bg";
    const auto  extent = mgPortVisibleExtent();
    if ((extent.left < 0 || extent.right > mgScreenWidth) &&
        (IsBackdrop(prim, tex_rect, character_name, 513, 768) ||
         IsBackdrop(prim, tex_rect, invent_name, 512, 416, 48) ||
         IsBackdrop(prim, tex_rect, options_name, 512, 448, 46))) {
        DrawBackdrop(*prim, put_rect, tex_rect, 512);
        return;
    }
    PrimQuad_f_(prim, mgRect_f_{.left = put_rect.left, .top = put_rect.top, .right = put_rect.right, .bottom = put_rect.bottom},
                mgRect_i_{.left = tex_rect.left, .top = tex_rect.top, .right = tex_rect.right, .bottom = tex_rect.bottom});
}

template <>
void PrimQuad<int>(mgCDrawPrim *prim, mgRect<int> put_rect, mgRect<int> tex_rect) {
    if (prim != nullptr && dc2::text::DrawMenuFigure(*prim, put_rect, tex_rect)) {
        return;
    }
    static char frame_name[] = "bg16";
    if (IsBackdrop(prim, tex_rect, frame_name, 704, 416)) {
        // Preserve the authored lens position. Full-frame sprite cover scaling
        // moves its alpha mask away from the model camera, even at 4:3.
        DrawBackdrop(*prim, mgRect<float>(put_rect.left, put_rect.top, put_rect.right, put_rect.bottom), tex_rect, 700);
        return;
    }
    PrimQuad_i_(prim, mgRect_i_{.left = put_rect.left, .top = put_rect.top, .right = put_rect.right, .bottom = put_rect.bottom},
                mgRect_i_{.left = tex_rect.left, .top = tex_rect.top, .right = tex_rect.right, .bottom = tex_rect.bottom});
}
