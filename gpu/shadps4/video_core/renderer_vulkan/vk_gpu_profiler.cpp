// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdlib>

#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

void GpuProfiler::Init(const Instance& instance, Scheduler& scheduler) {
    const char* env = std::getenv("BB_GPU_PROFILE");
    if (!env || env[0] != '1' || instance_ptr) {
        return;
    }
    auto* profiler = new GpuProfiler(instance, scheduler);
    if (!profiler->pool) {
        std::printf("GPU profile: off (no timestamp pool of %u queries)\n",
                    NumSlices * MinSliceQueries);
        delete profiler;
        return;
    }
    instance_ptr = profiler;
}

GpuProfiler::GpuProfiler(const Instance& instance, Scheduler& scheduler_)
    : device{instance.GetDevice()}, scheduler{scheduler_} {
    for (; slice_queries >= MinSliceQueries; slice_queries /= 2) {
        const vk::QueryPoolCreateInfo info = {
            .queryType = vk::QueryType::eTimestamp,
            .queryCount = NumSlices * slice_queries,
        };
        auto [result, created] = device.createQueryPoolUnique(info);
        if (result == vk::Result::eSuccess) {
            pool = std::move(created);
            break;
        }
    }
    if (!pool) {
        return;
    }
    device.resetQueryPool(*pool, 0, NumSlices * slice_queries);
    period_ns = instance.GetPhysicalDevice().getProperties().limits.timestampPeriod;
    for (auto& k : keys) {
        k.reserve(slice_queries);
    }
#ifdef __APPLE__
    pass_mode = true;
#endif
    std::printf("GPU profile: on (timestamp period %.2f ns, %u timestamps per frame%s)\n",
                period_ns, slice_queries, pass_mode ? ", render passes only" : "");
}

void GpuProfiler::WritePassTimestamp(u64 key, vk::PipelineStageFlagBits2 stage) {
    const u32 query = slice * slice_queries + used[slice]++;
    keys[slice].push_back(key);
    scheduler.Record([pool = *pool, query, stage](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp2(stage, pool, query);
    });
}

void GpuProfiler::PassBegin(const Scheduler* from, u32 width, u32 height, bool has_attachments) {
    if (!pass_mode || from != &scheduler) {
        return;
    }
    u64 key = std::exchange(pass_label, 0);
    // KosmicKrisp starts the Metal encoder of a pass without attachments at its first draw: a
    // timestamp before that would be written outside it. Two queries: the start and the end.
    if (!has_attachments || used[slice] + 2 > slice_queries) {
        return;
    }
    if (!key) {
        key = 0xFA55'0000'0000'0000ull | u64(width) << 16 | height;
        if (!described.contains(key)) {
            described.emplace(key, "pass " + std::to_string(width) + "x" + std::to_string(height) +
                                       " (not labelled)");
        }
    }
    // TOP_OF_PIPE and BOTTOM_OF_PIPE: the first and the last stage of the Metal render encoder.
    WritePassTimestamp(key, vk::PipelineStageFlagBits2::eTopOfPipe);
    pass_open = true;
}

void GpuProfiler::PassEnd(const Scheduler* from) {
    if (!pass_mode || from != &scheduler || !pass_open) {
        return;
    }
    pass_open = false;
    WritePassTimestamp(PassEndKey, vk::PipelineStageFlagBits2::eBottomOfPipe);
}

void GpuProfiler::WriteTimestamp(u64 key) {
    if (used[slice] + 1 >= slice_queries) {
        return; // the frame's slice is full: the rest of the frame goes to the last label
    }
    // Outside render passes: radv_CmdWriteTimestamp2 crashed inside some. Marks sit where a
    // pass, dispatch or submission ends anyway.
    scheduler.EndRendering();
    const u32 query = slice * slice_queries + used[slice]++;
    keys[slice].push_back(key);
    current = key;
    scheduler.Record([pool = *pool, query](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, pool, query);
    });
}

