#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "jiggler_config.h"
#include "mouse_activity.h"

namespace {

constexpr char kDeviceName[] = "BT Mouse";
constexpr uint32_t kJiggleStepIntervalMs = 25;
constexpr uint8_t kBootButtonPin = 9;
constexpr uint8_t kLedPin = 8;
constexpr uint32_t kButtonDebounceMs = 50;
constexpr uint32_t kPairingLedBlinkIntervalMs = 250;
constexpr uint32_t kPairingLongPressMs = 2000;
constexpr uint32_t kPairingConnectionStableMs = 2000;
constexpr uint32_t kConnectionLedFeedbackMs = 3000;
constexpr char kTag[] = "mouse-jiggler";
constexpr uint8_t kLeftMouseButton = 0x01;
constexpr uint8_t kJiggleReportCount = 6;
constexpr size_t kSerialCommandBufferSize = 48;

struct MouseReport {
    uint8_t buttons;
    int16_t x;
    int16_t y;
};

std::array<MouseReport, kJiggleReportCount> makeJiggleReports(uint32_t squareSidePixels) {
    const int16_t side = static_cast<int16_t>(squareSidePixels);
    return {{
        {0, side, 0},
        {0, 0, side},
        {0, static_cast<int16_t>(-side), 0},
        {0, 0, static_cast<int16_t>(-side)},
        {kLeftMouseButton, 0, 0},
        {0, 0, 0},
    }};
}

struct BleConnectionEvent {
    bool connected;
    uint32_t timestamp;
    uint16_t interval;
    int reason;
};

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
JigglerConfig jigglerConfig;
std::array<MouseReport, kJiggleReportCount> jiggleReports =
    makeJiggleReports(jigglerConfig.squareSidePixels);
MouseActivity activity(jigglerConfig.idleTimeoutSeconds * 1000,
                       jigglerConfig.repeatIntervalSeconds * 1000,
                       0);
QueueHandle_t bleConnectionEvents = nullptr;
bool jiggleSequenceActive = false;
uint8_t jiggleStep = 0;
uint32_t nextJiggleStepAt = 0;
bool jiggleEnabled = false;
bool rawButtonState = HIGH;
bool stableButtonState = HIGH;
uint32_t buttonStateChangedAt = 0;
uint32_t buttonPressedAt = 0;
bool longPressHandled = false;
uint32_t ledLastToggleAt = 0;
bool ledOn = false;
bool pairingMode = false;
bool connectionLedFeedback = false;
uint32_t connectionLedFeedbackStartedAt = 0;
bool pairingSessionActive = false;
bool pairingConnectionPending = false;
uint32_t pairingConnectionStartedAt = 0;
bool bondDeletionPending = false;
uint32_t bleConnectionStartedAt = 0;
char serialCommandBuffer[kSerialCommandBufferSize]{};
size_t serialCommandLength = 0;

void updateStatusLed(uint32_t now);

void loadJigglerConfig() {
    static_assert(sizeof(JigglerConfig) == 3 * sizeof(uint32_t));
    Preferences preferences;
    if (!preferences.begin("jiggler", true)) {
        Serial.println("ERROR: Could not open settings storage; using defaults");
        return;
    }

    const size_t storedSize = preferences.getBytesLength("config");
    if (storedSize == sizeof(JigglerConfig)) {
        JigglerConfig storedConfig;
        if (preferences.getBytes("config", &storedConfig, sizeof(storedConfig)) ==
                sizeof(storedConfig) &&
            isValidJigglerConfig(storedConfig)) {
            jigglerConfig = storedConfig;
        } else {
            Serial.println("WARNING: Stored settings are invalid; using defaults");
        }
    } else if (storedSize != 0) {
        Serial.println("WARNING: Stored settings have an unsupported size; using defaults");
    }
    preferences.end();

    jiggleReports = makeJiggleReports(jigglerConfig.squareSidePixels);
    activity = MouseActivity(jigglerConfig.idleTimeoutSeconds * 1000,
                             jigglerConfig.repeatIntervalSeconds * 1000,
                             millis());
    Serial.printf("Jiggle settings: idle %lu s, repeat %lu s, square %lu px\n",
                  static_cast<unsigned long>(jigglerConfig.idleTimeoutSeconds),
                  static_cast<unsigned long>(jigglerConfig.repeatIntervalSeconds),
                  static_cast<unsigned long>(jigglerConfig.squareSidePixels));
}

void processSerialCommand(const char* command) {
    if (std::strcmp(command, "PING") == 0) {
        Serial.println("CONFIG_READY");
        return;
    }

    JigglerConfig requestedConfig;
    if (!parseJigglerConfigCommand(command, requestedConfig)) {
        Serial.println("CONFIG_ERROR: Invalid settings");
        return;
    }

    Preferences preferences;
    if (!preferences.begin("jiggler", false)) {
        Serial.println("CONFIG_ERROR: Could not open settings storage");
        return;
    }

    const size_t written =
        preferences.putBytes("config", &requestedConfig, sizeof(requestedConfig));
    preferences.end();
    if (written != sizeof(requestedConfig)) {
        Serial.println("CONFIG_ERROR: Could not save settings");
        return;
    }

    const uint32_t now = millis();
    jigglerConfig = requestedConfig;
    jiggleReports = makeJiggleReports(jigglerConfig.squareSidePixels);
    activity = MouseActivity(jigglerConfig.idleTimeoutSeconds * 1000,
                             jigglerConfig.repeatIntervalSeconds * 1000,
                             now);
    jiggleSequenceActive = false;
    Serial.println("CONFIG_SAVED");
}

void processSerialCommands() {
    while (Serial.available() > 0) {
        const char value = static_cast<char>(Serial.read());
        if (value == '\r') {
            continue;
        }
        if (value == '\n') {
            if (serialCommandLength > 0) {
                serialCommandBuffer[serialCommandLength] = '\0';
                processSerialCommand(serialCommandBuffer);
                serialCommandLength = 0;
            }
            continue;
        }

        if (value < ' ' || value > '~' ||
            serialCommandLength >= sizeof(serialCommandBuffer) - 1) {
            serialCommandLength = 0;
            while (Serial.available() > 0 && Serial.read() != '\n') {
            }
            Serial.println("CONFIG_ERROR: Invalid command");
            continue;
        }
        serialCommandBuffer[serialCommandLength++] = value;
    }
}

void enqueueBleConnectionEvent(const BleConnectionEvent& event) {
    if (bleConnectionEvents == nullptr ||
        xQueueSend(bleConnectionEvents, &event, 0) != pdTRUE) {
        Serial.println("ERROR: BLE connection event queue is full");
    }
}

void processBleConnectionEvents() {
    BleConnectionEvent event{};
    while (xQueueReceive(bleConnectionEvents, &event, 0) == pdTRUE) {
        jiggleSequenceActive = false;
        activity.noteActivity(event.timestamp);

        if (event.connected) {
            bleConnectionStartedAt = event.timestamp;
            if (pairingSessionActive) {
                pairingConnectionPending = true;
                pairingConnectionStartedAt = event.timestamp;
            }
            Serial.printf("Computer connected; BLE interval %.1f ms\n",
                          event.interval * 1.25f);
            continue;
        }

        if (pairingSessionActive) {
            pairingConnectionPending = false;
            pairingMode = true;
            ledOn = true;
            ledLastToggleAt = event.timestamp;
        }
        Serial.printf("Computer disconnected after %lu ms (reason %d: %s)\n",
                      static_cast<unsigned long>(event.timestamp - bleConnectionStartedAt),
                      event.reason,
                      NimBLEUtils::returnCodeToString(event.reason));
    }
}

void deleteSavedBonds() {
    Serial.println("Deleting saved BLE bonds");
    if (!NimBLEDevice::deleteAllBonds()) {
        Serial.println("ERROR: Could not delete all saved BLE bonds");
    }
}

void startPairingAdvertising() {
    hidServer->advertiseOnDisconnect(true);
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (!advertising->isAdvertising() && !advertising->start()) {
        Serial.println("ERROR: Could not start BLE advertising for pairing");
    }
}

void processPendingBondDeletion() {
    if (!bondDeletionPending || hidServer->getConnectedCount() != 0) {
        return;
    }

    deleteSavedBonds();
    bondDeletionPending = false;
    startPairingAdvertising();
}

void processPairingConnection(uint32_t now) {
    if (!pairingConnectionPending || hidServer->getConnectedCount() == 0) {
        return;
    }

    if (static_cast<uint32_t>(now - pairingConnectionStartedAt) >=
        kPairingConnectionStableMs) {
        pairingConnectionPending = false;
        pairingSessionActive = false;
        pairingMode = false;
        connectionLedFeedback = true;
        connectionLedFeedbackStartedAt = now;
        updateStatusLed(now);
        Serial.println("BLE pairing complete; flashing LED three times");
    }
}

class HostReportCallbacks final : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
        Serial.printf("Computer HID report subscription: %u\n", subValue);
    }
};

class HostServerCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& connInfo) override {
        enqueueBleConnectionEvent(
            {true, millis(), connInfo.getConnInterval(), 0});
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        enqueueBleConnectionEvent({false, millis(), 0, reason});
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
        if (!activity.shouldJiggle(now)) {
            return;
        }
        jiggleSequenceActive = true;
        jiggleStep = 0;
        nextJiggleStepAt = now;
        Serial.printf("Idle timeout reached; starting %lu px square and left-click\n",
                      static_cast<unsigned long>(jigglerConfig.squareSidePixels));
    }

    if (static_cast<int32_t>(now - nextJiggleStepAt) < 0) {
        return;
    }

    if (jiggleStep >= jiggleReports.size()) {
        jiggleSequenceActive = false;
        Serial.println("Idle square and left-click complete");
        return;
    }

    const MouseReport& report = jiggleReports[jiggleStep];

    if (!notifyHost(report.buttons, report.x, report.y)) {
        Serial.println("Idle jiggle report was not delivered; cancelling sequence");
        jiggleSequenceActive = false;
        return;
    }

    ++jiggleStep;
    nextJiggleStepAt = now + kJiggleStepIntervalMs;
}

void updateStatusLed(uint32_t now) {
    if (pairingMode) {
        if (static_cast<uint32_t>(now - ledLastToggleAt) >=
            kPairingLedBlinkIntervalMs / 2) {
            ledOn = !ledOn;
            ledLastToggleAt = now;
        }
    } else if (connectionLedFeedback) {
        const uint32_t elapsed = static_cast<uint32_t>(now - connectionLedFeedbackStartedAt);
        if (elapsed >= kConnectionLedFeedbackMs) {
            connectionLedFeedback = false;
            ledOn = jiggleEnabled;
        } else {
            const uint32_t phase = elapsed * 6 / kConnectionLedFeedbackMs;
            ledOn = phase % 2 == 0;
        }
    } else {
        ledOn = jiggleEnabled;
    }
    digitalWrite(kLedPin, ledOn ? LOW : HIGH);
}

