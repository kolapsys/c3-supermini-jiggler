#pragma once

#include <cstddef>
#include <cstdint>

struct JigglerConfig {
    uint32_t idleTimeoutSeconds = 10;
    uint32_t repeatIntervalSeconds = 60;
    uint32_t squareSidePixels = 10;
};

constexpr uint32_t kMinJigglerTimeoutSeconds = 1;
constexpr uint32_t kMaxJigglerTimeoutSeconds = 3600;
constexpr uint32_t kMinJiggleSquareSidePixels = 1;
constexpr uint32_t kMaxJiggleSquareSidePixels = 100;

constexpr bool isValidJigglerConfig(const JigglerConfig& config) {
    return config.idleTimeoutSeconds >= kMinJigglerTimeoutSeconds &&
           config.idleTimeoutSeconds <= kMaxJigglerTimeoutSeconds &&
           config.repeatIntervalSeconds >= kMinJigglerTimeoutSeconds &&
           config.repeatIntervalSeconds <= kMaxJigglerTimeoutSeconds &&
           config.squareSidePixels >= kMinJiggleSquareSidePixels &&
           config.squareSidePixels <= kMaxJiggleSquareSidePixels;
}

inline bool parseJigglerConfigCommand(const char* command, JigglerConfig& config) {
    if (command == nullptr || command[0] != 'C' || command[1] != 'F' ||
        command[2] != 'G' || command[3] != ',') {
        return false;
    }

    uint32_t values[3]{};
    const char* position = command + 4;
    for (size_t index = 0; index < 3; ++index) {
        uint32_t& value = values[index];
        if (*position < '0' || *position > '9') {
            return false;
        }

        do {
            const uint32_t digit = static_cast<uint32_t>(*position - '0');
            if (value > (UINT32_MAX - digit) / 10) {
                return false;
            }
            value = value * 10 + digit;
            ++position;
        } while (*position >= '0' && *position <= '9');

        if (index < 2) {
            if (*position != ',') {
                return false;
            }
            ++position;
        }
    }

    if (*position != '\0') {
        return false;
    }

    const JigglerConfig parsed{values[0], values[1], values[2]};
    if (!isValidJigglerConfig(parsed)) {
        return false;
    }

    config = parsed;
    return true;
}
