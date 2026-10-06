// SPDX-License-Identifier: GPL-2.0-or-later
// AvPlayer's threads on Libraries::Kernel::Thread: a decoder ends its own Thread at the end of the
// stream, the demuxer joins it and ends its own Thread, and the game's sceAvPlayerStop joins the
// same Threads meanwhile. Every join has to return, after the thread's work.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "core/libraries/kernel/threads.h"

extern "C" void runtime_thread_attach_host(const char*) {}

int main() {
    // A join that never returns (a stale thread handle) fails the test instead of hanging it.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::fputs("HLE thread: FAIL (a join did not return)\n", stderr);
        std::_Exit(1);
    }).detach();
    for (int round = 0; round < 2000; ++round) {
        Libraries::Kernel::Thread decoder, demuxer;
        std::atomic<bool> eof{false}, decoded{false};
        decoder.Run([&](std::stop_token stop) {
            while (!stop.stop_requested() && !eof) {
                std::this_thread::yield();
            }
            decoded = true;
            decoder.Join(); // the decoder ends its own Thread
        });
        demuxer.Run([&](std::stop_token) {
            eof = true;
            decoder.Join(); // end of stream: the demuxer waits for the decoder
            demuxer.Join(); // and ends its own Thread
        });
        decoder.Stop(); // the game's sceAvPlayerStop, at the same time
        assert(decoded);
        demuxer.Stop();
        assert(!decoder.Joinable() && !demuxer.Joinable());
    }
    std::puts("HLE thread: PASS (end of stream and Stop join the same threads)");
}