void GpuProfiler::BeginFrame() {
    if (pass_mode) {
        // A pass still open: its time so far counts in this frame, the rest in none.
        if (pass_open) {
            pass_open = false;
            WritePassTimestamp(PassEndKey, vk::PipelineStageFlagBits2::eBottomOfPipe);
        }
        pending[slice] = used[slice] > 0;
    } else if (used[slice] > 0 && used[slice] < slice_queries) {
        // Close the frame: one more timestamp without a label.
        scheduler.EndRendering();
        const u32 query = slice * slice_queries + used[slice]++;
        scheduler.Record([pool = *pool, query](vk::CommandBuffer cmdbuf) {
            cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, pool, query);
        });
        pending[slice] = true;
    }
    slice = (slice + 1) % NumSlices;
    // The oldest slice: its frame was submitted four frames ago.
    if (pending[slice]) {
        Collect(slice);
    }
    if (used[slice]) {
        device.resetQueryPool(*pool, slice * slice_queries, used[slice]);
    }
    used[slice] = 0;
    keys[slice].clear();
    pending[slice] = false;
    Print();
}

void GpuProfiler::Collect(u32 which) {
    const u32 count = used[which];
    std::vector<u64> stamps(count);
    // Four frames on this is complete unless the GPU lags that far: then wait for it.
    const auto result = device.getQueryPoolResults(
        *pool, which * slice_queries, count, count * sizeof(u64), stamps.data(), sizeof(u64),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    if (result != vk::Result::eSuccess) {
        return;
    }
    if (pass_mode) {
        CollectPasses(which, stamps);
        ++frames;
        return;
    }
    for (u32 i = 0; i + 1 < count; ++i) {
        const double ms = double(stamps[i + 1] - stamps[i]) * period_ns / 1e6;
        auto& total = totals[keys[which][i]];
        total.ms += ms;
        ++total.segments;
    }
    ++frames;
}

void GpuProfiler::CollectPasses(u32 which, std::span<const u64> stamps) {
    // Starts and ends alternate: a pass ends before the next one begins.
    const auto& labels = keys[which];
    u64 first = ~0ull;
    u64 last = 0;
    for (size_t i = 0; i + 1 < stamps.size(); ++i) {
        if (labels[i] == PassEndKey || labels[i + 1] != PassEndKey) {
            continue;
        }
        const u64 begin = stamps[i];
        const u64 end = stamps[i + 1];
        if (begin == 0 || end < begin) {
            ++invalid;
            continue;
        }
        auto& total = totals[labels[i]];
        total.ms += double(end - begin) * period_ns / 1e6;
        ++total.segments;
        first = std::min(first, begin);
        last = std::max(last, end);
    }
    if (last > first) {
        span_ms += double(last - first) * period_ns / 1e6;
    }
}

void GpuProfiler::Print() {
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds(5) || frames == 0) {
        return;
    }
    window = now;
    std::vector<std::pair<u64, Total>> sorted(totals.begin(), totals.end());
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    double sum = 0;
    for (const auto& [key, total] : sorted) {
        sum += total.ms;
    }
    if (pass_mode) {
        // The span less the passes: compute, copies and idle time between the passes.
        std::printf("GPU profile: %.2f ms/frame in render passes, %.2f ms/frame from the first "
                    "pass start to the last pass end, over %llu frames, %zu labels, %llu dropped\n",
                    sum / frames, span_ms / frames, static_cast<unsigned long long>(frames),
                    sorted.size(), static_cast<unsigned long long>(invalid));
        span_ms = 0;
        invalid = 0;
    } else {
        std::printf("GPU profile: %.2f ms/frame over %llu frames, %zu labels\n", sum / frames,
                    static_cast<unsigned long long>(frames), sorted.size());
    }
    for (size_t i = 0; i < std::min<size_t>(sorted.size(), 30); ++i) {
        const auto& [key, total] = sorted[i];
        std::printf("  %6.3f ms/frame %5.1f/frame  %s\n", total.ms / frames,
                    double(total.segments) / frames, described[key].c_str());
    }
    totals.clear();
    frames = 0;
}

} // namespace Vulkan
