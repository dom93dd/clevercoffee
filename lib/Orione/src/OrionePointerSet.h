/**
 * @file OrionePointerSet.h
 *
 * @brief A few pointers kept by their owner's callbacks (connect, disconnect), without heap: the live-value clients
 *        of the web server (src/embeddedWebserver.h), whose send limit the library sets anew whenever one comes or
 *        goes. No Arduino dependencies: tested in simulator/test/test_orione.
 */

#pragma once

#include <cstddef>

namespace orione {

    template <typename T, size_t N>
    class PointerSet {
        public:
            /** @return false when full (the pointer is not kept) or nullptr */
            bool add(T* p) {
                if (p == nullptr || contains(p)) {
                    return p != nullptr;
                }

                for (auto& slot : slots_) {
                    if (slot == nullptr) {
                        slot = p;
                        return true;
                    }
                }

                return false;
            }

            void remove(const T* p) {
                for (auto& slot : slots_) {
                    if (slot == p) {
                        slot = nullptr;
                    }
                }
            }

            bool contains(const T* p) const {
                for (const auto* slot : slots_) {
                    if (p != nullptr && slot == p) {
                        return true;
                    }
                }

                return false;
            }

            size_t size() const {
                size_t n = 0;

                for (const auto* slot : slots_) {
                    n += slot != nullptr;
                }

                return n;
            }

            template <typename F>
            void forEach(F f) const {
                for (auto* slot : slots_) {
                    if (slot != nullptr) {
                        f(slot);
                    }
                }
            }

        private:
            T* slots_[N] = {};
    };

} // namespace orione
