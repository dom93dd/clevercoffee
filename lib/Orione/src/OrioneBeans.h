/**
 * @file OrioneBeans.h
 *
 * @brief A recipe per bean (Dominik, 08.10.2026: "wenn wir auf ne andere bohne wechseln, dass wir dann die
 *        werte neu anlegen können aber auch wenn wir zurückwechseln, dass wir die alten werte
 *        wiederbekommen"): dose, grind setting, target weight, brew temperature and the learned lag,
 *        under the name typed as the beans. Switching beans keeps the old bean's values and brings back
 *        the new one's, or starts it from the current values. The last kSize beans are kept; the one
 *        not used for the longest time makes room. No Arduino dependencies: tested in
 *        simulator/test/test_orione.
 */

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orione {

    struct BeanRecipe {
            char name[41] = {};          // as typed (brew.beans), up to 40 bytes
            uint16_t doseTenths = 0;     // 0.1 g
            char grind[10] = {};         // as typed
            uint16_t targetTenths = 0;   // target weight, 0.1 g
            int16_t setpointTenths = 0;  // brew temperature, 0.1 °C
            uint16_t lagCs = 0;          // learned lag (BrewLag), 0.01 s
            uint32_t used = 0;           // running number of the last use: the oldest makes room
    };

    class BeanProfiles {
        public:
            static constexpr int kSize = 8;

            /** Same bean: the names match ignoring case (ASCII) and spaces at the ends */
            static bool sameName(const char* a, const char* b) {
                if (a == nullptr || b == nullptr) {
                    return false;
                }

                const char* ae = trimEnd(a, a + std::strlen(a));
                const char* be = trimEnd(b, b + std::strlen(b));
                a = trimStart(a, ae);
                b = trimStart(b, be);

                if (ae - a != be - b || a == ae) {
                    return false; // different length, or no name at all
                }

                for (; a < ae; ++a, ++b) {
                    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
                        return false;
                    }
                }

                return true;
            }

            static bool blank(const char* name) {
                return name == nullptr || trimStart(name, name + std::strlen(name)) == name + std::strlen(name);
            }

            /** @return its place, -1 if there is no such bean */
            int find(const char* name) const {
                for (int i = 0; i < count_; ++i) {
                    if (sameName(beans_[i].name, name)) {
                        return i;
                    }
                }

                return -1;
            }

            /** Keep r under its name (a new bean or new values for a known one); marks it the newest used */
            void put(const BeanRecipe& r) {
                if (blank(r.name)) {
                    return;
                }

                int i = find(r.name);

                if (i < 0) {
                    if (count_ < kSize) {
                        i = count_++;
                    }
                    else { // full: the one not used for the longest time makes room
                        i = 0;

                        for (int k = 1; k < count_; ++k) {
                            if (beans_[k].used < beans_[i].used) {
                                i = k;
                            }
                        }
                    }
                }

                beans_[i] = r;
                beans_[i].name[sizeof(beans_[i].name) - 1] = '\0';
                beans_[i].grind[sizeof(beans_[i].grind) - 1] = '\0';
                beans_[i].used = ++clock_;
            }

            bool remove(const char* name) {
                const int i = find(name);

                if (i < 0) {
                    return false;
                }

                for (int k = i; k < count_ - 1; ++k) {
                    beans_[k] = beans_[k + 1];
                }

                beans_[--count_] = BeanRecipe{};
                return true;
            }

            int count() const {
                return count_;
            }

            const BeanRecipe& at(const int i) const {
                return beans_[i];
            }

            /** Places 0..count-1 ordered by the last use, newest first */
            int byUse(int* order) const {
                for (int i = 0; i < count_; ++i) {
                    order[i] = i;
                }

                for (int i = 1; i < count_; ++i) {
                    const int x = order[i];
                    int j = i - 1;

                    while (j >= 0 && beans_[order[j]].used < beans_[x].used) {
                        order[j + 1] = order[j];
                        --j;
                    }

                    order[j + 1] = x;
                }

                return count_;
            }

            struct Stored {
                    uint8_t version;
                    uint8_t count;
                    uint32_t clock;
                    BeanRecipe beans[kSize];
            };

            Stored stored() const {
                Stored s{};
                s.version = kVersion;
                s.count = static_cast<uint8_t>(count_);
                s.clock = clock_;
                std::memcpy(s.beans, beans_, sizeof(beans_));
                return s;
            }

            bool restore(const void* data, const size_t length) {
                Stored s{};

                if (data == nullptr || length != sizeof(s)) {
                    return false;
                }

                std::memcpy(&s, data, sizeof(s));

                if (s.version != kVersion || s.count > kSize) {
                    return false;
                }

                count_ = s.count;
                clock_ = s.clock;
                std::memcpy(beans_, s.beans, sizeof(beans_));

                for (auto& b : beans_) {
                    b.name[sizeof(b.name) - 1] = '\0';
                    b.grind[sizeof(b.grind) - 1] = '\0';
                }

                return true;
            }

        private:
            static const char* trimStart(const char* p, const char* end) {
                while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
                    ++p;
                }

                return p;
            }

            static const char* trimEnd(const char* start, const char* end) {
                while (end > start && std::isspace(static_cast<unsigned char>(end[-1]))) {
                    --end;
                }

                return end;
            }

            static constexpr uint8_t kVersion = 1;

            BeanRecipe beans_[kSize];
            int count_ = 0;
            uint32_t clock_ = 0;
    };

} // namespace orione
