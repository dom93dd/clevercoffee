/**
 * @file heapStages.h
 *
 * @brief Orione build (CC_ORIONE): the free heap after each step of the start, kept for GET /heap. Shows on the
 *        machine what the display, WiFi, the web server, OTA and the Bluetooth scale take (09.10.2026: only ~18 KB
 *        left in the machine, 50 KB on the desk on 03.10.2026). No dependencies, records 12 steps at most.
 */

#pragma once

#include <esp_heap_caps.h>

namespace heap_stages {

    struct Stage {
            const char* name;
            uint32_t free;
            uint32_t block;
            uint32_t ms;
    };

    constexpr int kMax = 12;
    inline Stage stages[kMax];
    inline int count = 0;

    /** @param name a string literal */
    inline void mark(const char* name) {
        if (count < kMax) {
            stages[count++] = {name, static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT)), static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                               static_cast<uint32_t>(millis())};
        }
    }

} // namespace heap_stages
