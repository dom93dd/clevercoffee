/**
 * @file test_main.cpp
 *
 * @brief Logic between firmware and UI: state mapping, brew timer, message splitting,
 *        number format, easing and the display power sequence.
 */

#include "../support/TestSupport.h"

#include <RoundDisplayControl.h>
#include <RoundDisplayFormat.h>
#include <RoundDisplayGuard.h>

#include "../../src/RemoteLink.h"

#include <cstring>

using namespace rd;

void setUp() {
}

void tearDown() {
}

// --- state mapping -------------------------------------------------------------------------

void test_machine_states_map_to_modes() {
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Init), static_cast<int>(modeFromMachineState(firmware::kInit)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Normal), static_cast<int>(modeFromMachineState(firmware::kPidNormal)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Brew), static_cast<int>(modeFromMachineState(firmware::kBrew)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::ManualFlush), static_cast<int>(modeFromMachineState(firmware::kManualFlush)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Steam), static_cast<int>(modeFromMachineState(firmware::kSteam)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::HotWater), static_cast<int>(modeFromMachineState(firmware::kHotWater)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Backflush), static_cast<int>(modeFromMachineState(firmware::kBackflush)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::PidDisabled), static_cast<int>(modeFromMachineState(firmware::kPidDisabled)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::WaterTankEmpty), static_cast<int>(modeFromMachineState(firmware::kWaterTankEmpty)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Standby), static_cast<int>(modeFromMachineState(firmware::kStandby)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::EmergencyStop), static_cast<int>(modeFromMachineState(firmware::kEmergencyStop)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::SensorError), static_cast<int>(modeFromMachineState(firmware::kSensorError)));
}

void test_unknown_machine_state_is_init() {
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Init), static_cast<int>(modeFromMachineState(5)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Init), static_cast<int>(modeFromMachineState(-1)));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Init), static_cast<int>(modeFromMachineState(999)));
}

void test_brew_and_backflush_states_map_to_phases() {
    TEST_ASSERT_EQUAL(static_cast<int>(BrewPhase::Idle), static_cast<int>(brewPhaseFromState(firmware::kBrewIdle)));
    TEST_ASSERT_EQUAL(static_cast<int>(BrewPhase::Preinfusion), static_cast<int>(brewPhaseFromState(firmware::kPreinfusion)));
    TEST_ASSERT_EQUAL(static_cast<int>(BrewPhase::PreinfusionPause), static_cast<int>(brewPhaseFromState(firmware::kPreinfusionPause)));
    TEST_ASSERT_EQUAL(static_cast<int>(BrewPhase::Running), static_cast<int>(brewPhaseFromState(firmware::kBrewRunning)));
    TEST_ASSERT_EQUAL(static_cast<int>(BrewPhase::Finished), static_cast<int>(brewPhaseFromState(firmware::kBrewFinished)));
    TEST_ASSERT_EQUAL(static_cast<int>(BrewPhase::Idle), static_cast<int>(brewPhaseFromState(0)));

    TEST_ASSERT_EQUAL(static_cast<int>(BackflushPhase::Idle), static_cast<int>(backflushPhaseFromState(firmware::kBackflushIdle)));
    TEST_ASSERT_EQUAL(static_cast<int>(BackflushPhase::Filling), static_cast<int>(backflushPhaseFromState(firmware::kBackflushFilling)));
    TEST_ASSERT_EQUAL(static_cast<int>(BackflushPhase::Flushing), static_cast<int>(backflushPhaseFromState(firmware::kBackflushFlushing)));
    TEST_ASSERT_EQUAL(static_cast<int>(BackflushPhase::Ending), static_cast<int>(backflushPhaseFromState(firmware::kBackflushEnding)));
    TEST_ASSERT_EQUAL(static_cast<int>(BackflushPhase::Finished), static_cast<int>(backflushPhaseFromState(firmware::kBackflushFinished)));
    TEST_ASSERT_EQUAL(static_cast<int>(BackflushPhase::Idle), static_cast<int>(backflushPhaseFromState(77)));
}

// --- brew timer ----------------------------------------------------------------------------

