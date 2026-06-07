#pragma once
#include <cmath>
#include <string>

// Map a frequency in Hz to a note name (e.g. 440 -> "A4", 55 -> "A1"). Returns
// "--" for non-positive/NaN input. Pure C++ — no JUCE dependency.
inline std::string noteNameForHz (float hz) noexcept
{
    if (! (hz > 0.0f)) return "--";
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int midi = (int) std::lround (12.0 * std::log2 ((double) hz / 440.0)) + 69;
    const int pc  = ((midi % 12) + 12) % 12;
    const int oct = midi / 12 - 1;
    return std::string (names[pc]) + std::to_string (oct);
}
