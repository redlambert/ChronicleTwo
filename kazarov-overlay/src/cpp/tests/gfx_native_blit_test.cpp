#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "gfx/context.hpp"
#include "tests/gfx_fixture.hpp"

using namespace dc::test;

namespace {

// Native blits on for the test's renderer, and the default again once it is gone.
class NativeBlitSwitch {
public:
    NativeBlitSwitch() { gfx::detail::SetNativeBlitForTest(true); }

    ~NativeBlitSwitch() { gfx::detail::SetNativeBlitForTest(true); }

    NativeBlitSwitch(const NativeBlitSwitch &) = delete;
    NativeBlitSwitch &operator=(const NativeBlitSwitch &) = delete;
};

// A source with no two neighbouring texels alike, so a sample from the wrong place shows.
std::vector<uint32_t> Pattern(uint32_t width, uint32_t height) {
    std::vector<uint32_t> texels(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            texels[static_cast<size_t>(y) * width + x] =
                Rgba(static_cast<uint8_t>(x * 37 + y * 11), static_cast<uint8_t>(x * 5 + y * 71),
                     static_cast<uint8_t>(x * y * 13 + 7), static_cast<uint8_t>(255 - x * 9 - y * 3));
        }
    }
    return texels;
}

struct BlitCase {
    const char *name;
    gfx::Rect   src;
    gfx::Rect   dst;
    gfx::Filter filter;
};

// The largest difference in any channel of any pixel.
int MaxDifference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    if (a.size() != b.size()) {
        return 256;
    }
    int worst = 0;
    for (size_t index = 0; index < a.size(); ++index) {
        worst = std::max(worst, std::abs(static_cast<int>(a[index]) - static_cast<int>(b[index])));
    }
    return worst;
}

// Pixels that differ, leaving out those whose centre maps exactly onto a source texel edge along either axis.
int NearestMismatches(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, const BlitCase &blit,
                      uint32_t width) {
    if (a.size() != b.size()) {
        return -1;
    }
    auto on_edge = [](int32_t pixel, int32_t dst0, int32_t dst_size, int32_t src0, int32_t src_size) {
        // (pixel + 0.5 - dst0) * src_size / dst_size + src0, in exact halves.
        const int64_t numerator = static_cast<int64_t>(2 * (pixel - dst0) + 1) * src_size;
        const int64_t denominator = 2 * static_cast<int64_t>(dst_size);
        return numerator % denominator == 0 && pixel >= std::min(dst0, dst0 + dst_size) &&
               pixel < std::max(dst0, dst0 + dst_size);
    };
    int mismatches = 0;
    for (size_t index = 0; index < a.size(); index += 4) {
        if (std::equal(a.begin() + index, a.begin() + index + 4, b.begin() + index)) {
            continue;
        }
        const int32_t x = static_cast<int32_t>((index / 4) % width);
        const int32_t y = static_cast<int32_t>((index / 4) / width);
        if (!on_edge(x, blit.dst.x, blit.dst.w, blit.src.x, blit.src.w) &&
            !on_edge(y, blit.dst.y, blit.dst.h, blit.src.y, blit.src.h)) {
            ++mismatches;
        }
    }
    return mismatches;
}

} // namespace