void test_brew_timer_hidden_while_idle() {
    BrewTimer t;
    TEST_ASSERT_FALSE(t.update(false, 0, 0, 3));
    TEST_ASSERT_FALSE(t.update(false, 0, 5000, 3));
    TEST_ASSERT_EQUAL_FLOAT(0, t.lastShotSeconds());
}

void test_brew_timer_holds_after_the_shot() {
    BrewTimer t;
    TEST_ASSERT_TRUE(t.update(true, 0.1f, 1000, 3));
    TEST_ASSERT_TRUE(t.update(true, 25.3f, 26300, 3));
    TEST_ASSERT_TRUE(t.update(false, 25.3f, 26400, 3));           // shot over, hold starts
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.3f, t.lastShotSeconds());
    TEST_ASSERT_TRUE(t.update(false, 25.3f, 29400, 3));           // exactly 3 s: still shown
    TEST_ASSERT_FALSE(t.update(false, 25.3f, 29401, 3));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.3f, t.lastShotSeconds()); // kept for the ready screen
}

void test_brew_timer_next_shot_during_hold() {
    BrewTimer t;
    t.update(true, 1, 0, 3);
    t.update(false, 20, 20000, 3);
    TEST_ASSERT_TRUE(t.update(true, 0.1f, 21000, 3)); // new shot while the old one is still shown
    TEST_ASSERT_TRUE(t.update(false, 28, 49000, 3));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 28.0f, t.lastShotSeconds());
}

void test_brew_timer_stays_while_the_switch_is_left_on() {
    BrewTimer t;
    t.update(true, 1, 0, 10, false);
    TEST_ASSERT_TRUE(t.update(false, 26.4f, 26400, 10, true));         // stopped by weight, switch still on
    TEST_ASSERT_TRUE(t.held());
    TEST_ASSERT_TRUE(t.update(false, 26.4f, 60000, 10, true));         // long after the hold time: still shown
    TEST_ASSERT_FALSE(t.remind(60000));
    TEST_ASSERT_TRUE(t.update(false, 26.4f, 86400, 10, true));
    TEST_ASSERT_TRUE_MESSAGE(t.remind(86400), "a minute with the switch on: remind");
    TEST_ASSERT_TRUE(t.update(false, 26.4f, 90000, 10, false));        // switched off: 10 s more, no reminder
    TEST_ASSERT_FALSE(t.remind(90000));
    TEST_ASSERT_FALSE(t.held());
    TEST_ASSERT_TRUE(t.update(false, 26.4f, 100000, 10, false));
    TEST_ASSERT_FALSE(t.update(false, 26.4f, 100001, 10, false));
}

void test_brew_timer_stopped_by_hand_holds_as_before() {
    BrewTimer t;
    t.update(true, 1, 0, 10, true);
    TEST_ASSERT_TRUE(t.update(false, 22.0f, 22000, 10, false)); // stopped by switching off
    TEST_ASSERT_FALSE(t.held());
    TEST_ASSERT_TRUE(t.update(false, 22.0f, 32000, 10, false));
    TEST_ASSERT_FALSE(t.update(false, 22.0f, 32001, 10, false));
    BrewTimer u; // the switch going on again after the hold started (a new shot follows as brewActive)
    u.update(true, 1, 0, 10, false);
    u.update(false, 20.0f, 20000, 10, false);
    TEST_ASSERT_FALSE_MESSAGE(u.update(false, 20.0f, 31000, 10, true), "held only counts from the end of the shot");
}

void test_brew_timer_zero_hold() {
    BrewTimer t;
    t.update(true, 1, 0, 0);
    TEST_ASSERT_TRUE(t.update(false, 5, 5000, 0));
    TEST_ASSERT_FALSE(t.update(false, 5, 5001, 0));
}

// --- messages ------------------------------------------------------------------------------

void test_message_split_into_title_and_lines() {
    MessageText m;
    splitMessage("Verbinde WLAN:\nMeinNetz", m);
    TEST_ASSERT_EQUAL_INT(2, m.count);
    TEST_ASSERT_EQUAL_STRING("VERBINDE WLAN:", m.lines[0]);
    TEST_ASSERT_EQUAL_STRING("MeinNetz", m.lines[1]);

    const Message msg = m.message();
    TEST_ASSERT_EQUAL_STRING("VERBINDE WLAN:", msg.title);
    TEST_ASSERT_EQUAL_STRING("MeinNetz", msg.line1);
    TEST_ASSERT_NULL(msg.line2);
    TEST_ASSERT_NULL(msg.line3);
}

