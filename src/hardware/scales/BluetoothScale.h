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
        SemaphoreHandle_t lock_ = nullptr;
#endif
};
