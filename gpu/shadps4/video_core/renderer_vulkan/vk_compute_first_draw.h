// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: draws that KosmicKrisp (Vulkan on Metal) prepares in a compute pass before drawing:
// it unrolls or rewrites their indices, or runs tessellation. Metal has no compute inside a
// render encoder, so each one ends the render pass, and its attachments are stored and loaded
// again (mesa src/kosmickrisp/vulkan/kk_cmd_draw.c: requires_unroll and the tessellation path).

#pragma once

#include <array>
#include <atomic>
#include "video_core/amdgpu/regs_vertex.h"

namespace Vulkan {

enum class ComputeFirstDraw : u32 {
    None,
    ListRestart,  ///< indexed list with primitive restart on: unrolled
    Strip16,      ///< indexed strip, 16-bit indices, restart off: indices promoted to 32 bits
    Tessellation, ///< patches, and rect and quad lists (emulated with tessellation)
    Fan,          ///< triangle fan or polygon: Metal has no fans
    Count,
};

constexpr ComputeFirstDraw ClassifyComputeFirstDraw(AmdGpu::PrimitiveType type, bool indexed,
                                                    bool restart, bool index16) {
    switch (type) {
    case AmdGpu::PrimitiveType::PointList:
    case AmdGpu::PrimitiveType::LineList:
    case AmdGpu::PrimitiveType::TriangleList:
        return indexed && restart ? ComputeFirstDraw::ListRestart : ComputeFirstDraw::None;
    case AmdGpu::PrimitiveType::LineStrip:
    case AmdGpu::PrimitiveType::TriangleStrip:
        return indexed && !restart && index16 ? ComputeFirstDraw::Strip16 : ComputeFirstDraw::None;
    case AmdGpu::PrimitiveType::PatchPrimitive:
    case AmdGpu::PrimitiveType::RectList:
    case AmdGpu::PrimitiveType::QuadList:
        return ComputeFirstDraw::Tessellation;
    case AmdGpu::PrimitiveType::TriangleFan:
    case AmdGpu::PrimitiveType::Polygon:
        return ComputeFirstDraw::Fan;
    default:
        return ComputeFirstDraw::None;
    }
}

/// Draws per kind since the last frame statistics (BB_FRAME_STATS=1).
inline std::array<std::atomic<u64>, static_cast<size_t>(ComputeFirstDraw::Count)>
    compute_first_draws{};

} // namespace Vulkan
