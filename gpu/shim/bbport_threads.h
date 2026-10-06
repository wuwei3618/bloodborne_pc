// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: helper thread sizing. Counts follow the hardware threads this process may run on
// (the affinity mask, so `taskset` can emulate a Steam Deck), and speculative helpers run as
// SCHED_IDLE: they use cores the game leaves idle and never take time from its threads.
// macOS has neither affinity masks nor SCHED_IDLE: all hardware threads count, and helpers
// get the utility QoS class.

#pragma once

#include <algorithm>
#include <cstdint>
#include <pthread.h>
#include <thread>
#ifdef __APPLE__
#include <pthread/qos.h>
#else
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace BbThreads {

/// Hardware threads available to the process.
inline unsigned Available() {
#ifndef __APPLE__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        return std::max(1, CPU_COUNT(&set));
    }
#endif
    return std::max(1u, std::thread::hardware_concurrency());
}

/// The calling thread only runs on otherwise idle cores (falls back to the lowest nice level).
inline void MakeBackground() {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
    sched_param param{};
    if (sched_setscheduler(0, SCHED_IDLE, &param) != 0) {
        setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 19);
    }
#endif
}

/// Kernel id of the calling thread, for logs and statistics.
inline std::uint32_t CurrentId() {
#ifdef __APPLE__
    std::uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return static_cast<std::uint32_t>(id);
#else
    return static_cast<std::uint32_t>(gettid());
#endif
}

} // namespace BbThreads
