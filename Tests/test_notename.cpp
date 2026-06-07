#include "NoteName.h"
#include <cassert>
#include <cstdio>
#include <string>

int main()
{
    assert (noteNameForHz (440.0f)  == "A4");
    assert (noteNameForHz (55.0f)   == "A1");
    assert (noteNameForHz (261.63f) == "C4");
    assert (noteNameForHz (0.0f)    == "--");   // invalid -> placeholder
    std::puts ("notename OK");
    return 0;
}
