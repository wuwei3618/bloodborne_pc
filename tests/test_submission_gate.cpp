// SPDX-License-Identifier: GPL-2.0-or-later
// The guest's submission throttle: sceGnmSubmitDone closes the gate while the GPU has work, the
// GPU thread's idle interrupt opens it, the next submit waits for it. An idle signal from a pass
// that ended with a submission still queued must not open the gate for that submission.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include "bbport_submission_gate.h"

int main() {
    BbPort::SubmissionGate gate;
    bool idle = false;
    const auto gpu_idle = [&idle] { return idle; };

    gate.Close(gpu_idle); // sceGnmSubmitDone while the submission is queued
    assert(!gate.IsOpen());
    gate.Open(gpu_idle); // the signal of the pass before, which drained while it was submitted
    assert(!gate.IsOpen());

    std::atomic<bool> resumed{false};
    std::thread submit([&] {
        gate.Wait(); // the next sceGnmSubmitAndFlipCommandBuffers
        resumed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!resumed);
    idle = true;
    gate.Open(gpu_idle); // the pass that processed the submission
    submit.join();
    assert(resumed && gate.IsOpen());

    gate.Close(gpu_idle); // sceGnmSubmitDone with the GPU idle already
    assert(gate.IsOpen());
    std::puts("Submission gate: PASS (an idle signal with work queued keeps the gate closed)");
}
