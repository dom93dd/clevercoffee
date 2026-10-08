/**
 * @file BluetoothScale.cpp
 * @brief Bluetooth scale implementation
 */

#include "BluetoothScale.h"
#include "Logger.h"
#include <Arduino.h>

#ifdef CC_ORIONE
#include "Config.h"
#include <cstring>

extern Config config; // the chosen scale is a setting (hardware.sensors.scale.address)
#endif

#ifdef CC_ORIONE
void BluetoothScale::requestTarget(const char* address) {
    char normalized[sizeof(pendingTarget_)];
    orione::ScaleList::normalize(address, normalized);
    portENTER_CRITICAL(&requestLock_);
    std::memcpy(pendingTarget_, normalized, sizeof(pendingTarget_));
    targetPending_ = true;
    portEXIT_CRITICAL(&requestLock_);
}

void BluetoothScale::requestDiscover() {
    portENTER_CRITICAL(&requestLock_);
    discoverPending_ = true;
    portEXIT_CRITICAL(&requestLock_);
}

bool BluetoothScale::hasTarget() {
    portENTER_CRITICAL(&requestLock_);
    const bool chosen = target_[0] != '\0';
    portEXIT_CRITICAL(&requestLock_);
    return chosen;
}
#endif

#ifdef CC_FAKE_SCALE
#ifndef ROUND_TIMING
#error "CC_FAKE_SCALE is for the bench build esp32_round_bench only, never for the machine"
#endif
/*
 * Bench build only (-D CC_FAKE_SCALE): a simulated scale instead of Bluetooth, so flow, first
 * drops and brew by weight can be tried on the desk. No BLE is started. The weight follows the
 * pump relay (own rough numbers, not measured): nothing for 5 s while the puck soaks, then up to
 * 2 g/s within 3 s and slowly more towards the end; after the pump stops 3 s of drops; reported
 * 10 times a second with a little noise.
 */
#include "benchSwitch.h"
#include "hardware/GPIOPin.h"

extern GPIOPin* pumpRelayPin;

namespace {
    float simGrams = 0.0f, simTare = 0.0f, simDrip = 0.0f;
    unsigned long simLastMs = 0, simPumpOnMs = 0;
    bool simPumping = false;
}

BluetoothScale::BluetoothScale(bool debug) :
    bleScale(nullptr), currentWeight(0), lastUpdateTime(0), connected(false), bleInitialized(true), lastConnectionAttempt(0), connectionAttemptInterval(5000), isUpdatingConnection(false),
    maxConnectionAttemptInterval(30000) {
    LOG(WARNING, "SIMULATED SCALE: bench build only, no Bluetooth");
    orione::ScaleList::normalize(config.get<String>("hardware.sensors.scale.address").c_str(), target_);
    connected = target_[0] != '\0'; // like the real one: connected only to a chosen scale
}

BluetoothScale::~BluetoothScale() = default;

bool BluetoothScale::init() {
    return true;
}

void BluetoothScale::updateConnection() {}

bool BluetoothScale::isConnecting() const {
    return false;
}

bool BluetoothScale::update() {
    const unsigned long now = millis();
    applyRequests();

    if (now - simLastMs < 100) {
        return false;
    }

    const float dt = simLastMs == 0 ? 0.0f : (now - simLastMs) / 1000.0f;
    simLastMs = now;
    const bool pumping = pumpRelayPin != nullptr && pumpRelayPin->read() == HIGH; // high-level trigger module

    if (pumping && !simPumping) {
        simPumpOnMs = now;
    }

    if (!pumping && simPumping) {
        simDrip = 0.5f; // the drops after the stop, fading out
    }

    simPumping = pumping;
    float flow = 0.0f;

    if (pumping) {
        const float t = (now - simPumpOnMs) / 1000.0f;
        flow = t < 5.0f ? 0.0f : t < 8.0f ? (t - 5.0f) / 3.0f * 2.0f : 2.0f + (t - 8.0f) * 0.02f;
    }
    else if (simDrip > 0.0f) {
        flow = simDrip;
        simDrip = std::max(0.0f, simDrip - dt / 6.0f); // 0.5 g/s down to 0 in 3 s
    }

    if (bench::scaleMissed) {
        flow = 0.0f;
    }

    simGrams += flow * dt + bench::scaleAddGrams;
    bench::scaleAddGrams = 0.0f;
    currentWeight = simGrams - simTare + (static_cast<int>(now / 100) % 5 - 2) * 0.02f;
    lastUpdateTime = now;
    return true;
}

