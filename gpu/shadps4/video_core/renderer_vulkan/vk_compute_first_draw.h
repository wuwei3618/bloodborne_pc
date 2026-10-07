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
    RectQuad,     ///< rect or quad list: bbport draws these with tessellation
    Patch,        ///< the game's own tessellation
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
    case AmdGpu::PrimitiveType::RectList:
    case AmdGpu::PrimitiveType::QuadList:
        return ComputeFirstDraw::RectQuad;
    case AmdGpu::PrimitiveType::PatchPrimitive:
        return ComputeFirstDraw::Patch;
    case AmdGpu::PrimitiveType::TriangleFan:
    case AmdGpu::PrimitiveType::Polygon:
        return ComputeFirstDraw::Fan;
    default:
        return ComputeFirstDraw::None;
    }
}

/// BB_STRIP_RESTART: turn restart on for indexed 16-bit strips that have it off, so KosmicKrisp
/// draws them without the index rewrite. On by default on macOS; "0" turns it off, "1" on.
constexpr bool StripRestartWanted(const char* env, bool apple) {
    return env && env[0] ? env[0] == '1' : apple;
}

/// Draws per kind since the last frame statistics (BB_FRAME_STATS=1).
inline std::array<std::atomic<u64>, static_cast<size_t>(ComputeFirstDraw::Count)>
    compute_first_draws{};

} // namespace Vulkan
