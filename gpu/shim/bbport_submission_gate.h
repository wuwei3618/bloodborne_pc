// bbport: the guest's submission throttle. sceGnmSubmitDone closes the gate while the GPU still
// has work, the GPU idle interrupt opens it, and the next submit waits until it is open. The GPU
// thread signals idle after each pass, also when a submission arrived while the pass drained the
// draw pipe; such a signal must not open a gate closed for that submission, or the guest runs
// ahead and reuses command buffer memory the GPU thread has not read yet. Both decisions take
// the GPU's state under the gate's lock.
#pragma once
#include <condition_variable>
#include <mutex>

namespace BbPort {
class SubmissionGate {
public:
    template <typename GpuIdle>
    void Close(GpuIdle&& gpu_idle) {
        std::scoped_lock lock{mutex};
        if (!gpu_idle()) {
            closed = true;
        }
    }
    template <typename GpuIdle>
    void Open(GpuIdle&& gpu_idle) {
        std::scoped_lock lock{mutex};
        if (closed && gpu_idle()) {
            closed = false;
            opened.notify_all();
        }
    }
    void Wait() {
        std::unique_lock lock{mutex};
        opened.wait(lock, [this] { return !closed; });
    }
    bool IsOpen() {
        std::scoped_lock lock{mutex};
        return !closed;
    }

private:
    std::mutex mutex;
    std::condition_variable opened;
    bool closed = false;
};
} // namespace BbPort
