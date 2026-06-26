// runtime/sliding_average — ring-buffer rolling mean (compile-time capacity).
//
// Port of TiXL Editor/Gui/Interaction/Timing/SlidingAverage.cs.
// TiXL uses a dynamic Queue<double> (heap-allocated, runtime-length).
// sw diverges intentionally:
//   FORK-1 Type: TiXL uses double; sw uses float (GPU-side convention, zero wasteful precision).
//   FORK-2 Capacity: TiXL takes runtime int maxMaxLength; sw templates on N (zero heap, runtime zone).
//   FORK-3 API: TiXL `UpdateAndCompute(v)` returns immediately; sw splits push()/mean() so callers
//             can query the current average without pushing (e.g. beat_timing read-only smoothing).
//
// PURE CPU — no Metal, no app/ui/platform/verify dependency.
#pragma once

#include <cstddef>

namespace sw {

// SlidingAverage<N>: N-slot ring buffer; returns the mean of all stored values.
// Capacity N is a compile-time constant — zero heap allocation.
// Equivalent to TiXL SlidingAverage with maxMaxLength == N.
template<int N>
struct SlidingAverage {
    static_assert(N > 0, "SlidingAverage capacity must be >= 1");

    float values[N] = {};
    int   head  = 0;
    int   count = 0;

    // push(v) — enqueue a value, evicting the oldest when full.
    // = TiXL SlidingAverage.UpdateAndCompute (the enqueue side).
    void push(float v) {
        values[head] = v;
        head = (head + 1) % N;
        if (count < N) ++count;
    }

    // mean() — returns the running average of all stored values.
    // Returns 0 if empty (mirrors TiXL: "if (_queue.Count > 0) averageStrength = _currentSum / _queue.Count").
    float mean() const {
        if (count == 0) return 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += values[i];
        return sum / static_cast<float>(count);
    }

    // pushAndMean(v) — convenience: push then return mean.
    // = TiXL SlidingAverage.UpdateAndCompute (both sides combined).
    float pushAndMean(float v) {
        push(v);
        return mean();
    }

    // reset() — clear the buffer.
    // = TiXL SlidingAverage.Clear().
    void reset() {
        head  = 0;
        count = 0;
    }

    bool empty() const { return count == 0; }
    int  size()  const { return count; }
};

// ringAverage(s) — free-function alias for s.mean(), used by modules
// (beat_timing, etc.) that want a verb-style call site.
template<int N>
inline float ringAverage(const SlidingAverage<N>& s) {
    return s.mean();
}

}  // namespace sw
