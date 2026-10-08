/**
 * @file firmwareGuard.h
 *
 * @brief Orione build (CC_ORIONE): a firmware that came over WiFi is only taken as good once it runs. The
 *        bootloader starts a new app in "pending verify"; until it is confirmed, any reset (crash,
 *        watchdog, power) starts the previous app again (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, set in the
 *        Arduino framework). Arduino would confirm it at once; verifyRollbackLater() in main.cpp keeps that
 *        for here: confirmed 30 s after the start with WiFi up (or after 10 min without), and on a restart
 *        on purpose. So a firmware that breaks the start or WiFi cannot lock the machine out of updates
 *        over WiFi (Dominik, 08.10.2026). A USB flash is never pending.
 */

#pragma once

#include <esp_ota_ops.h>
#include <esp_system.h>

namespace firmware_guard {

    constexpr unsigned long kConfirmOnlineMs = 30000;
    constexpr unsigned long kConfirmAnywayMs = 600000;

    inline bool pending = false;          // came by OTA and not confirmed yet
    inline bool lastUpdateFailed = false; // an update did not start and the bootloader went back to this one
    inline bool skipOnRestart = false;    // the heap safety net restarts: no sign of a good firmware

    inline void confirm(const char* why) {
        if (!pending) {
            return;
        }

        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            pending = false;
            LOGF(INFO, "Firmware confirmed (%s)", why);
        }
    }

    inline void onRestart() {
        if (!skipOnRestart) {
            confirm("restart on purpose");
        }
    }

    /** Call early in setup() */
    inline void begin() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state;
        pending = running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
        lastUpdateFailed = esp_ota_get_last_invalid_partition() != nullptr;
        esp_register_shutdown_handler(onRestart); // runs on esp_restart(), not on a crash or the watchdog

        if (pending) {
            LOG(INFO, "Firmware new over WiFi: confirmed once it runs");
        }

        if (lastUpdateFailed) {
            LOG(WARNING, "Firmware: an update did not start, running the one before");
        }
    }

    /** Call from loop() */
    inline void loop(const bool online) {
        if (pending && ((online && millis() > kConfirmOnlineMs) || millis() > kConfirmAnywayMs)) {
            confirm(online ? "running with WiFi" : "running for 10 min");
        }
    }

} // namespace firmware_guard