float BluetoothScale::getWeight() const {
    return currentWeight;
}

void BluetoothScale::tare() {
    simTare = simGrams;
    currentWeight = 0.0f;
}

void BluetoothScale::startTimer() const {}

void BluetoothScale::stopTimer() const {}

void BluetoothScale::resetTimer() const {}

void BluetoothScale::setSamples(int samples) {}

bool BluetoothScale::isConnected() const {
    return connected && !bench::scaleOff;
}

void BluetoothScale::startConnectionTask() {}

int BluetoothScale::getBattery() const {
    return connected ? 76 : -1;
}

void BluetoothScale::applyRequests() {
    portENTER_CRITICAL(&requestLock_);
    if (targetPending_) {
        std::memcpy(target_, pendingTarget_, sizeof(target_));
        connected = target_[0] != '\0';
        targetPending_ = false;
    }
    if (discoverPending_) {
        fakeDiscoverUntil_ = millis() + kDiscoverMs;
        discoverPending_ = false;
    }
    portEXIT_CRITICAL(&requestLock_);
}

int BluetoothScale::found(orione::FoundScale* out, const int max) {
    if (!discovering() || max < 1) {
        return 0;
    }

    out[0] = orione::FoundScale{};
    std::strcpy(out[0].name, "BOOKOO_SC SIM");
    std::strcpy(out[0].address, "02:00:00:00:00:01");
    out[0].rssi = -55;
    return 1;
}

bool BluetoothScale::discovering() {
    return static_cast<int32_t>(fakeDiscoverUntil_ - millis()) > 0;
}

void BluetoothScale::connectionTask(void* self) {}

#else // the real scale

#ifdef CC_ORIONE
namespace {
    /** Takes the scale lock for the scope, waiting at most `wait` ticks */
    class ScaleLock {
        public:
            ScaleLock(SemaphoreHandle_t lock, const TickType_t wait) :
                lock_(lock), held_(lock != nullptr && xSemaphoreTake(lock, wait) == pdTRUE) {
            }

            ~ScaleLock() {
                if (held_) {
                    xSemaphoreGive(lock_);
                }
            }

            explicit operator bool() const {
                return held_;
            }

        private:
            SemaphoreHandle_t lock_;
            bool held_;
    };
}

// Leaves the function (with `onBusy`) if the connection task holds the scale longer than `wait`
#define SCALE_LOCK(wait, onBusy)              \
    const ScaleLock scaleLock(lock_, (wait)); \
    if (!scaleLock) {                         \
        onBusy;                               \
    }
#else
#define SCALE_LOCK(wait, onBusy)
#endif

BluetoothScale::BluetoothScale(bool debug) :
    currentWeight(0.0), lastUpdateTime(0), connected(false), bleInitialized(false), lastConnectionAttempt(0), connectionAttemptInterval(5000), isUpdatingConnection(false), maxConnectionAttemptInterval(30000) {
    bleScale = new AcaiaArduinoBLE(debug);
#ifdef CC_ORIONE
    lock_ = xSemaphoreCreateMutex();
#endif
}

BluetoothScale::~BluetoothScale() {
    delete bleScale;
}

bool BluetoothScale::init() {
    LOG(INFO, "Starting Bluetooth scale initialization");

#ifdef CC_ORIONE
    // only the scale chosen on the web page, none without a choice
    orione::ScaleList::normalize(config.get<String>("hardware.sensors.scale.address").c_str(), target_);
    bleScale->requireTarget(true);
    const bool success = bleScale->init(target_);
#else
    const bool success = bleScale->init();
#endif

    if (success) {
        bleInitialized = true;
        lastConnectionAttempt = millis();
        LOG(INFO, "BLE Scale initialization successful");
    }
    else {
        LOG(ERROR, "BLE Scale initialization failed");
        bleInitialized = false;
    }

    return success;
}

