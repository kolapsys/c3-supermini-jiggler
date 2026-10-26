#pragma once

#include <cstddef>
#include <cstdint>

struct JigglerConfig {
    uint32_t idleTimeoutSeconds = 10;
    uint32_t repeatIntervalSeconds = 60;
    uint32_t squareSidePixels = 10;
    uint32_t runDurationMinutes = 0;
};

constexpr uint32_t kMinJigglerTimeoutSeconds = 1;
constexpr uint32_t kMaxJigglerTimeoutSeconds = 3600;
constexpr uint32_t kMinJiggleSquareSidePixels = 1;
constexpr uint32_t kMaxJiggleSquareSidePixels = 100;
constexpr uint32_t kMaxJigglerRunDurationMinutes = 1440;
constexpr uint32_t kJigglerRunDurationUnitMs = 60'000;

constexpr bool isValidJigglerConfig(const JigglerConfig& config) {
    return config.idleTimeoutSeconds >= kMinJigglerTimeoutSeconds &&
           config.idleTimeoutSeconds <= kMaxJigglerTimeoutSeconds &&
           config.repeatIntervalSeconds >= kMinJigglerTimeoutSeconds &&
           config.repeatIntervalSeconds <= kMaxJigglerTimeoutSeconds &&
           config.squareSidePixels >= kMinJiggleSquareSidePixels &&
           config.squareSidePixels <= kMaxJiggleSquareSidePixels &&
           config.runDurationMinutes <= kMaxJigglerRunDurationMinutes;
}

inline bool parseJigglerConfigCommand(const char* command, JigglerConfig& config) {
    if (command == nullptr || command[0] != 'C' || command[1] != 'F' ||
        command[2] != 'G' || command[3] != ',') {
        return false;
    }

    uint32_t values[4]{};
    const char* position = command + 4;
    size_t valueCount = 0;
    while (valueCount < 4) {
        const size_t index = valueCount;
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

        ++valueCount;
        if (*position != ',') {
            break;
        }
        ++position;
        if (valueCount == 4) {
            return false;
        }
    }

    if (*position != '\0') {
        return false;
    }

    if (valueCount != 3 && valueCount != 4) {
        return false;
    }

    const JigglerConfig parsed{
        values[0],
        values[1],
        values[2],
        valueCount == 4 ? values[3] : 0,
    };
    if (!isValidJigglerConfig(parsed)) {
        return false;
    }

    config = parsed;
    return true;
}

constexpr bool hasJigglerRunDurationExpired(
    uint32_t durationMinutes,
    uint32_t enabledAtMs,
    uint32_t nowMs
) {
    return durationMinutes != 0 &&
           static_cast<uint32_t>(nowMs - enabledAtMs) >=
               durationMinutes * kJigglerRunDurationUnitMs;
}
