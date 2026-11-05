#pragma once
#include <atomic>
// Deterministic ownership model, NOT a decoder or an FFmpeg integration test.
namespace frame_model {
extern std::atomic<int> live_buffers,frame_allocations;
extern std::atomic<bool> fail_next_ref;
}