void BluetoothScale::updateConnection() {
    if (!bleInitialized) {
        return;
    }

    SCALE_LOCK(portMAX_DELAY, return);
#ifdef CC_ORIONE
    applyRequests();
#endif

    const unsigned long currentTime = millis();

    const bool wasConnecting = bleScale->isConnecting();
    bleScale->updateConnection();

    // Only update timing if we're not in a connection process or if connection just started
    if (!wasConnecting || bleScale->isConnecting()) {
        lastConnectionAttempt = currentTime;
    }

    // Check for connection state changes
    if (const bool newConnected = bleScale->isConnected(); newConnected != connected) {
        connected = newConnected;

        if (connected) {
            LOG(INFO, "Bluetooth scale connected");
            // Reset connection attempt interval on successful connection
            connectionAttemptInterval = 5000;
        }
        else {
            LOG(INFO, "Bluetooth scale disconnected");
            // Only increase interval if we're not actively connecting
            if (!bleScale->isConnecting()) {
                connectionAttemptInterval = min(connectionAttemptInterval * 2, 30000UL);
            }
        }
    }

    // If connection failed and we're not connecting, wait before retry
    if (!connected && !bleScale->isConnecting()) {
        if (currentTime - lastConnectionAttempt < connectionAttemptInterval) {
            return;
        }

        // Restart connection process by calling init again
        bleScale->init();
    }
}

bool BluetoothScale::isConnecting() const {
    if (!bleInitialized) {
        return false;
    }

    return bleScale->isConnecting();
}

bool BluetoothScale::update() {
    if (!bleInitialized) {
        return false;
    }

    SCALE_LOCK(0, return false);

    if (connected) {
        if (bleScale->heartbeatRequired()) {
            bleScale->heartbeat();
        }

        if (bleScale->newWeightAvailable()) {
            // Allow negative values (post-tare) but filter out clearly invalid readings
            if (const float newWeight = bleScale->getWeight(); newWeight > -1000.0f && newWeight < 10000.0f) {
                currentWeight = newWeight;
                return true;
            }
        }
    }

    return false;
}

float BluetoothScale::getWeight() const {
    return currentWeight;
}

void BluetoothScale::tare() {
    SCALE_LOCK(pdMS_TO_TICKS(50), return);

    if (connected) {
        bleScale->tare();
    }
}

void BluetoothScale::startTimer() const {
    SCALE_LOCK(pdMS_TO_TICKS(50), return);

    if (connected) {
        return bleScale->startTimer();
    }
}

void BluetoothScale::stopTimer() const {
    SCALE_LOCK(pdMS_TO_TICKS(50), return);

    if (connected) {
        return bleScale->stopTimer();
    }
}

void BluetoothScale::resetTimer() const {
    SCALE_LOCK(pdMS_TO_TICKS(50), return);

    if (connected) {
        return bleScale->resetTimer();
    }
}

void BluetoothScale::setSamples(int samples) {
    // Most BLE scales handle sampling internally
}

bool BluetoothScale::isConnected() const {
    return connected;
}

#ifdef CC_ORIONE
void BluetoothScale::startConnectionTask() {
#ifdef CC_ORIONE
    xTaskCreatePinnedToCore(connectionTask, "scale", 3328, this, 1, nullptr, 0); // used ~1.9 KB at most, connected (GET /boot)
#else
    xTaskCreatePinnedToCore(connectionTask, "scale", 4096, this, 1, nullptr, 0);
#endif
}

int BluetoothScale::getBattery() const {
    return connected ? bleScale->getBattery() : -1;
}

// scale task, holding the scale lock
void BluetoothScale::applyRequests() {
    char target[sizeof(pendingTarget_)];
    portENTER_CRITICAL(&requestLock_);
    const bool newTarget = targetPending_, discover = discoverPending_;
    std::memcpy(target, pendingTarget_, sizeof(target));
    targetPending_ = discoverPending_ = false;
    portEXIT_CRITICAL(&requestLock_);

    if (newTarget) {
        bleScale->setTarget(target);
        portENTER_CRITICAL(&requestLock_);
        std::memcpy(target_, target, sizeof(target_));
        portEXIT_CRITICAL(&requestLock_);
        LOGF(INFO, "Bluetooth scale: %s", target[0] ? target : "none chosen");
    }

    if (discover) {
        bleScale->discover(kDiscoverMs);
        LOG(INFO, "Bluetooth scale: searching");
    }
}

int BluetoothScale::found(orione::FoundScale* out, const int max) {
    SCALE_LOCK(pdMS_TO_TICKS(50), return 0);
    return bleScale->found(out, max);
}

bool BluetoothScale::discovering() {
    portENTER_CRITICAL(&requestLock_);
    const bool pending = discoverPending_;
    portEXIT_CRITICAL(&requestLock_);
    SCALE_LOCK(pdMS_TO_TICKS(50), return pending);
    return pending || bleScale->discovering();
}

void BluetoothScale::connectionTask(void* self) {
    for (;;) {
        static_cast<BluetoothScale*>(self)->updateConnection();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
#endif

#endif // CC_FAKE_SCALE
