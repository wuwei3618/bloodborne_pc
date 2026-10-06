// bbport: host threads that may call guest code (AvPlayer allocator callbacks).
// Each thread gets a guest TCB (GS base, TLS) from the C runtime before running.
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include "common/types.h"

extern "C" void runtime_thread_attach_host(const char* name);

namespace Libraries::Kernel {
// Several threads may join the same Thread at once (AvPlayer: the game's sceAvPlayerStop and the
// demuxer at the end of the stream), and a thread may end its own Thread object (the decoders and
// the demuxer do). One caller joins, the others wait for that join; the thread itself only
// detaches while nobody joins it. Unsynchronized, a second join could wait on a thread handle the
// system had given to a new thread, and never return.
class Thread {
public:
    Thread() = default;
    ~Thread() { Stop(); }
    void Run(std::function<void(std::stop_token)>&& func) {
        Stop(); // as std::jthread's assignment did: the previous thread is stopped and joined
        std::scoped_lock lock{mutex};
        thread = std::jthread([func = std::move(func)](std::stop_token stop) {
            runtime_thread_attach_host("bb:hle");
            func(stop);
        });
        owner = thread.get_id();
        stop_source = thread.get_stop_source();
    }
    void Join() {
        std::unique_lock lock{mutex};
        if (owner == std::this_thread::get_id()) {
            if (thread.joinable()) {
                thread.detach();
            }
            return;
        }
        joined.wait(lock, [this] { return !joining; });
        if (!thread.joinable()) {
            return;
        }
        joining = true;
        std::jthread ending = std::move(thread);
        lock.unlock();
        ending.join();
        lock.lock();
        joining = false;
        joined.notify_all();
    }
    bool Joinable() const {
        std::scoped_lock lock{mutex};
        return thread.joinable() || joining;
    }
    void Stop() {
        {
            std::scoped_lock lock{mutex};
            stop_source.request_stop();
        }
        Join();
    }

private:
    mutable std::mutex mutex;
    std::condition_variable joined;
    bool joining = false;
    std::thread::id owner;
    std::stop_source stop_source{std::nostopstate};
    std::jthread thread;
};
} // namespace Libraries::Kernel
