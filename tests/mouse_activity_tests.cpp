#include <cassert>
#include <cstdint>

#include "jiggler_config.h"
#include "mouse_activity.h"

int main() {
    JigglerConfig config;
    assert(parseJigglerConfigCommand("CFG,10,60,10", config));
    assert(config.idleTimeoutSeconds == 10);
    assert(config.repeatIntervalSeconds == 60);
    assert(config.squareSidePixels == 10);
    assert(config.runDurationMinutes == 0);
    assert(parseJigglerConfigCommand("CFG,3600,1,100,1440", config));
    assert(config.idleTimeoutSeconds == 3600);
    assert(config.repeatIntervalSeconds == 1);
    assert(config.squareSidePixels == 100);
    assert(config.runDurationMinutes == 1440);
    assert(parseJigglerConfigCommand("CFG,10,60,10", config));
    assert(config.runDurationMinutes == 0);
    assert(!parseJigglerConfigCommand("CFG,0,60,10", config));
    assert(!parseJigglerConfigCommand("CFG,10,3601,10", config));
    assert(!parseJigglerConfigCommand("CFG,10,60,101", config));
    assert(!parseJigglerConfigCommand("CFG,10,60,10,1441", config));
    assert(!parseJigglerConfigCommand("CFG,10,60,10,1,2", config));
    assert(!parseJigglerConfigCommand("CFG,10,60,10,0,", config));
    assert(!parseJigglerConfigCommand("CFG,10,60,10x", config));
    assert(!parseJigglerConfigCommand("CFG,4294967296,60,10", config));
    assert(!parseJigglerConfigCommand("CFG,,60,10", config));
    assert(!parseJigglerConfigCommand("CFG,10,60", config));
    assert(!parseJigglerConfigCommand(nullptr, config));
    assert(config.idleTimeoutSeconds == 10);
    assert(config.repeatIntervalSeconds == 60);
    assert(config.squareSidePixels == 10);
    assert(config.runDurationMinutes == 0);

    assert(!hasJigglerRunDurationExpired(0, 0, UINT32_MAX));
    assert(!hasJigglerRunDurationExpired(1, 100, 60'099));
    assert(hasJigglerRunDurationExpired(1, 100, 60'100));
    assert(!hasJigglerRunDurationExpired(1, 0xFFFFFF00u, 59'743u));
    assert(hasJigglerRunDurationExpired(1, 0xFFFFFF00u, 59'744u));

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
