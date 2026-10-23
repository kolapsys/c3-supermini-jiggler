#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_log.h>

#include "mouse_activity.h"

namespace {

constexpr char kDeviceName[] = "BT Mouse";
constexpr uint32_t kIdleTimeoutMs = 10000;
constexpr uint32_t kJiggleIntervalMs = 60000;
constexpr uint32_t kJiggleStepIntervalMs = 25;
constexpr int16_t kJiggleSquareSide = 10;
constexpr uint8_t kBootButtonPin = 9;
constexpr uint8_t kLedPin = 8;
constexpr uint32_t kButtonDebounceMs = 50;
constexpr uint32_t kLedBlinkIntervalMs = 1000;
constexpr uint8_t kLedBrightness = 40;
constexpr uint16_t kMinConnectionInterval = 6;
constexpr uint16_t kMaxConnectionInterval = 12;
constexpr uint16_t kConnectionTimeout = 400;
constexpr char kTag[] = "mouse-jiggler";

constexpr uint8_t kMouseReportMap[] = {
    0x05, 0x01,       // Generic Desktop
    0x09, 0x02,       // Mouse
    0xA1, 0x01,       // Application
    0x09, 0x01,       // Pointer
    0xA1, 0x00,       // Physical
    0x85, 0x01,       // Report ID 1
    0x05, 0x09,       // Buttons
    0x19, 0x01,
    0x29, 0x03,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x03,
    0x75, 0x01,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x05,
    0x81, 0x03,
    0x05, 0x01,
    0x09, 0x30,       // X
    0x09, 0x31,       // Y
    0x16, 0x01, 0x80,
    0x26, 0xFF, 0x7F,
    0x75, 0x10,
    0x95, 0x02,
    0x81, 0x06,
    0x09, 0x38,       // Wheel
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x06,
    0xC0,
    0xC0,
};

NimBLEServer* hidServer = nullptr;
NimBLECharacteristic* mouseInput = nullptr;
MouseActivity* activity = nullptr;
bool jiggleSequenceActive = false;
uint8_t jiggleStep = 0;
uint32_t nextJiggleStepAt = 0;
bool jiggleEnabled = false;
bool rawButtonState = HIGH;
bool stableButtonState = HIGH;
uint32_t buttonStateChangedAt = 0;
uint32_t ledLastToggleAt = 0;
bool ledOn = false;

class HostReportCallbacks final : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
        Serial.printf("Computer HID report subscription: %u\n", subValue);
    }
};

class HostServerCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        activity->noteActivity(millis());
        jiggleSequenceActive = false;
        Serial.printf("Computer connected; BLE interval %.1f ms\n",
                      connInfo.getConnInterval() * 1.25f);
        server->updateConnParams(connInfo.getConnHandle(),
                                 kMinConnectionInterval,
                                 kMaxConnectionInterval,
                                 0,
                                 kConnectionTimeout);
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        activity->noteActivity(millis());
        jiggleSequenceActive = false;
        Serial.printf("Computer disconnected (reason %d)\n", reason);
    }

    void onConnParamsUpdate(NimBLEConnInfo& connInfo) override {
        Serial.printf("Computer BLE interval updated: %.1f ms\n",
                      connInfo.getConnInterval() * 1.25f);
    }
};

HostReportCallbacks hostReportCallbacks;
HostServerCallbacks hostServerCallbacks;

bool notifyHost(uint8_t buttons, int16_t x, int16_t y) {
    const uint8_t bytes[] = {
        buttons,
        static_cast<uint8_t>(x & 0xFF),
        static_cast<uint8_t>((static_cast<uint16_t>(x) >> 8) & 0xFF),
        static_cast<uint8_t>(y & 0xFF),
        static_cast<uint8_t>((static_cast<uint16_t>(y) >> 8) & 0xFF),
        0,
    };
    mouseInput->setValue(bytes, sizeof(bytes));
    return mouseInput->notify();
}

void processIdleJiggle(uint32_t now) {
    if (!jiggleEnabled || hidServer == nullptr || hidServer->getConnectedCount() == 0) {
        jiggleSequenceActive = false;
        return;
    }

    if (!jiggleSequenceActive) {
        if (!activity->shouldJiggle(now)) {
            return;
        }
        jiggleSequenceActive = true;
        jiggleStep = 0;
        nextJiggleStepAt = now;
        Serial.println("Idle timeout reached; starting 10px square and left-click");
    }

    if (static_cast<int32_t>(now - nextJiggleStepAt) < 0) {
        return;
    }

    if (jiggleStep >= 6) {
        jiggleSequenceActive = false;
        Serial.println("Idle square and left-click complete");
        return;
    }

    uint8_t buttons = 0;
    int16_t x = 0;
    int16_t y = 0;
    switch (jiggleStep) {
        case 0:
            x = kJiggleSquareSide;
            break;
        case 1:
            y = kJiggleSquareSide;
            break;
        case 2:
            x = -kJiggleSquareSide;
            break;
        case 3:
            y = -kJiggleSquareSide;
            break;
        case 4:
            buttons = 0x01;
            break;
        case 5:
            break;
        default:
            jiggleSequenceActive = false;
            return;
    }

    if (!notifyHost(buttons, x, y)) {
        Serial.println("Idle jiggle report was not delivered; cancelling sequence");
        jiggleSequenceActive = false;
        return;
    }

    ++jiggleStep;
    nextJiggleStepAt = now + kJiggleStepIntervalMs;
}