void test_message_title_capitals_with_umlauts_and_accents() {
    MessageText m;
    splitMessage("über ärger öl", m);
    TEST_ASSERT_EQUAL_STRING("ÜBER ÄRGER ÖL", m.lines[0]);
    splitMessage("Dirección IP:", m);
    TEST_ASSERT_EQUAL_STRING("DIRECCIÓN IP:", m.lines[0]);
    splitMessage("Straße", m);          // ß has no single capital: unchanged
    TEST_ASSERT_EQUAL_STRING("STRAßE", m.lines[0]);
    splitMessage("x\nkleine zeile", m); // only the title is converted
    TEST_ASSERT_EQUAL_STRING("kleine zeile", m.lines[1]);
}

void test_message_lines_are_trimmed() {
    MessageText m;
    splitMessage("Kein \nWLAN", m); // as the firmware's "No WiFi" strings
    TEST_ASSERT_EQUAL_STRING("KEIN", m.lines[0]);
    TEST_ASSERT_EQUAL_STRING("WLAN", m.lines[1]);
    splitMessage("  Version  \n  4.0.3 ", m);
    TEST_ASSERT_EQUAL_STRING("VERSION", m.lines[0]);
    TEST_ASSERT_EQUAL_STRING("4.0.3", m.lines[1]);
}

void test_message_keeps_at_most_four_lines() {
    MessageText m;
    splitMessage("a\nb\nc\nd\ne\nf", m);
    TEST_ASSERT_EQUAL_INT(4, m.count);
    TEST_ASSERT_EQUAL_STRING("d", m.lines[3]);
    TEST_ASSERT_EQUAL_STRING("d", m.message().line3);
}

void test_message_long_lines_do_not_cut_utf8() {
    // (kLength - 2) x 'a' + "ü" (2 bytes) is one byte more than fits: the whole ü has to go
    std::string text = "x\n" + std::string(MessageText::kLength - 2, 'a') + "ü";
    MessageText m;
    splitMessage(text.c_str(), m);
    TEST_ASSERT_EQUAL_size_t(MessageText::kLength - 2, strlen(m.lines[1]));

    text = "x\n" + std::string(300, 'b');
    splitMessage(text.c_str(), m);
    TEST_ASSERT_EQUAL_size_t(MessageText::kLength - 1, strlen(m.lines[1]));
}

void test_message_empty_and_null() {
    MessageText m;
    splitMessage(nullptr, m);
    TEST_ASSERT_EQUAL_INT(0, m.count);
    TEST_ASSERT_NULL(m.message().title);

    splitMessage("", m);
    TEST_ASSERT_EQUAL_INT(1, m.count);
    TEST_ASSERT_EQUAL_STRING("", m.lines[0]);
}

void test_message_reuse_clears_old_lines() {
    MessageText m;
    splitMessage("a\nb\nc", m);
    splitMessage("x", m);
    TEST_ASSERT_EQUAL_INT(1, m.count);
    TEST_ASSERT_NULL(m.message().line1);
}

// --- number format -------------------------------------------------------------------------

void test_numbers_german_and_english() {
    char buf[16];
    formatNumber(buf, sizeof(buf), 93.44f, 1, Language::German);
    TEST_ASSERT_EQUAL_STRING("93,4", buf);
    formatNumber(buf, sizeof(buf), 93.44f, 1, Language::English);
    TEST_ASSERT_EQUAL_STRING("93.4", buf);
    formatNumber(buf, sizeof(buf), 25.0f, 0, Language::German);
    TEST_ASSERT_EQUAL_STRING("25", buf);
    formatNumber(buf, sizeof(buf), 99.96f, 1, Language::German);
    TEST_ASSERT_EQUAL_STRING("100,0", buf);
    formatNumber(buf, sizeof(buf), -49.9f, 1, Language::German);
    TEST_ASSERT_EQUAL_STRING("-49,9", buf);
}

