#include "ScopeReduce.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <cstdio>

static bool approx (float a, float b) { return std::fabs (a - b) < 1e-5f; }

int main()
{
    // 1. Downsampling: each pixel column reduces to the min/max of the samples in its span.
    //    src[i] = i, span 4 samples/pixel, 2 pixels -> col0=[0,4)->{0..3}, col1=[4,8)->{4..7}
    {
        std::vector<float> src (8);
        for (int i = 0; i < 8; ++i) src[(size_t) i] = (float) i;
        float mn[2], mx[2];
        decimateMinMax (src.data(), 8, 0.0, 4.0, 2, mn, mx);
        assert (approx (mn[0], 0.0f) && approx (mx[0], 3.0f));
        assert (approx (mn[1], 4.0f) && approx (mx[1], 7.0f));
    }

    // 2. Upsampling (spp < 1): columns with no integer sample interpolate (min == max == line).
    //    src = {0,10,20,30}, start 0, spp 0.5, width 4.
    //    col0 [0,0.5) contains i=0 -> 0;  col1 [0.5,1.0) sparse -> interp(0.75)=7.5
    //    col2 [1.0,1.5) contains i=1 -> 10; col3 [1.5,2.0) sparse -> interp(1.75)=17.5
    {
        std::vector<float> src = { 0.0f, 10.0f, 20.0f, 30.0f };
        float mn[4], mx[4];
        decimateMinMax (src.data(), 4, 0.0, 0.5, 4, mn, mx);
        const float want[4] = { 0.0f, 7.5f, 10.0f, 17.5f };
        for (int x = 0; x < 4; ++x)
        {
            assert (approx (mn[x], mx[x]));    // sparse columns are a line, not a band
            assert (approx (mn[x], want[x]));
        }
    }

    // 3. Sub-sample start: span [0.5, 2.5) over src[i]=i picks integers 1 and 2.
    {
        std::vector<float> src (8);
        for (int i = 0; i < 8; ++i) src[(size_t) i] = (float) i;
        float mn[1], mx[1];
        decimateMinMax (src.data(), 8, 0.5, 2.0, 1, mn, mx);
        assert (approx (mn[0], 1.0f) && approx (mx[0], 2.0f));
    }

    // 4. Fully out-of-range span clamps to the nearest valid edge sample (no zero injection).
    //    start -2, spp 2 -> col0 [-2,0): indices -2,-1 invalid -> interp center -1 clamps to src[0].
    {
        std::vector<float> src = { 5.0f, 6.0f, 7.0f, 8.0f };
        float mn[1], mx[1];
        decimateMinMax (src.data(), 4, -2.0, 2.0, 1, mn, mx);
        assert (approx (mn[0], 5.0f) && approx (mx[0], 5.0f));
    }

    // 5. Partially out-of-range span uses only the in-range samples (no zero injection).
    //    start 2, spp 4 -> col0 [2,6): indices 2,3 valid; 4,5 dropped -> min 2, max 3.
    {
        std::vector<float> src = { 0.0f, 1.0f, 2.0f, 3.0f };
        float mn[1], mx[1];
        decimateMinMax (src.data(), 4, 2.0, 4.0, 1, mn, mx);
        assert (approx (mn[0], 2.0f) && approx (mx[0], 3.0f));
    }

    std::puts ("scopereduce OK");
    return 0;
}
