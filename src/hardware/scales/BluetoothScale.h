/**
 * @file BluetoothScale.h
 * @brief Bluetooth scale implementation using AcaiaArduinoBLE library
 */

#pragma once

#include "Scale.h"
#include <AcaiaArduinoBLE.h>

#ifdef CC_ORIONE
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

/**
 * @brief Bluetooth scale implementation for Acaia and compatible scales
 */
class BluetoothScale : public Scale {
    public:
        explicit BluetoothScale(bool debug = false);

        ~BluetoothScale() override;

        bool init() override;
        bool update() override;
        [[nodiscard]] float getWeight() const override;
        void tare() override;
        void setSamples(int samples) override;
        [[nodiscard]] bool isConnected() const override;
        void startTimer() const;
        void stopTimer() const;
        void resetTimer() const;

        void updateConnection();
        [[nodiscard]] bool isConnecting() const;

#ifdef CC_ORIONE
        /**
         * @brief Orione build: scanning, connecting and cleanup (with delay() and blocking BLE calls, up
         *        to ~1 s measured) run in their own task on core 0, so loop() and the display never wait.
         *        update(), tare() and the timer commands only try the lock: while the task is busy the
         *        scale is not connected anyway, and loop() skips the round.
         */
        void startConnectionTask();

        /** Remaining battery of the scale in percent, -1 if not connected or the scale does not report it */
        [[nodiscard]] int getBattery() const;

        /**
         * @brief Choosing the scale on the web page (Einstellungen → Waage): only the chosen one is
         *        connected, none without a choice. Requests come from the web server's task and are
         *        applied by the scale task; found() and target() may be read from any task.
         */
        void requestTarget(const char* address); // "aa:bb:cc:dd:ee:ff", "" = forget
        void requestDiscover();                  // collect the scales around for kDiscoverMs
        int found(orione::FoundScale* out, int max);
        [[nodiscard]] bool discovering();
        [[nodiscard]] bool hasTarget();
        static constexpr uint32_t kDiscoverMs = 12000;

        /** No search for the scale while paused (standby): WiFi has the radio alone. A connected scale stays. */
        void pauseSearch(bool paused) {
            searchPaused_ = paused;
        }
#endif

    private:
        AcaiaArduinoBLE* bleScale;
        float currentWeight;
        unsigned long lastUpdateTime;
        bool connected;

        // Connection retry mechanism
        bool bleInitialized;
        unsigned long lastConnectionAttempt;
        unsigned long connectionAttemptInterval;

        bool isUpdatingConnection;
        unsigned long maxConnectionAttemptInterval;

#ifdef CC_ORIONE
        static void connectionTask(void* self);
        void applyRequests();
        SemaphoreHandle_t lock_ = nullptr;
        portMUX_TYPE requestLock_ = portMUX_INITIALIZER_UNLOCKED;
        char target_[18] = {};        // the chosen scale, "" = none
        char pendingTarget_[18] = {}; // set by the web page, applied by the scale task
        bool targetPending_ = false;
        bool discoverPending_ = false;
        volatile bool searchPaused_ = false; // pauseSearch(), applied by the scale task
        uint32_t fakeDiscoverUntil_ = 0;     // bench build: the simulated search
#endif
};
