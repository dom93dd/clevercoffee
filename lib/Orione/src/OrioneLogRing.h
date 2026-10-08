/**
 * @file OrioneLogRing.h
 *
 * @brief The last log lines, kept where a software restart, a crash or a watchdog does not wipe them
 *        (RTC memory on the ESP32, src/orioneLog.h), so GET /log can show what happened right before an
 *        unexpected restart (Dominik, 08.10.2026: "mir ist wichtig, dass wir auch danach per wlan genauso
 *        weiterentwicklen können"). A power loss clears it: the header then does not check out. No Arduino
 *        dependencies: tested in simulator/test/test_orione.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orione {

    template <size_t N>
    struct LogRing {
            static constexpr uint32_t kMagic = 0x4C4F4752; // "LOGR"
            static constexpr uint32_t kSalt = 0xA5C3E10Fu;

            uint32_t magic;
            uint32_t written; // bytes ever written since the last clear; the newest N are kept
            uint32_t check;   // magic ^ written ^ kSalt: the header is ours and consistent
            char data[N];

            void clear() {
                magic = kMagic;
                written = 0;
                check = magic ^ written ^ kSalt;
            }

            bool valid() const {
                return magic == kMagic && check == (magic ^ written ^ kSalt);
            }

            /** @param keep the memory survived (a restart, not a power-up): continue the old log if it checks out */
            void begin(const bool keep) {
                if (!keep || !valid()) {
                    clear();
                }
            }

            void append(const char* text) {
                if (text != nullptr) {
                    append(text, std::strlen(text));
                }
            }

            void append(const char* text, size_t n) {
                if (text == nullptr) {
                    return;
                }

                if (n > N) { // only the end of an overlong text fits
                    written += static_cast<uint32_t>(n - N);
                    text += n - N;
                    n = N;
                }

                for (size_t i = 0; i < n; ++i) {
                    data[(written + i) % N] = text[i];
                }

                written += static_cast<uint32_t>(n);
                check = magic ^ written ^ kSalt;
            }

            /** The oldest byte still kept */
            uint32_t first() const {
                return written > N ? written - static_cast<uint32_t>(N) : 0;
            }

            /**
             * Copies the log from position pos up to (not including) end, at most max bytes; a position already
             * overwritten starts at the oldest byte kept
             * @return bytes copied, 0 at the end
             */
            size_t read(uint32_t pos, const uint32_t end, char* out, const size_t max) const {
                if (pos < first()) {
                    pos = first();
                }

                const uint32_t stop = end < written ? end : written;
                size_t n = 0;

                for (; pos < stop && n < max; ++pos, ++n) {
                    out[n] = data[pos % N];
                }

                return n;
            }
    };

} // namespace orione