void test_numbers_never_negative_zero() {
    char buf[16];
    formatNumber(buf, sizeof(buf), -0.04f, 1, Language::German);
    TEST_ASSERT_EQUAL_STRING("0,0", buf);
    formatNumber(buf, sizeof(buf), -0.4f, 0, Language::English);
    TEST_ASSERT_EQUAL_STRING("0", buf);
}

void test_numbers_fit_small_buffers() {
    char buf[4];
    formatNumber(buf, sizeof(buf), 1234.5f, 1, Language::German);
    TEST_ASSERT_EQUAL_size_t(3, strlen(buf)); // cut, not overflowing
}

void test_easing_and_phase() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, easeOutCubic(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, easeOutCubic(1.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, easeInOutCubic(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, easeInOutCubic(0.5f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, easeInOutCubic(1.0f));

    float last = -1.0f;

    for (float t = 0.0f; t <= 1.0f; t += 0.05f) {
        TEST_ASSERT_TRUE(easeOutCubic(t) >= last);
        last = easeOutCubic(t);
    }

    TEST_ASSERT_EQUAL_FLOAT(0.0f, phase(50, 100, 200));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, phase(150, 100, 200));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, phase(250, 100, 200));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, clampf(5, 0, 2));
}

// --- display power -------------------------------------------------------------------------

void test_power_stays_on_without_request() {
    RoundUi ui;
    PowerSequencer power;
    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::None), static_cast<int>(power.update(false, ui, 0)));
    TEST_ASSERT_FALSE(power.asleep());
    TEST_ASSERT_FALSE(ui.animating(0));
}

void test_power_closes_iris_then_sleeps_then_wakes() {
    RoundUi ui;
    PowerSequencer power;

    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::None), static_cast<int>(power.update(true, ui, 1000)));
    TEST_ASSERT_TRUE(power.closing());
    TEST_ASSERT_TRUE(ui.animating(1000)); // iris closes

    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::None), static_cast<int>(power.update(true, ui, 1400)));
    TEST_ASSERT_FALSE(power.asleep());

    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::Sleep), static_cast<int>(power.update(true, ui, 1800)));
    TEST_ASSERT_TRUE(power.asleep());
    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::None), static_cast<int>(power.update(true, ui, 5000)));

    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::Wake), static_cast<int>(power.update(false, ui, 6000)));
    TEST_ASSERT_FALSE(power.asleep());
    TEST_ASSERT_TRUE(ui.animating(6000)); // iris opens
    TEST_ASSERT_TRUE(ui.needsRedraw(6000));
}

void test_power_request_withdrawn_while_closing() {
    RoundUi ui;
    PowerSequencer power;
    power.update(true, ui, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(PowerSequencer::Action::None), static_cast<int>(power.update(false, ui, 300)));
    TEST_ASSERT_FALSE(power.closing());
    TEST_ASSERT_TRUE(ui.animating(300)); // opens again instead of staying half closed

    for (uint32_t t = 300; t < 3000; t += 100) {
        TEST_ASSERT_NOT_EQUAL(static_cast<int>(PowerSequencer::Action::Sleep), static_cast<int>(power.update(false, ui, t)));
    }

    TEST_ASSERT_FALSE(power.asleep());
}

// Loop guard: heater off while loop() hangs, restart if it does not come back

void test_guard_waits_for_the_first_loop() {
    TEST_ASSERT_TRUE(rd::loopGuardAction(60000, 0, true) == rd::GuardAction::None);
}

void test_guard_holds_the_heater_then_restarts() {
    const uint32_t beat = 100000;
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + 50, beat, true) == rd::GuardAction::None);
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + rd::kGuardHoldMs - 1, beat, true) == rd::GuardAction::None);
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + rd::kGuardHoldMs, beat, true) == rd::GuardAction::HoldHeater);
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + rd::kGuardRestartMs - 1, beat, true) == rd::GuardAction::HoldHeater);
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + rd::kGuardRestartMs, beat, true) == rd::GuardAction::Restart);
}

