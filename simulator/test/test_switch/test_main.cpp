/**
 * src/hardware/IOSwitch.cpp on the desktop, with a pin whose level the test sets: the brew switch must
 * not read OFF at power-on while it is ON. Before the fix it did for the 20 ms debounce, brew() took
 * that as "switched off" and a toggle switch left ON started a brew by itself (Lokus' case, 24.08.2026).
 */

#define CC_ORIONE

#include <unity.h>

#include "../../../src/hardware/IOSwitch.cpp"

namespace {
    int pinLevel = LOW;
}

// the pin: no hardware, the level the test sets
GPIOPin::GPIOPin(const int pinNumber, const Type type) :
    pin(pinNumber), pinType(type) {
}

int GPIOPin::read() const {
    return pinLevel;
}

void setUp() {
    fake_arduino::nowMs = 5000; // setup() is long done when loop() reads the switch first
    pinLevel = LOW;
}

void tearDown() {}

namespace {
    /** every millisecond for `ms`: true if the switch ever read OFF */
    bool readsOffWithin(IOSwitch& s, const unsigned long ms) {
        bool off = false;

        for (unsigned long i = 0; i <= ms; ++i) {
            off = off || !s.isPressed();
            ++fake_arduino::nowMs;
        }

        return off;
    }
}

void test_toggle_on_at_power_on_never_reads_off() {
    pinLevel = HIGH;
    IOSwitch s(34, GPIOPin::IN_HARDWARE, Switch::TOGGLE, Switch::NORMALLY_OPEN, Switch::NORMALLY_OPEN); // as main.cpp
    TEST_ASSERT_FALSE_MESSAGE(readsOffWithin(s, 200), "an ON switch read OFF after power-on");
}

void test_toggle_off_at_power_on_reads_off_then_on_after_debounce() {
    IOSwitch s(34, GPIOPin::IN_HARDWARE, Switch::TOGGLE, Switch::NORMALLY_OPEN, Switch::NORMALLY_OPEN);
    TEST_ASSERT_FALSE(s.isPressed());
    pinLevel = HIGH;
    TEST_ASSERT_FALSE_MESSAGE(s.isPressed(), "debounced: not at once");
    fake_arduino::nowMs += 30;
    TEST_ASSERT_TRUE(s.isPressed());
    pinLevel = LOW;
    s.isPressed();
    fake_arduino::nowMs += 30;
    TEST_ASSERT_FALSE(s.isPressed());
}

void test_normally_closed_switch_starts_from_its_level_too() {
    pinLevel = LOW; // normally closed: LOW = pressed
    IOSwitch s(34, GPIOPin::IN_HARDWARE, Switch::TOGGLE, Switch::NORMALLY_CLOSED, Switch::NORMALLY_CLOSED);
    TEST_ASSERT_FALSE(readsOffWithin(s, 100));
}

void test_momentary_held_at_power_on_is_a_long_press_only_after_the_hold_time() {
    pinLevel = HIGH;
    IOSwitch s(34, GPIOPin::IN_HARDWARE, Switch::MOMENTARY, Switch::NORMALLY_OPEN, Switch::NORMALLY_OPEN);
    s.isPressed();
    TEST_ASSERT_FALSE_MESSAGE(s.longPressDetected(), "not before 500 ms");
    fake_arduino::nowMs += 600;
    s.isPressed();
    TEST_ASSERT_TRUE(s.longPressDetected());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_toggle_on_at_power_on_never_reads_off);
    RUN_TEST(test_toggle_off_at_power_on_reads_off_then_on_after_debounce);
    RUN_TEST(test_normally_closed_switch_starts_from_its_level_too);
    RUN_TEST(test_momentary_held_at_power_on_is_a_long_press_only_after_the_hold_time);
    return UNITY_END();
}
