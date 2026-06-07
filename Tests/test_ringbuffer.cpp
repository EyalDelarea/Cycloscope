#include "RingBuffer.h"
#include <cassert>
#include <vector>
#include <cstdio>

int main()
{
    // capacity 8, write 1..10, latest 4 should be 7,8,9,10
    RingBuffer rb (8);
    for (int i = 1; i <= 10; ++i) rb.write ((float) i);

    std::vector<float> out (4, 0.0f);
    rb.copyLatest (out.data(), 4);
    assert (out[0] == 7.0f && out[1] == 8.0f && out[2] == 9.0f && out[3] == 10.0f);

    // request more than written/capacity -> zero-pad at the front
    RingBuffer rb2 (8);
    rb2.write (5.0f); rb2.write (6.0f);
    std::vector<float> out2 (4, -1.0f);
    rb2.copyLatest (out2.data(), 4);
    assert (out2[0] == 0.0f && out2[1] == 0.0f && out2[2] == 5.0f && out2[3] == 6.0f);

    // over-read: requesting more history than capacity must zero-pad the unavailable
    // (too-old) samples, NOT alias wrapped data
    RingBuffer rb3 (8);
    for (int i = 1; i <= 100; ++i) rb3.write ((float) i);
    std::vector<float> out3 (20, -1.0f);
    rb3.copyLatest (out3.data(), 20);
    assert (out3[19] == 100.0f);   // newest
    assert (out3[12] == 93.0f);    // age 8 (== capacity) still valid
    assert (out3[11] == 0.0f);     // age 9 > capacity -> unavailable -> 0 (not aliased)
    assert (out3[0]  == 0.0f);     // age 20 -> 0

    std::puts ("ringbuffer OK");
    return 0;
}