void updateStatusLed(uint32_t now, bool resetBlink = false) {
    if (!jiggleEnabled) {
        ledOn = false;
        digitalWrite(kLedPin, HIGH);
        return;
    }

    if (resetBlink) {
        ledOn = true;
        ledLastToggleAt = now;
    } else if (static_cast<uint32_t>(now - ledLastToggleAt) >= kLedBlinkIntervalMs / 2) {
        ledOn = !ledOn;
        ledLastToggleAt = now;
    }
    digitalWrite(kLedPin, ledOn ? LOW : HIGH);
}

void updateJiggleToggle(uint32_t now) {
    const bool reading = digitalRead(kBootButtonPin);
    if (reading != rawButtonState) {
        rawButtonState = reading;
        buttonStateChangedAt = now;
    }

    if (static_cast<uint32_t>(now - buttonStateChangedAt) < kButtonDebounceMs ||
        stableButtonState == rawButtonState) {
        return;
    }

    stableButtonState = rawButtonState;
    if (stableButtonState != LOW) {
        return;
    }

    jiggleEnabled = !jiggleEnabled;
    jiggleSequenceActive = false;
    activity->noteActivity(now);
    if (!jiggleEnabled && hidServer->getConnectedCount() > 0) {
        notifyHost(0, 0, 0);
    }
    updateStatusLed(now, jiggleEnabled);
    Serial.printf("Jiggler %s\n", jiggleEnabled ? "enabled" : "disabled");
}

void setupMouseHid() {
    hidServer = NimBLEDevice::createServer();
    hidServer->setCallbacks(&hostServerCallbacks, false);
    hidServer->advertiseOnDisconnect(true);

    NimBLEHIDDevice* hid = new NimBLEHIDDevice(hidServer);
    mouseInput = hid->getInputReport(1);
    if (mouseInput == nullptr) {
        ESP_LOGE(kTag, "Could not create HID mouse input report");
        abort();
    }
    mouseInput->setCallbacks(&hostReportCallbacks);
    hid->setManufacturer("ESP32-C3");
    hid->setPnp(0x02, 0x303A, 0x1001, 0x0100);
    hid->setHidInfo(0x00, 0x01);
    hid->setReportMap(const_cast<uint8_t*>(kMouseReportMap), sizeof(kMouseReportMap));

    if (!hidServer->start()) {
        ESP_LOGE(kTag, "Could not start HID GATT services");
        abort();
    }

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->setAppearance(HID_MOUSE);
    advertising->addServiceUUID(hid->getHidService()->getUUID());
    if (!advertising->setName(kDeviceName) || !advertising->start()) {
        ESP_LOGE(kTag, "Could not advertise HID mouse");
        abort();
    }
    Serial.printf("Advertising standalone BLE mouse as %s\n", kDeviceName);
}

}  // namespace

void setup() {
    pinMode(kBootButtonPin, INPUT_PULLUP);
    pinMode(kLedPin, OUTPUT);
    rawButtonState = digitalRead(kBootButtonPin);
    stableButtonState = rawButtonState;
    buttonStateChangedAt = millis();
    updateStatusLed(millis());

    Serial.begin(115200);
    const uint32_t serialWaitStarted = millis();
    while (!Serial && static_cast<uint32_t>(millis() - serialWaitStarted) < 3000) {
        delay(10);
    }

    Serial.println();
    Serial.println("ESP32-C3 standalone mouse jiggler starting");
    activity = new MouseActivity(kIdleTimeoutMs, kJiggleIntervalMs, millis());

    if (!NimBLEDevice::init(kDeviceName)) {
        ESP_LOGE(kTag, "Could not initialize NimBLE");
        abort();
    }
    if (!NimBLEDevice::setPower(ESP_PWR_LVL_P9)) {
        ESP_LOGW(kTag, "Could not set BLE transmit power");
    }
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    setupMouseHid();
}

void loop() {
    const uint32_t now = millis();
    updateJiggleToggle(now);
    updateStatusLed(now);
    processIdleJiggle(now);
    delay(5);
}
