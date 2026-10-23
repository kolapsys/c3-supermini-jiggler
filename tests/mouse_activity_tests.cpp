#include <cassert>
#include <cstdint>

#include "mouse_activity.h"

int main() {
    MouseActivity activity(10'000, 60'000, 100);
    assert(!activity.shouldJiggle(10'099));
    assert(activity.shouldJiggle(10'100));
    assert(!activity.shouldJiggle(70'099));
    assert(activity.shouldJiggle(70'100));

    activity.noteActivity(100'000);
    assert(!activity.shouldJiggle(109'999));
    assert(activity.shouldJiggle(110'000));
    assert(!activity.shouldJiggle(169'999));
    assert(activity.shouldJiggle(170'000));

    MouseActivity wraparound(100, 50, 0xFFFFFFF0u);
    assert(!wraparound.shouldJiggle(0x00000052u));
    assert(wraparound.shouldJiggle(0x00000054u));
}
