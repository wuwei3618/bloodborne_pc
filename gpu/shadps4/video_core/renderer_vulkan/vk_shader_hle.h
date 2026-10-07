// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include "common/types.h"

namespace AmdGpu {
struct ComputeProgram;
union Regs;
} // namespace AmdGpu

namespace Shader {
struct Info;
}

namespace Vulkan {

class Rasterizer;

/// Attempts to execute a shader using HLE if possible.
bool ExecuteShaderHLE(const Shader::Info& info, const AmdGpu::Regs& regs,
                      const AmdGpu::ComputeProgram& cs_program, Rasterizer& rasterizer);

#ifdef __APPLE__
/// bbport (macOS, BB_FRAME_STATS=1): batches of the copy shader since the last frame statistics,
/// and which of them could be done in guest memory (BB_COPY_SHADER_CPU).
enum CopyBatchStat : u32 {
    CopyBatchTotal,
    CopyBatchInPass, ///< recorded while a render pass was open
    CopyBatchSrcGpu, ///< the GPU holds newer data for the source range
    CopyBatchDstGpu, ///< ... for the destination range
    CopyBatchImage,  ///< an image overlaps the source or the destination
    CopyBatchCpu,    ///< copied in guest memory
    CopyBatchStatCount,
};
inline std::array<std::atomic<u64>, CopyBatchStatCount> copy_shader_batches{};
#endif

} // namespace Vulkan
