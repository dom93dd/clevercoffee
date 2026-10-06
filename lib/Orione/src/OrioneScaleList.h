/**
 * @file OrioneScaleList.h
 *
 * @brief The Bluetooth scales a search found, for "Waage suchen" on the web page: one entry per
 *        address (a scale advertises many times a second), the strongest signal first, forgotten when
 *        not seen for a while (switched off, out of range). No Arduino dependencies: tested in
 *        simulator/test/test_orione.
 */

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>

namespace orione {

    struct FoundScale {
            char name[24] = {};
            char address[18] = {}; // "aa:bb:cc:dd:ee:ff", lower case
            int8_t rssi = 0;       // dBm, closer to 0 is stronger
            uint32_t seenMs = 0;
    };

    class ScaleList {
        public:
            static constexpr int kMax = 6;
            static constexpr uint32_t kForgetMs = 15000;

            void clear() {
                count_ = 0;
            }

            /** A supported scale advertised itself */
            void seen(const char* name, const char* address, const int rssi, const uint32_t nowMs) {
                char addr[sizeof(FoundScale::address)];
                normalize(address, addr);

                if (addr[0] == '\0') {
                    return;
                }

                int slot = -1;

                for (int i = 0; i < count_; ++i) {
                    if (std::strcmp(found_[i].address, addr) == 0) {
                        slot = i;
                        break;
                    }
                }

                if (slot < 0) {
                    if (count_ < kMax) {
                        slot = count_++;
                    }
                    else { // full: the one not seen for the longest time makes room
                        slot = 0;

                        for (int i = 1; i < count_; ++i) {
                            if (nowMs - found_[i].seenMs > nowMs - found_[slot].seenMs) {
                                slot = i;
                            }
                        }
                    }

                    std::strcpy(found_[slot].address, addr);
                }

                if (name != nullptr && name[0] != '\0') {
                    std::strncpy(found_[slot].name, name, sizeof(found_[slot].name) - 1);
                    found_[slot].name[sizeof(found_[slot].name) - 1] = '\0';
                }

                found_[slot].rssi = static_cast<int8_t>(rssi < -127 ? -127 : rssi > 0 ? 0 : rssi);
                found_[slot].seenMs = nowMs;
            }

            /** @return how many were written to out: seen within kForgetMs, strongest first */
            int list(FoundScale* out, const int max, const uint32_t nowMs) const {
                int n = 0;

                for (int i = 0; i < count_ && n < max; ++i) {
                    if (nowMs - found_[i].seenMs <= kForgetMs) {
                        out[n++] = found_[i];
                    }
                }

                for (int i = 1; i < n; ++i) { // a handful: insertion sort
                    const FoundScale x = out[i];
                    int j = i - 1;

                    while (j >= 0 && out[j].rssi < x.rssi) {
                        out[j + 1] = out[j];
                        --j;
                    }

                    out[j + 1] = x;
                }

                return n;
            }

            /** "AA:BB:..." or "aa-bb-..." to "aa:bb:cc:dd:ee:ff"; empty if it is not a MAC address */
            static void normalize(const char* in, char* out) {
                out[0] = '\0';

                if (in == nullptr) {
                    return;
                }

                int n = 0;

                for (const char* p = in; *p != '\0'; ++p) {
                    if (std::isxdigit(static_cast<unsigned char>(*p))) {
                        if (n == 17) {
                            out[0] = '\0';
                            return;
                        }

                        if (n % 3 == 2) {
                            out[n++] = ':';
                        }

                        out[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
                    }
                    else if (*p != ':' && *p != '-') {
                        out[0] = '\0';
                        return;
                    }
                }

                out[n == 17 ? n : 0] = '\0';
            }

        private:
            FoundScale found_[kMax];
            int count_ = 0;
    };

} // namespace orione
