// Just enough Arduino for src/hardware/IOSwitch.cpp on the desktop: a clock the test sets
#pragma once

#include <cstdint>

#define LOW 0
#define HIGH 1

namespace fake_arduino {
    inline unsigned long nowMs = 0;
}

inline unsigned long millis() {
    return fake_arduino::nowMs;
}
