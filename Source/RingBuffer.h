#pragma once
#include <atomic>
#include <vector>
#include <cstddef>

// Single-producer (audio thread) / single-consumer (GUI thread) circular
// buffer of floats. copyLatest snapshots the most recent N samples. A torn
// read during wrap is acceptable for visualization and never reads out of
// bounds. Pure C++ — no JUCE dependency so it is unit-testable headlessly.
class RingBuffer
{
public:
    explicit RingBuffer (std::size_t capacity)
        : buffer (capacity, 0.0f), cap (capacity) {}

    void write (float sample) noexcept
    {
        const std::size_t w = writePos.load (std::memory_order_relaxed);
        buffer[w % cap] = sample;
        writePos.store (w + 1, std::memory_order_release);
    }

    // Copy the most recent numSamples into dest (oldest first). If fewer than
    // numSamples have ever been written, the front of dest is zero-padded.
    void copyLatest (float* dest, int numSamples) const noexcept
    {
        const std::size_t w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < numSamples; ++i)
        {
            // dest[numSamples-1] is the newest sample; `age` is how far back it is.
            const long long age   = (long long) (numSamples - i);
            const long long index = (long long) w - age;
            // Unavailable if never written (index < 0) OR older than capacity (would
            // alias newer wrapped data). Zero-pad rather than show aliased garbage.
            if (index < 0 || age > (long long) cap)
                dest[i] = 0.0f;
            else
                dest[i] = buffer[(std::size_t) index % cap];
        }
    }

    std::size_t capacity() const noexcept { return cap; }

private:
    std::vector<float> buffer;
    std::size_t cap;
    std::atomic<std::size_t> writePos { 0 };
};
