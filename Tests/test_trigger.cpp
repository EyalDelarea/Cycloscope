#include "Trigger.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <cstdio>

static bool near (float a, float b) { return std::fabs (a - b) < 0.05f; }

int main()
{
    std::vector<float> saw = {-3,-2,-1,0,1,2,3,2,1,0,-1,-2};
    const int window = 4;

    // Rising crossing of 0 at index 3 (sub-sample crossing lands exactly on 3.0)
    float idx = findTriggerIndex (saw.data(), (int) saw.size(), window, TriggerMode::Rising, 0.0f);
    assert (near (idx, 3.0f));

    // Sub-sample: crossing halfway between samples -> fractional index 0.5
    std::vector<float> ramp (12, 1.0f);
    ramp[(size_t) 0] = -1.0f; // arms (below -hyst) then crosses up to +1 between idx 0 and 1
    float sub = findTriggerIndex (ramp.data(), 12, window, TriggerMode::Rising, 0.0f);
    assert (near (sub, 0.5f));

    // Falling: no downward crossing in the searchable region -> fallback (latest window)
    float idxF = findTriggerIndex (saw.data(), (int) saw.size(), window, TriggerMode::Falling, 0.0f);
    assert (near (idxF, (float) ((int) saw.size() - window)));

    // Free -> fallback
    float idxFree = findTriggerIndex (saw.data(), (int) saw.size(), window, TriggerMode::Free, 0.0f);
    assert (near (idxFree, (float) ((int) saw.size() - window)));

    // Flat signal never arms (never dips below threshold-hysteresis) -> fallback, no false trigger
    std::vector<float> flat (12, 0.5f);
    float idxFlat = findTriggerIndex (flat.data(), 12, window, TriggerMode::Rising, 0.0f);
    assert (near (idxFlat, (float) (12 - window)));

    std::puts ("trigger OK");
    return 0;
}
