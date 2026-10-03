/**
 * @file IOSwitch.h
 *
 * @brief A physical switch connected to a GPIO Pin
 */

#include "IOSwitch.h"
#include "GPIOPin.h"

#include "Logger.h"
IOSwitch::IOSwitch(const int pinNumber, const GPIOPin::Type pinType, const Type switchType, const Mode mode, const uint8_t initialState) :
    Switch(switchType, mode), gpio(pinNumber, pinType), lastState(initialState), currentState(LOW) {
#ifdef CC_ORIONE
    // Start from the pin's actual level. Starting from LOW, a toggle brew switch that was already ON at
    // power-on read OFF for the 20 ms debounce, brew() took that as "switched off" and the brew started
    // by itself: Lokus' case of 24.08.2026, tank pumped empty, thermal fuse blown (orione-full-build.md
    // chapter 10). Now such a switch has to go OFF and ON again first.
    lastState = static_cast<uint8_t>(gpio.read());
    currentState = lastState ^ mode_;

    if (currentState == HIGH) {
        pressStartTime = millis();
    }
#endif
}

bool IOSwitch::isPressed() {
    const uint8_t reading = gpio.read();
    const unsigned long currentMillis = millis();

    if (reading != lastState) {
        lastDebounceTime = currentMillis;
    }

    if (currentMillis - lastDebounceTime > debounceDelay) {
        if ((reading ^ mode_) != currentState) {
            currentState = reading ^ mode_;

            if (currentState == LOW) {
                lastStateChangeTime = currentMillis;
            }
            else {
                pressStartTime = currentMillis;
            }
        }
    }

    lastState = reading;

    if (type_ == MOMENTARY) {
        if (currentState == HIGH && pressStartTime + longPressDuration <= currentMillis) {
            longPressTriggered = true;
        }
        else if (currentState == LOW && lastStateChangeTime == currentMillis) {
            longPressTriggered = false;
        }
    }

    return currentState == HIGH;
}

bool IOSwitch::longPressDetected() {
    if (type_ == TOGGLE) {
        return false;
    }

    if (type_ == MOMENTARY) {
        return longPressTriggered;
    }

    return false;
}
