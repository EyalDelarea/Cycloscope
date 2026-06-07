#pragma once

// Pure trigger-search logic. No JUCE dependency -> unit-testable headlessly.
enum class TriggerMode { Free = 0, Rising = 1, Falling = 2 };

// Returns the sub-sample start index of a windowSize-long draw window into
// buf[0..numSamples). Free: newest window. Rising/Falling: the MOST RECENT edge
// crossing of `threshold` that still leaves a full window, with HYSTERESIS (the
// signal must first move past threshold-/+hysteresis before a crossing can fire,
// which kills noise false-triggers) and SUB-SAMPLE interpolation of the crossing
// point (which stops the trace walking frame-to-frame). Falls back to the newest
// window when nothing valid is found.
inline float findTriggerIndex (const float* buf, int numSamples, int windowSize,
                               TriggerMode mode, float threshold,
                               float hysteresis = 0.05f, bool* triggered = nullptr) noexcept
{
    if (triggered != nullptr) *triggered = false;
    const float fallback = (float) (numSamples - windowSize > 0 ? numSamples - windowSize : 0);
    if (buf == nullptr || mode == TriggerMode::Free || numSamples <= windowSize)
        return fallback;

    const int maxStart = numSamples - windowSize; // last start leaving a full window
    const float armLo = threshold - hysteresis;   // rising: arm once we drop below this
    const float armHi = threshold + hysteresis;    // falling: arm once we rise above this

    bool armed = false;
    float lastCrossing = -1.0f;

    for (int i = 1; i <= maxStart; ++i)
    {
        const float prev = buf[i - 1];
        const float curr = buf[i];

        if (mode == TriggerMode::Rising)
        {
            if (prev < armLo) armed = true;
            if (armed && prev < threshold && curr >= threshold)
            {
                const float denom = curr - prev;
                const float frac = denom != 0.0f ? (threshold - prev) / denom : 0.0f;
                lastCrossing = (float) (i - 1) + frac;
                armed = false;
            }
        }
        else // Falling
        {
            if (prev > armHi) armed = true;
            if (armed && prev >= threshold && curr < threshold)
            {
                const float denom = curr - prev;
                const float frac = denom != 0.0f ? (threshold - prev) / denom : 0.0f;
                lastCrossing = (float) (i - 1) + frac;
                armed = false;
            }
        }
    }

    if (lastCrossing >= 0.0f)
    {
        if (triggered != nullptr) *triggered = true;
        return lastCrossing;
    }
    return fallback;
}
