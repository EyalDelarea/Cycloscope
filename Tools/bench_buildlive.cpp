// Micro-benchmark: OLD vs NEW scope data path, at wide-open Time zoom. Pure C++ (no
// JUCE) so it builds and runs headlessly. Mirrors the per-frame work each version does
// in ScopeComponent::buildLive, to quantify the speedup and show where time goes.
#include "RingBuffer.h"
#include "Trigger.h"
#include "SignalUtils.h"   // combineChannel
#include "StereoUtils.h"   // rmsDb
#include "ScopeReduce.h"
#include <vector>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <functional>
#include <algorithm>

using Clock = std::chrono::steady_clock;
static double ms (Clock::duration d) { return std::chrono::duration<double, std::milli> (d).count(); }

// best (min) per-frame ms over REPEATS, after one untimed warmup batch -- min is the
// cleanest microbenchmark signal (least contaminated by scheduler/thermal noise).
static double bestOf (int frames, int repeats, const std::function<void()>& frame)
{
    for (int f = 0; f < frames; ++f) frame(); // warmup (not timed)
    double best = 1.0e30;
    for (int r = 0; r < repeats; ++r)
    {
        auto t0 = Clock::now();
        for (int f = 0; f < frames; ++f) frame();
        best = std::min (best, ms (Clock::now() - t0) / frames);
    }
    return best;
}

int main()
{
    const int width = 1100;
    const float spp = 64.0f;            // Time fully "wide open"
    const int window = (int) (width * spp);
    const int captureSize = window * 2; // 140,800 samples
    const int FRAMES = 300;             // ~5 s of 60 fps repaints

    RingBuffer ringL (192000), ringR (192000);
    for (int i = 0; i < 192000; ++i)
    {
        const float v = 0.8f * (float) std::sin (2.0 * 3.14159 * 220.0 * i / 48000.0);
        ringL.write (v); ringR.write (v);
    }

    std::printf ("wide-open: width=%d spp=%.0f window=%d captureSize=%d, %d frames\n\n",
                 width, spp, window, captureSize, FRAMES);

    const int REPEATS = 7; // best-of, to reject scheduler/thermal noise

    // ---- OLD path: assign(zero) x3, copyLatest x2, combine, full min/max, full rms, trigger, interp ----
    {
        std::vector<float> capture, capL, capR, ys ((size_t) width);
        const double per = bestOf (FRAMES, REPEATS, [&]
        {
            capture.assign ((size_t) captureSize, 0.0f);
            capL.assign ((size_t) captureSize, 0.0f);
            capR.assign ((size_t) captureSize, 0.0f);
            ringL.copyLatest (capL.data(), captureSize);
            ringR.copyLatest (capR.data(), captureSize);
            for (int i = 0; i < captureSize; ++i) capture[(size_t) i] = combineChannel (capL[(size_t) i], capR[(size_t) i], 0);
            bool trig = false;
            const float start = findTriggerIndex (capture.data(), captureSize, window, TriggerMode::Rising, 0.0f, 0.05f, &trig);
            float mn = 1e9f, mx = -1e9f;
            for (int i = 0; i < captureSize; ++i) { const float v = capture[(size_t) i]; mn = mn < v ? mn : v; mx = mx > v ? mx : v; }
            volatile float vpp = mx - mn; (void) vpp;
            volatile float rms = rmsDb (capture.data(), captureSize); (void) rms;
            for (int x = 0; x < width; ++x)
            {
                const float pos = start + (float) x * spp;
                const int i0 = (int) pos;
                ys[(size_t) x] = (i0 >= 0 && i0 < captureSize) ? capture[(size_t) i0] : 0.0f;
            }
        });
        std::printf ("OLD                               per-frame %.3f ms\n", per);
    }

    // ---- NEW path: reserve once, resize, copyLatest x2, combine, window-only measure, trigger, decimate ----
    {
        std::vector<float> capture, capL, capR, yHi ((size_t) width), yLo ((size_t) width);
        capture.reserve (2 * 192000); capL.reserve (2 * 192000); capR.reserve (2 * 192000);
        const double per = bestOf (FRAMES, REPEATS, [&]
        {
            capture.resize ((size_t) captureSize);
            capL.resize ((size_t) captureSize);
            capR.resize ((size_t) captureSize);
            ringL.copyLatest (capL.data(), captureSize);
            ringR.copyLatest (capR.data(), captureSize);
            for (int i = 0; i < captureSize; ++i) capture[(size_t) i] = combineChannel (capL[(size_t) i], capR[(size_t) i], 0);
            bool trig = false;
            const float start = findTriggerIndex (capture.data(), captureSize, window, TriggerMode::Rising, 0.0f, 0.05f, &trig);
            const int s0 = (int) start < 0 ? 0 : (int) start;
            const int s1 = (s0 + window) > captureSize ? captureSize : (s0 + window);
            float mn = 1e9f, mx = -1e9f;
            for (int i = s0; i < s1; ++i) { const float v = capture[(size_t) i]; mn = mn < v ? mn : v; mx = mx > v ? mx : v; }
            volatile float vpp = mx - mn; (void) vpp;
            volatile float rms = (s1 > s0) ? rmsDb (capture.data() + s0, s1 - s0) : -100.0f; (void) rms;
            decimateMinMax (capture.data(), captureSize, start, spp, width, yLo.data(), yHi.data());
        });
        std::printf ("NEW                               per-frame %.3f ms\n", per);
    }

    // ---- NEW + Free-trigger fast path: capture only the visible window (no 2x search headroom) ----
    {
        const int cap1 = window;
        std::vector<float> capture, capL, capR, yHi ((size_t) width), yLo ((size_t) width);
        capture.reserve (2 * 192000); capL.reserve (2 * 192000); capR.reserve (2 * 192000);
        const double per = bestOf (FRAMES, REPEATS, [&]
        {
            capture.resize ((size_t) cap1);
            capL.resize ((size_t) cap1);
            capR.resize ((size_t) cap1);
            ringL.copyLatest (capL.data(), cap1);
            ringR.copyLatest (capR.data(), cap1);
            for (int i = 0; i < cap1; ++i) capture[(size_t) i] = combineChannel (capL[(size_t) i], capR[(size_t) i], 0);
            float mn = 1e9f, mx = -1e9f;
            for (int i = 0; i < cap1; ++i) { const float v = capture[(size_t) i]; mn = mn < v ? mn : v; mx = mx > v ? mx : v; }
            volatile float vpp = mx - mn; (void) vpp;
            volatile float rms = rmsDb (capture.data(), cap1); (void) rms;
            decimateMinMax (capture.data(), cap1, 0.0, spp, width, yLo.data(), yHi.data());
        });
        std::printf ("NEW+ (Free, window-only capture)  per-frame %.3f ms\n", per);
    }

    return 0;
}