void enterPairingMode(uint32_t now) {
    pairingSessionActive = true;
    pairingConnectionPending = false;
    pairingMode = true;
    jiggleSequenceActive = false;
    activity.noteActivity(now);
    ledOn = true;
    ledLastToggleAt = now;
    updateStatusLed(now);
    Serial.println("BLE pairing mode enabled");

    hidServer->advertiseOnDisconnect(false);
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (advertising->isAdvertising()) {
        advertising->stop();
    }

    const std::vector<uint16_t> peers = hidServer->getPeerDevices();
    bondDeletionPending = !peers.empty();
    for (uint16_t connHandle : peers) {
        if (!hidServer->disconnect(connHandle)) {
            Serial.printf("Could not disconnect BLE host on connection %u\n", connHandle);
        }
    }

    if (!bondDeletionPending) {
        deleteSavedBonds();
        startPairingAdvertising();
    }
}

void enterStartupPairingMode(uint32_t now) {
    pairingSessionActive = true;
    pairingConnectionPending = false;
    pairingMode = true;
    ledOn = true;
    ledLastToggleAt = now;
    updateStatusLed(now);
    Serial.println("No BLE computer connected at startup; entering pairing mode (saved bonds preserved)");
}

void toggleJiggler(uint32_t now) {
    jiggleEnabled = !jiggleEnabled;
    jiggleSequenceActive = false;
    activity.noteActivity(now);
    if (!jiggleEnabled && hidServer->getConnectedCount() > 0) {
        notifyHost(0, 0, 0);
    }
    updateStatusLed(now);
    Serial.printf("Jiggler %s\n", jiggleEnabled ? "enabled" : "disabled");
}

void updateButton(uint32_t now) {
    const bool reading = digitalRead(kBootButtonPin);
    if (reading != rawButtonState) {
        rawButtonState = reading;
        buttonStateChangedAt = now;
    }

    if (rawButtonState != stableButtonState &&
        static_cast<uint32_t>(now - buttonStateChangedAt) >= kButtonDebounceMs) {
        const bool previousStableState = stableButtonState;
        stableButtonState = rawButtonState;
        if (previousStableState == HIGH && stableButtonState == LOW) {
            buttonPressedAt = now;
            longPressHandled = false;
        } else if (previousStableState == LOW && stableButtonState == HIGH && !longPressHandled) {
            toggleJiggler(now);
        }
    }

    if (rawButtonState == LOW && stableButtonState == LOW && !longPressHandled &&
        static_cast<uint32_t>(now - buttonPressedAt) >= kPairingLongPressMs) {
        longPressHandled = true;
        enterPairingMode(now);
    }
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
    if (!advertising->setName(kDeviceName)) {
        ESP_LOGE(kTag, "Could not configure BLE mouse advertising");
        abort();
    }
    enterStartupPairingMode(millis());
    if (!advertising->start()) {
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
    bleConnectionEvents = xQueueCreate(8, sizeof(BleConnectionEvent));
    if (bleConnectionEvents == nullptr) {
        ESP_LOGE(kTag, "Could not create BLE connection event queue");
        abort();
    }
    loadJigglerConfig();

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
    processBleConnectionEvents();
    processSerialCommands();
    const uint32_t now = millis();
    updateButton(now);
    updateStatusLed(now);
    processPendingBondDeletion();
    processPairingConnection(now);
    processIdleJiggle(now);
    delay(5);
}