void test_guard_stays_out_while_the_heater_timer_is_off() {
    // OTA upload: loop() stands in ArduinoOTA.handle() for a minute, the firmware stopped the heater timer itself
    TEST_ASSERT_TRUE(rd::loopGuardAction(100000 + 90000, 100000, false) == rd::GuardAction::None);
}

void test_guard_across_the_millis_overflow() {
    const uint32_t beat = 0xFFFFF000u; // 4096 ms before the overflow
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + 4096 + 1000, beat, true) == rd::GuardAction::None);
    TEST_ASSERT_TRUE(rd::loopGuardAction(beat + rd::kGuardHoldMs, beat, true) == rd::GuardAction::HoldHeater);
}

void test_guard_ignores_a_beat_newer_than_the_watchers_clock() {
    // Watcher reads the time, is preempted on core 0 (web server), loop() beats meanwhile on core 1:
    // the beat is then a few ms "in the future". Found on the real ESP32 on 02.10.2026: it read as
    // a stall of 49 days and restarted the machine.
    const uint32_t now = 100000;
    TEST_ASSERT_TRUE(rd::loopGuardAction(now, now + 1, true) == rd::GuardAction::None);
    TEST_ASSERT_TRUE(rd::loopGuardAction(now, now + 150, true) == rd::GuardAction::None);
    TEST_ASSERT_TRUE(rd::loopGuardAction(0xFFFFFFF0u, 0x10u, true) == rd::GuardAction::None); // across the overflow
}

void test_guard_limits_above_the_intended_pauses() {
    // Places where loop() stops on purpose (see RoundDisplayGuard.h) must not trigger the guard
    constexpr uint32_t bluetoothConnect = 5000 + 2 * 500;                          // connect timeout plus delays and service discovery
    constexpr uint32_t scaleCalibration = 2000 + 2000 + 10000 + 2000;
    TEST_ASSERT_GREATER_THAN_UINT32(bluetoothConnect, rd::kGuardHoldMs);
    TEST_ASSERT_GREATER_THAN_UINT32(scaleCalibration + 4000, rd::kGuardRestartMs); // plus tare and margin
    TEST_ASSERT_LESS_THAN_UINT32(rd::kGuardRestartMs, rd::kGuardHoldMs);
}

// Link simulator -> ESP32 (simulator --display, esp32-bench env remote)

void test_link_model_survives_the_trip() {
    Model m;
    m.mode = Mode::Brew;
    m.language = Language::English;
    m.temperature = 93.25f;
    m.setpoint = 94.5f;
    m.heaterPercent = 37.0f;
    m.readyBand = 0.4f;
    m.emergencyResetTemp = 99.0f;
    m.brewTimerVisible = true;
    m.brewPhase = BrewPhase::Running;
    m.brewTime = 12.3f;
    m.brewTargetTime = 25.0f;
    m.lastBrewTime = 24.8f;
    m.flushTime = 1.5f;
    m.hotWaterTime = 2.5f;
    m.scaleEnabled = true;
    m.scaleFault = false;
    m.bleScale = true;
    m.bleScaleConnected = true;
    m.brewWeight = 18.2f;
    m.brewTargetWeight = 36.0f;
    m.backflushPhase = BackflushPhase::Flushing;
    m.backflushCycle = 3;
    m.backflushCycles = 5;
    m.offlineMode = true;
    m.wifiConnected = false;
    m.wifiBars = 2;
    m.mqttEnabled = true;
    m.mqttConnected = true;

    const auto bytes = sim::link::encodeModel(m);
    Model out;
    TEST_ASSERT_TRUE(sim::link::decodeModel(bytes.data(), bytes.size(), out));
    TEST_ASSERT_EQUAL_MEMORY(sim::link::encodeModel(m).data(), sim::link::encodeModel(out).data(), bytes.size());
    TEST_ASSERT_TRUE(out.mode == Mode::Brew && out.brewPhase == BrewPhase::Running && out.backflushPhase == BackflushPhase::Flushing);
    TEST_ASSERT_EQUAL_FLOAT(18.2f, out.brewWeight);
    TEST_ASSERT_FALSE(sim::link::decodeModel(bytes.data(), bytes.size() - 1, out)); // cut short
}

