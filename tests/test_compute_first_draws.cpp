// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the draws KosmicKrisp (Vulkan on Metal) prepares in a compute pass, which ends the
// render pass (vk_compute_first_draw.h). Neither launches the game nor needs a Vulkan device.
#include <cassert>
#include <cstdio>
#include "video_core/renderer_vulkan/vk_compute_first_draw.h"

int main() {
    using AmdGpu::PrimitiveType;
    using Vulkan::ClassifyComputeFirstDraw;
    using Kind = Vulkan::ComputeFirstDraw;

    // Indexed lists with primitive restart on are unrolled; other lists draw directly.
    for (const auto type : {PrimitiveType::PointList, PrimitiveType::LineList,
                            PrimitiveType::TriangleList}) {
        for (const bool index16 : {false, true}) {
            assert(ClassifyComputeFirstDraw(type, true, true, index16) == Kind::ListRestart);
            assert(ClassifyComputeFirstDraw(type, true, false, index16) == Kind::None);
            assert(ClassifyComputeFirstDraw(type, false, true, index16) == Kind::None);
        }
    }

    // Metal always restarts strips at 0xFFFF: 16-bit indices without restart are promoted.
    for (const auto type : {PrimitiveType::LineStrip, PrimitiveType::TriangleStrip}) {
        assert(ClassifyComputeFirstDraw(type, true, false, true) == Kind::Strip16);
        assert(ClassifyComputeFirstDraw(type, true, true, true) == Kind::None);
        assert(ClassifyComputeFirstDraw(type, true, false, false) == Kind::None);
        assert(ClassifyComputeFirstDraw(type, false, false, true) == Kind::None);
    }

    // Rect and quad lists are drawn with tessellation, as patches are, indexed or not.
    for (const auto type : {PrimitiveType::RectList, PrimitiveType::QuadList,
                            PrimitiveType::PatchPrimitive}) {
        for (const bool indexed : {false, true}) {
            for (const bool restart : {false, true}) {
                assert(ClassifyComputeFirstDraw(type, indexed, restart, true) ==
                       Kind::Tessellation);
            }
        }
    }

    // Metal has no triangle fans.
    for (const auto type : {PrimitiveType::TriangleFan, PrimitiveType::Polygon}) {
        assert(ClassifyComputeFirstDraw(type, false, false, false) == Kind::Fan);
        assert(ClassifyComputeFirstDraw(type, true, true, true) == Kind::Fan);
    }

    assert(ClassifyComputeFirstDraw(PrimitiveType::None, true, true, true) == Kind::None);
    std::puts("Compute-first draws: PASS");
}
