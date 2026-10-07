// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: BB_GPU_PROFILE=1 — GPU time per render pass, dispatch and upscaler run. A timestamp is
// written where each of them starts (and at the frame end); the time to the next timestamp is
// charged to its label, barriers and copies recorded in between included. Results are read four
// frames later and printed every 5 s: GPU ms per frame by label.
// On macOS only render passes are timed, by a timestamp after each one begins and one before it
// ends: KosmicKrisp reads timestamps written outside a render pass back as 0 and adds a compute
// dispatch for each, while inside a pass it writes them at a stage of the render encoder.

#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

class GpuProfiler {
public:
    /// The profiler when BB_GPU_PROFILE=1, else null.
    static GpuProfiler* Get() noexcept {
        return instance_ptr;
    }
    static void Init(const Instance& instance, Scheduler& scheduler);

    /// Starts a segment labelled `key`; `describe` names it the first time the key is seen.
    template <typename Describe>
    void Mark(u64 key, Describe&& describe) {
        if (pass_mode) {
            return;
        }
        if (!described.contains(key)) {
            described.emplace(key, describe());
        }
        WriteTimestamp(key);
    }

    /// Labels the render pass about to begin (Mark when not timing passes only).
    template <typename Describe>
    void LabelPass(u64 key, Describe&& describe) {
        if (!pass_mode) {
            Mark(key, std::forward<Describe>(describe));
            return;
        }
        if (!described.contains(key)) {
            described.emplace(key, describe());
        }
        pass_label = key;
    }

    /// Pass timing (macOS): `from` began a render pass of `state`, or is about to end it.
    void PassBegin(const Scheduler* from, u32 width, u32 height, bool has_attachments);
    void PassEnd(const Scheduler* from);

    /// The label of the open segment, for Resume after a nested segment (transfers).
    [[nodiscard]] u64 Current() const noexcept {
        return current;
    }
    /// Continues the segment of `key` (a label seen before) where a nested one ends.
    void Resume(u64 key) {
        if (!pass_mode) {
            WriteTimestamp(key);
        }
    }

    /// Whether `other` is the scheduler it records into (the presenter has its own).
    [[nodiscard]] bool Records(const Scheduler* other) const noexcept {
        return other == &scheduler;
    }

    /// At the display pass: closes the frame's last segment, reads an old frame's results.
    void BeginFrame();

private:
    GpuProfiler(const Instance& instance, Scheduler& scheduler);
    void WriteTimestamp(u64 key);
    void WritePassTimestamp(u64 key, vk::PipelineStageFlagBits2 stage);
    void Collect(u32 slice);
    void CollectPasses(u32 slice, std::span<const u64> stamps);
    void Print();

    /// Key of a pass end timestamp (pass mode).
    static constexpr u64 PassEndKey = ~0ull;

    static constexpr u32 NumSlices = 4;
    /// Timestamps per frame. bbport: Metal (KosmicKrisp) caps a timestamp pool at 4096, so the
    /// pool is halved down to MinSliceQueries per frame until the driver accepts it.
    static constexpr u32 MaxSliceQueries = 4096;
    static constexpr u32 MinSliceQueries = 256;
    static inline GpuProfiler* instance_ptr = nullptr;

    vk::Device device;
    Scheduler& scheduler;
    vk::UniqueQueryPool pool;
    u32 slice_queries = MaxSliceQueries;
    double period_ns = 1.0;
    u32 slice = 0;
    std::array<std::vector<u64>, NumSlices> keys; ///< label of each timestamp but the last
    std::array<u32, NumSlices> used{};
    std::array<bool, NumSlices> pending{};
    std::unordered_map<u64, std::string> described;
    struct Total {
        double ms = 0;
        u64 segments = 0;
    };
    std::unordered_map<u64, Total> totals;
    u64 frames = 0;
    u64 current = 0;
    bool pass_mode = false;
    bool pass_open = false; ///< a pass start was written and its end not yet
    u64 pass_label = 0;     ///< LabelPass for the pass about to begin
    double span_ms = 0;     ///< first pass start to last pass end, summed over frames
    u64 invalid = 0;        ///< pass timings dropped (zero or decreasing timestamps)
    std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
};

} // namespace Vulkan