// Every scaled blit the game makes lands the same with native blits as through vkCmdBlitImage: up and down, nearest
// and bilinear, mirrored either way, into part of a target whose other pixels stay as they were.
TEST(GfxNativeBlit, MatchesDriverBlits) {
    NativeBlitSwitch native;
    GfxFixture       fixture(64, 48);
    ASSERT_NE(gfx::detail::g.blit_pipeline, VK_NULL_HANDLE);

    constexpr uint32_t kSourceWidth = 16;
    constexpr uint32_t kSourceHeight = 12;
    const auto         source = gfx::CreateTexture({kSourceWidth, kSourceHeight, gfx::TextureFormat::Rgba8, 1, true});
    ASSERT_NE(source, gfx::kNullTexture);
    const auto texels = Pattern(kSourceWidth, kSourceHeight);
    ASSERT_TRUE(gfx::UpdateTexture(source, 0, 0, 0, kSourceWidth, kSourceHeight, texels.data()));

    const std::array cases{
        BlitCase{"up nearest",          {0, 0, 16, 12},  {0, 0, 40, 30},  gfx::Filter::Nearest},
        BlitCase{"up linear",           {0, 0, 16, 12},  {0, 0, 40, 30},  gfx::Filter::Linear },
        BlitCase{"down nearest",        {0, 0, 16, 12},  {3, 5, 7, 5},    gfx::Filter::Nearest},
        BlitCase{"down linear",         {0, 0, 16, 12},  {3, 5, 7, 5},    gfx::Filter::Linear },
        BlitCase{"part up linear",      {2, 3, 9, 6},    {4, 2, 27, 19},  gfx::Filter::Linear },
        BlitCase{"mirrored x nearest",  {16, 0, -16, 12}, {1, 1, 33, 25}, gfx::Filter::Nearest},
        BlitCase{"mirrored y linear",   {0, 12, 16, -12}, {1, 1, 33, 25}, gfx::Filter::Linear },
        BlitCase{"mirrored dst linear", {0, 0, 16, 12},  {35, 2, -30, 20}, gfx::Filter::Linear},
    };
    for (const BlitCase &blit : cases) {
        auto run = [&](bool use_native) {
            gfx::detail::SetNativeBlitForTest(use_native);
            const auto target = gfx::CreateRenderTarget(40, 30, true);
            EXPECT_NE(target, gfx::kNullTexture);
            // A background the blit must keep outside its rectangle.
            const auto background = Pattern(40, 30);
            const auto fill = gfx::CreateTexture({40, 30, gfx::TextureFormat::Rgba8, 1, true});
            EXPECT_TRUE(gfx::UpdateTexture(fill, 0, 0, 0, 40, 30, background.data()));
            EXPECT_TRUE(gfx::CopyTexture(fill, {0, 0, 40, 30}, target, 0, 0));
            EXPECT_TRUE(gfx::BlitTexture(source, blit.src, target, blit.dst, blit.filter));
            std::vector<uint8_t> pixels;
            uint32_t             width = 0;
            uint32_t             height = 0;
            EXPECT_TRUE(gfx::ReadbackTexture(target, pixels, width, height));
            gfx::DestroyTexture(fill);
            gfx::DestroyTexture(target);
            return pixels;
        };
        const auto driver = run(false);
        const auto drawn = run(true);
        ASSERT_FALSE(driver.empty()) << blit.name;
        if (blit.filter == gfx::Filter::Linear) {
            // Bilinear weights may round one step apart between the copy path and the sampler.
            EXPECT_LE(MaxDifference(driver, drawn), 2) << blit.name;
        } else {
            // A pixel centre that maps exactly onto a texel edge may take either texel (drivers differ: lavapipe
            // takes the lower one, the sampler the upper); every other pixel must match exactly.
            EXPECT_EQ(NearestMismatches(driver, drawn, blit, 40), 0) << blit.name;
        }
    }
    gfx::DestroyTexture(source);
}

// A native blit in the middle of a frame leaves the frame's draws after it as they would be without it.
TEST(GfxNativeBlit, DrawsAfterABlitInAFrameAreUnchanged) {
    NativeBlitSwitch native;
    GfxFixture       fixture(64, 48);
    ASSERT_NE(gfx::detail::g.blit_pipeline, VK_NULL_HANDLE);
    const auto source = gfx::CreateTexture({8, 8, gfx::TextureFormat::Rgba8, 1, true});
    const auto texels = Pattern(8, 8);
    ASSERT_TRUE(gfx::UpdateTexture(source, 0, 0, 0, 8, 8, texels.data()));
    const auto target = gfx::CreateRenderTarget(32, 32, true);
    ASSERT_NE(target, gfx::kNullTexture);

    auto run = [&](bool use_native) {
        gfx::detail::SetNativeBlitForTest(use_native);
        fixture.Frame({10, 20, 30, 255}, [&] {
            EXPECT_TRUE(gfx::BlitTexture(gfx::kMainTarget, {0, 0, 640, 480}, target, {0, 0, 32, 32},
                                         gfx::Filter::Linear));
            EXPECT_TRUE(gfx::BlitTexture(source, {0, 0, 8, 8}, gfx::kMainTarget, {100, 80, 300, 200},
                                         gfx::Filter::Linear));
            gfx::DrawState state;
            state.blend = true;
            const auto quad = Quad(320, 240, 280, 220, {200, 100, 50, 128}, 0.0f, 0.0f, 8.0f, 8.0f);
            gfx::Draw2D(gfx::Primitive::Quads, quad, {.texture = source, .filter = gfx::Filter::Linear}, state);
        });
        return fixture.pixels;
    };
    const auto driver = run(false);
    const auto drawn = run(true);
    ASSERT_FALSE(driver.empty());
    EXPECT_LE(MaxDifference(driver, drawn), 2);
    gfx::DestroyTexture(target);
    gfx::DestroyTexture(source);
}

// A copy within one image reuses its scratch image from one copy to the next instead of making one each time.
TEST(GfxNativeBlit, SelfCopiesReuseTheirScratchImage) {
    GfxFixture fixture(64, 48);
    const auto texture = gfx::CreateTexture({16, 16, gfx::TextureFormat::Rgba8, 1, true});
    const auto texels = Pattern(16, 16);
    ASSERT_TRUE(gfx::UpdateTexture(texture, 0, 0, 0, 16, 16, texels.data()));
    for (int pass = 0; pass < 4; ++pass) {
        ASSERT_TRUE(gfx::CopyTexture(texture, {0, 0, 8, 8}, texture, 4, 4));
    }
    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
    ASSERT_TRUE(gfx::ReadbackTexture(texture, pixels, width, height));
    // The last copy moved the then-current top-left 8x8 to (4, 4): pixel (4, 4) holds what (0, 0) held, the
    // untouched original.
    const uint32_t original = texels[0];
    const uint8_t *p = &pixels[(4 * width + 4) * 4];
    EXPECT_EQ(Rgba(p[0], p[1], p[2], p[3]), original);
    gfx::DestroyTexture(texture);
}
