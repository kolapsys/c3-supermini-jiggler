#pragma once

#include <cstdint>

class MouseActivity {
public:
    MouseActivity(uint32_t idleTimeoutMs, uint32_t jiggleIntervalMs, uint32_t now)
        : idleTimeoutMs_(idleTimeoutMs),
          jiggleIntervalMs_(jiggleIntervalMs),
          lastActivityMs_(now),
          lastJiggleMs_(now),
          hasJiggled_(false) {}

    void noteActivity(uint32_t now) {
        lastActivityMs_ = now;
        hasJiggled_ = false;
    }

    bool shouldJiggle(uint32_t now) {
        if (static_cast<uint32_t>(now - lastActivityMs_) < idleTimeoutMs_) {
            return false;
        }
        if (hasJiggled_ && static_cast<uint32_t>(now - lastJiggleMs_) < jiggleIntervalMs_) {
            return false;
        }

        lastJiggleMs_ = now;
        hasJiggled_ = true;
        return true;
    }

private:
    uint32_t idleTimeoutMs_;
    uint32_t jiggleIntervalMs_;
    uint32_t lastActivityMs_;
    uint32_t lastJiggleMs_;
    bool hasJiggled_;
};