void test_link_rejects_unknown_states() {
    auto bytes = sim::link::encodeModel(Model());
    bytes[0] = 200; // no such mode
    Model out;
    out.temperature = 42.0f;
    TEST_ASSERT_FALSE(sim::link::decodeModel(bytes.data(), bytes.size(), out));
    TEST_ASSERT_EQUAL_FLOAT(42.0f, out.temperature); // left as it was
}

void test_link_message_with_umlauts() {
    sim::link::TextMessage t{"WLAN-EINRICHTUNG", "Hotspot: Küche", "", "192.168.4.1"};
    const auto bytes = sim::link::encodeText(t);
    sim::link::TextMessage out;
    TEST_ASSERT_TRUE(sim::link::decodeText(bytes.data(), bytes.size(), out));
    TEST_ASSERT_TRUE(out == t);
    TEST_ASSERT_TRUE(sim::link::TextMessage().empty());
}

void test_link_frames_resync_and_check_the_sum() {
    sim::link::Parser parser;
    std::vector<uint8_t> stream = {0x00, 0xA5, 0x13, 0x5A, 0xFF}; // noise, a false start
    const auto good = sim::link::frame(sim::link::Model, sim::link::encodeModel(Model()));
    auto broken = good;
    broken[10] ^= 0x40;
    stream.insert(stream.end(), broken.begin(), broken.end());
    stream.insert(stream.end(), good.begin(), good.end());
    int frames = 0;

    for (const uint8_t b : stream) {
        if (parser.push(b)) {
            ++frames;
            TEST_ASSERT_EQUAL_UINT8(sim::link::Model, parser.type());
            TEST_ASSERT_EQUAL_size_t(good.size() - 6, parser.payload().size());
        }
    }

    TEST_ASSERT_EQUAL_INT(1, frames);        // only the good one
    TEST_ASSERT_EQUAL_UINT32(1, parser.bad); // the broken one counted
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_machine_states_map_to_modes);
    RUN_TEST(test_unknown_machine_state_is_init);
    RUN_TEST(test_brew_and_backflush_states_map_to_phases);
    RUN_TEST(test_brew_timer_hidden_while_idle);
    RUN_TEST(test_brew_timer_holds_after_the_shot);
    RUN_TEST(test_brew_timer_next_shot_during_hold);
    RUN_TEST(test_brew_timer_stays_while_the_switch_is_left_on);
    RUN_TEST(test_brew_timer_stopped_by_hand_holds_as_before);
    RUN_TEST(test_brew_timer_zero_hold);
    RUN_TEST(test_message_split_into_title_and_lines);
    RUN_TEST(test_message_title_capitals_with_umlauts_and_accents);
    RUN_TEST(test_message_lines_are_trimmed);
    RUN_TEST(test_message_keeps_at_most_four_lines);
    RUN_TEST(test_message_long_lines_do_not_cut_utf8);
    RUN_TEST(test_message_empty_and_null);
    RUN_TEST(test_message_reuse_clears_old_lines);
    RUN_TEST(test_numbers_german_and_english);
    RUN_TEST(test_numbers_never_negative_zero);
    RUN_TEST(test_numbers_fit_small_buffers);
    RUN_TEST(test_easing_and_phase);
    RUN_TEST(test_power_stays_on_without_request);
    RUN_TEST(test_power_closes_iris_then_sleeps_then_wakes);
    RUN_TEST(test_power_request_withdrawn_while_closing);
    RUN_TEST(test_guard_waits_for_the_first_loop);
    RUN_TEST(test_guard_holds_the_heater_then_restarts);
    RUN_TEST(test_guard_stays_out_while_the_heater_timer_is_off);
    RUN_TEST(test_guard_across_the_millis_overflow);
    RUN_TEST(test_guard_ignores_a_beat_newer_than_the_watchers_clock);
    RUN_TEST(test_guard_limits_above_the_intended_pauses);
    RUN_TEST(test_link_model_survives_the_trip);
    RUN_TEST(test_link_rejects_unknown_states);
    RUN_TEST(test_link_message_with_umlauts);
    RUN_TEST(test_link_frames_resync_and_check_the_sum);
    return UNITY_END();
}
