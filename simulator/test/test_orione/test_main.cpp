/**
 * @file test_main.cpp
 *
 * @brief Pure logic of the slim Orione build (lib/Orione): the web request gate with its
 *        emergency brake, the settings that are fixed on Dominik's machine and the log of the
 *        last shots.
 */

#include "../support/TestSupport.h"

#include <OrioneFixed.h>
#include <OrioneFlow.h>
#include <OrioneBeans.h>
#include <OrioneCare.h>
#include <OrioneDescale.h>
#include <OrionePointerSet.h>
#include <OrioneHeat.h>
#include <OrioneLogRing.h>
#include <OrioneShots.h>
#include <OrioneWebGate.h>
#include <OrioneWarmupFlush.h>
#include <OrioneScaleList.h>

#include <cstring>
#include <set>
#include <string>

using orione::GateAction;
using orione::gateDecide;
using orione::GateLimits;

void setUp() {
}

void tearDown() {
}

constexpr size_t kKB = 1024;

// --- web request gate ----------------------------------------------------------------------

void test_gate_answers_at_once_when_idle() {
    TEST_ASSERT_TRUE(gateDecide(0, 0, 50 * kKB, 37 * kKB) == GateAction::Start);
}

void test_gate_lets_a_second_request_wait() {
    // one response at a time: two bundle files in parallel took the heap to 11 KB (02.10.2026)
    TEST_ASSERT_TRUE(gateDecide(1, 0, 50 * kKB, 37 * kKB) == GateAction::Wait);
    TEST_ASSERT_TRUE(gateDecide(1, 4, 50 * kKB, 37 * kKB) == GateAction::Wait); // a page load needs 4
}

void test_gate_refuses_when_the_queue_is_full() {
    TEST_ASSERT_TRUE(gateDecide(1, 5, 50 * kKB, 37 * kKB) == GateAction::Busy);
    TEST_ASSERT_TRUE(gateDecide(1, 9, 50 * kKB, 37 * kKB) == GateAction::Busy);
}

void test_gate_brake_on_low_heap_even_when_idle() {
    const GateLimits l;
    TEST_ASSERT_TRUE(gateDecide(0, 0, l.brakeFree - 1, 37 * kKB) == GateAction::Busy);
    TEST_ASSERT_TRUE(gateDecide(0, 0, l.brakeFree, 37 * kKB) == GateAction::Start); // the limit itself is fine
}

void test_gate_brake_on_a_broken_up_heap() {
    // plenty free in total, but no piece large enough: what took the WiFi down on 02.10.2026
    const GateLimits l;
    TEST_ASSERT_TRUE(gateDecide(0, 0, 27 * kKB, l.brakeBlock - 1) == GateAction::Busy);
    TEST_ASSERT_TRUE(gateDecide(0, 0, 27 * kKB, l.brakeBlock) == GateAction::Start);
}

void test_gate_resets_when_the_heap_is_almost_gone() {
    const GateLimits l;
    TEST_ASSERT_TRUE(gateDecide(0, 0, 30 * kKB, l.abortBlock - 1) == GateAction::Reset);
    TEST_ASSERT_TRUE(gateDecide(1, 9, 1 * kKB, 0) == GateAction::Reset); // before every other rule
}

void test_gate_brake_comes_before_the_queue() {
    TEST_ASSERT_TRUE(gateDecide(1, 0, 10 * kKB, 37 * kKB) == GateAction::Busy); // no point in waiting on a starving heap
}

void test_gate_second_parallel_response_only_with_enough_heap() {
    GateLimits two;
    two.maxActive = 2;
    TEST_ASSERT_TRUE(gateDecide(1, 0, 50 * kKB, 37 * kKB, two) == GateAction::Start);
    TEST_ASSERT_TRUE(gateDecide(1, 0, two.secondNeedsFree - 1, 37 * kKB, two) == GateAction::Wait);
    TEST_ASSERT_TRUE(gateDecide(1, 0, 50 * kKB, two.secondNeedsBlock - 1, two) == GateAction::Wait);
    TEST_ASSERT_TRUE(gateDecide(2, 0, 50 * kKB, 37 * kKB, two) == GateAction::Wait);
}

void test_gate_limits_are_consistent() {
    const GateLimits l;
    TEST_ASSERT_LESS_THAN_UINT32(l.brakeBlock, l.abortBlock);
    TEST_ASSERT_LESS_THAN_UINT32(l.secondNeedsBlock, l.brakeBlock);
    TEST_ASSERT_LESS_THAN_UINT32(l.secondNeedsFree, l.brakeFree);
    // measured page load with the scale on: lowest 25 KB free, largest block 15 KB -> the brake must not trip there
    TEST_ASSERT_LESS_THAN_UINT32(25 * kKB, l.brakeFree);
    TEST_ASSERT_LESS_THAN_UINT32(15 * kKB, l.brakeBlock);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4, l.maxWaiting); // one page: index + data requests
}

// --- fixed settings ------------------------------------------------------------------------

static double fixed(const char* path) {
    const double* v = orione::fixedValue(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(v, path);
    return *v;
}

void test_relays_are_fixed_to_high_trigger() {
    // LOW_TRIGGER with the high-level SSR module would switch heater, pump and valve on while "off"
    TEST_ASSERT_EQUAL_INT(static_cast<int>(1), static_cast<int>(fixed("hardware.relays.heater.trigger_type")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(1), static_cast<int>(fixed("hardware.relays.valve.trigger_type")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(1), static_cast<int>(fixed("hardware.relays.pump.trigger_type")));
}

void test_every_relay_setting_is_fixed() {
    int relays = 0;

    for (const auto& f : orione::kFixedSettings) {
        if (std::strstr(f.path, ".relays.") != nullptr) {
            ++relays;
            TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(orione::kHighTrigger), static_cast<int>(f.value), f.path);
        }
    }

    TEST_ASSERT_EQUAL_INT(3, relays);
}

void test_brew_switch_is_a_normally_open_toggle() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(1), static_cast<int>(fixed("hardware.switches.brew.enabled")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(orione::kToggle), static_cast<int>(fixed("hardware.switches.brew.type")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(orione::kNormallyOpen), static_cast<int>(fixed("hardware.switches.brew.mode")));
}

void test_scale_is_bluetooth_and_offline_mode_is_off() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(2), static_cast<int>(fixed("hardware.sensors.scale.type")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(0), static_cast<int>(fixed("system.offline_mode")));
}

void test_ordinary_settings_are_not_fixed() {
    TEST_ASSERT_NULL(orione::fixedValue("brew.setpoint"));
    TEST_ASSERT_NULL(orione::fixedValue("pid.enabled"));
    TEST_ASSERT_NULL(orione::fixedValue("hardware.relays.heater")); // a prefix is not a match
    TEST_ASSERT_NULL(orione::fixedValue("hardware.relays.heater.trigger_type.x"));
    TEST_ASSERT_NULL(orione::fixedValue(""));
    TEST_ASSERT_NULL(orione::fixedValue(nullptr));
}

void test_fixed_paths_are_unique() {
    std::set<std::string> seen;

    for (const auto& f : orione::kFixedSettings) {
        TEST_ASSERT_TRUE_MESSAGE(seen.insert(f.path).second, f.path);
    }
}

// ---------- last shots ----------
// Unity is built without double support here: compare tenths of a gram/second as integers
static int tenths(const float v) {
    return static_cast<int>(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
}

void test_shots_newest_first_and_at_most_five() {
    orione::ShotLog log;

    for (int i = 0; i < 7; ++i) {
        TEST_ASSERT_TRUE(log.record(20.0f + i, 30.0f + i, 1000 + i, i * 60000));
    }

    TEST_ASSERT_EQUAL_INT(5, log.count());
    TEST_ASSERT_EQUAL_INT(260, tenths(log.at(0).seconds)); // the 7th shot
    TEST_ASSERT_EQUAL_INT(220, tenths(log.at(4).seconds)); // the 3rd; the first two dropped out
    TEST_ASSERT_EQUAL_UINT32(1006, log.at(0).when);
}

void test_shots_short_ones_are_left_out() {
    orione::ShotLog log;
    TEST_ASSERT_FALSE(log.record(3.2f, 2.0f, 0, 0));          // flush or a slip of the switch
    TEST_ASSERT_FALSE(log.record(8.0f, -1.0f, 0, 0));         // flush through the group, no scale
    TEST_ASSERT_FALSE(log.record(0.0f / 0.0f, 40.0f, 0, 0));  // NaN must not count either
    TEST_ASSERT_FALSE(log.record(-3.0f, 40.0f, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, log.count());
    TEST_ASSERT_TRUE(log.record(orione::ShotLog::kMinSeconds, -1.0f, 0, 0));
    TEST_ASSERT_TRUE(log.record(8.0f, 36.0f, 0, 0));          // ran through fast: coffee in the cup
    TEST_ASSERT_EQUAL_INT(2, log.count());
    TEST_ASSERT_FALSE(log.record(2.8f, 26.0f, 0, 0));         // the rinse after a shot: its water on the scale
    TEST_ASSERT_TRUE(log.record(orione::ShotLog::kFlushSeconds, 20.0f, 0, 0));
}

void test_shots_with_a_scale_need_coffee_in_the_cup() {
    orione::ShotLog log; // Dominik, 08.10.2026: a flush with the scale on is not a shot, however long
    TEST_ASSERT_FALSE(log.record(25.0f, 0.0f, 0, 0));
    TEST_ASSERT_FALSE(log.record(40.0f, 4.9f, 0, 0));
    TEST_ASSERT_TRUE(log.record(25.0f, orione::ShotLog::kMinGrams, 0, 0));
    TEST_ASSERT_TRUE(log.record(25.0f, -1.0f, 0, 0)); // no scale connected: the time decides
    TEST_ASSERT_EQUAL_INT(2, log.count());
    TEST_ASSERT_EQUAL_UINT16(2, log.sinceBackflush());
}

void test_shots_delete_keeps_the_others_and_their_curves() {
    orione::ShotLog log;

    for (int i = 0; i < 5; ++i) {
        log.record(20.0f + i, 30.0f + i, 0, 0); // seq 0..4, newest (24 s) first
    }

    TEST_ASSERT_FALSE(log.remove(5));
    TEST_ASSERT_FALSE(log.remove(-1));
    TEST_ASSERT_TRUE(log.remove(2)); // the 22 s shot
    TEST_ASSERT_EQUAL_INT(4, log.count());
    TEST_ASSERT_EQUAL_FLOAT(24.0f, log.at(0).seconds);
    TEST_ASSERT_EQUAL_FLOAT(23.0f, log.at(1).seconds);
    TEST_ASSERT_EQUAL_FLOAT(21.0f, log.at(2).seconds);
    TEST_ASSERT_EQUAL_UINT16(1, log.at(2).seq); // its curve stays where it was
    TEST_ASSERT_EQUAL_UINT16(4, log.sinceBackflush());

    // the next shots must not take the curve slot of a shot still listed (seq 5 would be slot 0 = the 20 s shot)
    for (int i = 0; i < 3; ++i) {
        log.record(30.0f + i, 40.0f, 0, 0);
        std::set<int> slots;

        for (int k = 0; k < log.count(); ++k) {
            slots.insert(log.at(k).seq % orione::ShotLog::kSize);
        }

        TEST_ASSERT_EQUAL_INT(log.count(), static_cast<int>(slots.size()));
    }

    TEST_ASSERT_EQUAL_FLOAT(32.0f, log.at(0).seconds);
    TEST_ASSERT_EQUAL_FLOAT(23.0f, log.at(4).seconds);
}

void test_shots_delete_the_newest_while_its_drops_are_counted() {
    orione::ShotLog log;
    log.record(25.0f, 30.0f, 0, 0);
    log.record(26.0f, 34.0f, 0, 10000);
    TEST_ASSERT_TRUE(log.settling());
    TEST_ASSERT_TRUE(log.remove(0));
    TEST_ASSERT_FALSE(log.settling());
    TEST_ASSERT_FALSE(log.settle(11000, 99.0f)); // the drops do not land on the shot before it
    TEST_ASSERT_EQUAL_FLOAT(30.0f, log.at(0).grams);
    TEST_ASSERT_TRUE(log.remove(0));
    TEST_ASSERT_EQUAL_INT(0, log.count());
    TEST_ASSERT_FALSE(log.remove(0));
    log.record(25.0f, 30.0f, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(0, log.at(0).seq);
}

void test_shots_delete_counts_since_backflush_only_for_newer_ones() {
    orione::ShotLog log;
    log.record(25.0f, 36.0f, 0, 0);
    log.record(25.0f, 36.0f, 0, 0);
    log.backflushDone();
    log.record(25.0f, 36.0f, 0, 0); // the only one since
    TEST_ASSERT_TRUE(log.remove(2)); // before the backflush
    TEST_ASSERT_EQUAL_UINT16(1, log.sinceBackflush());
    TEST_ASSERT_TRUE(log.remove(0));
    TEST_ASSERT_EQUAL_UINT16(0, log.sinceBackflush());
}

void test_brew_weight_ignores_a_cup_put_on_during_the_shot() {
    orione::BrewWeight w;
    w.start(0.0f);
    float g = 0.0f;

    for (int i = 1; i <= 20; ++i) {
        g = w.update(0.4f * static_cast<float>(i)); // 2 g/s at 5 readings a second
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, w.update(8.0f + 152.0f)); // a 152 g cup put down
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.4f, w.update(160.4f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.4f, w.update(8.4f));          // lifted again
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.8f, w.update(8.8f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.8f, w.update(0.0f / 0.0f));   // no reading
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w.base());                 // on and off again: back where it started
}

void test_brew_weight_late_tare_is_not_a_negative_shot() {
    orione::BrewWeight w;
    w.start(212.0f); // the cup, the tare still on its way
    w.update(212.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, w.update(0.3f)); // tared
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.3f, w.update(2.3f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.8f, w.update(11.8f - 2.0f)); // steps under 10 g are coffee
}

void test_shots_drops_after_the_stop_are_counted() {
    orione::ShotLog log;
    log.record(25.0f, 34.2f, 0, 10000);
    TEST_ASSERT_FALSE(log.settle(11000, 35.0f));
    TEST_ASSERT_FALSE(log.settle(12000, 35.6f));
    TEST_ASSERT_FALSE(log.settle(13000, 0.3f)); // cup lifted: keeps the highest reading
    TEST_ASSERT_EQUAL_INT(356, tenths(log.at(0).grams));
    TEST_ASSERT_TRUE(log.settle(14000, 36.0f));  // settled: save now, this reading comes too late
    TEST_ASSERT_FALSE(log.settle(15000, 36.0f)); // only once
    TEST_ASSERT_EQUAL_INT(356, tenths(log.at(0).grams));
}

void test_shots_drops_counted_until_the_weight_stops_rising() {
    orione::ShotLog log; // water without a puck ran on for 15-20 s at the machine (08.10.2026)
    log.record(10.6f, 89.0f, 0, 0);

    for (uint32_t t = 500; t <= 9000; t += 500) { // 1 g/s until 9 s
        TEST_ASSERT_FALSE_MESSAGE(log.settle(t, 89.0f + static_cast<float>(t) / 1000.0f), "still rising: keeps counting past 4 s");
    }

    TEST_ASSERT_FALSE(log.settle(10000, 98.0f)); // quiet from 9 s on
    TEST_ASSERT_FALSE(log.settle(10900, 98.05f));
    TEST_ASSERT_TRUE(log.settle(11000, 98.1f)); // 2 s without a real rise
    TEST_ASSERT_EQUAL_INT(981, tenths(log.at(0).grams));
}

void test_shots_drops_counted_at_most_fifteen_seconds() {
    orione::ShotLog log;
    log.record(10.0f, 80.0f, 0, 1000);
    uint32_t t = 1000;
    float g = 80.0f;

    while (!log.settle(t += 500, g += 0.5f)) {
        TEST_ASSERT_TRUE(t <= 1000 + orione::ShotLog::kSettleMaxMs);
    }

    TEST_ASSERT_EQUAL_UINT32(1000 + orione::ShotLog::kSettleMaxMs, t);
}

void test_shots_without_scale_settle_after_four_seconds() {
    orione::ShotLog log;
    log.record(25.0f, -1.0f, 0, 0);
    TEST_ASSERT_FALSE(log.settle(3999, -1.0f));
    TEST_ASSERT_TRUE(log.settle(4000, -1.0f));
}

void test_heat_boost_while_the_pump_runs() {
    using B = orione::BrewHeatBoost;
    TEST_ASSERT_EQUAL_FLOAT(600.0f, static_cast<float>(B::apply(0.0, 1000.0, 60.0, true, 92.0, 93.0)));    // at least 60 %
    TEST_ASSERT_EQUAL_FLOAT(900.0f, static_cast<float>(B::apply(900.0, 1000.0, 60.0, true, 88.0, 93.0)));  // the controller may give more
    TEST_ASSERT_EQUAL_FLOAT(0.0f, static_cast<float>(B::apply(0.0, 1000.0, 60.0, false, 88.0, 93.0)));     // pump off: the controller alone
    TEST_ASSERT_EQUAL_FLOAT(0.0f, static_cast<float>(B::apply(0.0, 1000.0, 60.0, true, 93.5, 93.0)));      // over the setpoint: no floor
    TEST_ASSERT_EQUAL_FLOAT(200.0f, static_cast<float>(B::apply(200.0, 1000.0, 0.0, true, 80.0, 93.0)));   // switched off
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, static_cast<float>(B::apply(0.0, 1000.0, 150.0, true, 80.0, 93.0)));  // at most the whole window
    TEST_ASSERT_EQUAL_FLOAT(0.0f, static_cast<float>(B::apply(0.0, 1000.0, 60.0, true, NAN, 93.0)));       // no reading: never heat blind
}

void test_steam_seen_from_the_temperature() {
    orione::SteamWatch w;
    using W = orione::SteamWatch;
    TEST_ASSERT_EQUAL(W::kNone, w.update(99.0, 93.0)); // the controller's overshoot is no steam
    TEST_ASSERT_EQUAL(W::kSteam, w.update(106.0, 93.0));
    TEST_ASSERT_EQUAL(W::kSteam, w.update(104.0, 93.0)); // hysteresis
    TEST_ASSERT_EQUAL(W::kSteam, w.update(125.0, 93.0));
    TEST_ASSERT_EQUAL(W::kSteam, w.update(-49.9, 93.0)); // the sensor's error value changes nothing
    TEST_ASSERT_EQUAL(W::kCooling, w.update(102.9, 93.0)); // S2 off: cooling down
    TEST_ASSERT_EQUAL(W::kCooling, w.update(97.0, 93.0));
    TEST_ASSERT_EQUAL(W::kNone, w.update(96.0, 93.0)); // near the setpoint: espresso again
    TEST_ASSERT_EQUAL(W::kSteam, w.update(110.0, 93.0)); // steam again
    TEST_ASSERT_EQUAL(W::kCooling, w.update(100.0, 93.0));
    TEST_ASSERT_EQUAL(W::kSteam, w.update(105.0, 93.0)); // switched on again while cooling
}

void test_shots_without_scale_keep_no_weight() {
    orione::ShotLog log;
    log.record(25.0f, -1.0f, 0, 0);
    log.settle(1000, -1.0f);
    TEST_ASSERT_TRUE(log.at(0).grams < 0);
}

void test_shots_settle_across_millis_wrap() {
    orione::ShotLog log;
    log.record(25.0f, 30.0f, 0, 0xFFFFFC00u);           // 1024 ms before millis() wraps
    TEST_ASSERT_FALSE(log.settle(0x00000100u, 31.0f)); // 1280 ms later
    TEST_ASSERT_TRUE(log.settle(0x00000C00u, 31.0f));  // 4096 ms later
}

void test_shots_survive_save_and_restore() {
    orione::ShotLog log;
    log.record(24.5f, 36.1f, 1727890000u, 0);
    log.record(26.0f, -1.0f, 0, 0);
    const auto saved = log.stored();

    orione::ShotLog back;
    TEST_ASSERT_TRUE(back.restore(&saved, sizeof(saved)));
    TEST_ASSERT_EQUAL_INT(2, back.count());
    TEST_ASSERT_EQUAL_INT(260, tenths(back.at(0).seconds));
    TEST_ASSERT_EQUAL_INT(361, tenths(back.at(1).grams));
    TEST_ASSERT_EQUAL_UINT32(1727890000u, back.at(1).when);
}

void test_shots_reject_foreign_data() {
    orione::ShotLog log;
    auto saved = log.stored();
    TEST_ASSERT_FALSE(log.restore(&saved, sizeof(saved) - 1)); // other size (older layout)
    TEST_ASSERT_FALSE(log.restore(nullptr, sizeof(saved)));
    saved.version = 99;
    TEST_ASSERT_FALSE(log.restore(&saved, sizeof(saved)));
    saved = log.stored();
    saved.count = 9;
    TEST_ASSERT_FALSE(log.restore(&saved, sizeof(saved)));
    TEST_ASSERT_EQUAL_INT(0, log.count());
}

void test_shots_numbered_for_their_curves() {
    orione::ShotLog log;
    log.record(25.0f, 36.0f, 0, 0);
    log.record(26.0f, 37.0f, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(1, log.at(0).seq);
    TEST_ASSERT_EQUAL_UINT16(0, log.at(1).seq);
    const auto saved = log.stored();
    orione::ShotLog back;
    back.restore(&saved, sizeof(saved));
    back.record(24.0f, 35.0f, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(2, back.at(0).seq); // goes on after a restart

    for (int i = 0; i < 6; ++i) {
        back.record(24.0f, 35.0f, 0, 0);
    }

    std::set<int> slots; // the five kept shots have five different curve slots

    for (int i = 0; i < back.count(); ++i) {
        slots.insert(back.at(i).seq % orione::ShotLog::kSize);
    }

    TEST_ASSERT_EQUAL_INT(5, static_cast<int>(slots.size()));
}

void test_curve_point_every_half_second() {
    orione::ShotCurve c;
    c.begin(1000);

    for (uint32_t ms = 1000; ms <= 3000; ms += 50) { // loop() runs far more often than points are due
        c.sample(ms, (ms - 1000) / 100.0f, 93.46f);
    }

    TEST_ASSERT_EQUAL_INT(5, c.count()); // 0, 0.5, 1, 1.5, 2 s
    TEST_ASSERT_EQUAL_INT(500, c.intervalMs());
    TEST_ASSERT_EQUAL_INT(0, c.at(0).grams);
    TEST_ASSERT_EQUAL_INT(50, c.at(1).grams); // 5.0 g in tenths
    TEST_ASSERT_EQUAL_INT(935, c.at(4).celsius);
}

void test_curve_long_shot_keeps_its_length() {
    orione::ShotCurve c;
    c.begin(0);

    for (uint32_t ms = 0; ms <= 80000; ms += 10) {
        c.sample(ms, ms / 1000.0f, 93.0f);
    }

    // 80 s: 160 points at 0.5 s do not fit, 80 at 1 s do
    TEST_ASSERT_EQUAL_INT(1000, c.intervalMs());
    TEST_ASSERT_EQUAL_INT(81, c.count());

    for (int i = 0; i < c.count(); ++i) { // each point still sits at i * interval
        TEST_ASSERT_EQUAL_INT(i * 10, c.at(i).grams);
    }
}

void test_curve_marks_the_stop_and_keeps_the_drops() {
    orione::ShotCurve c;
    c.begin(0);

    for (uint32_t ms = 0; ms <= 25000; ms += 100) {
        c.sample(ms, ms / 700.0f, 93.0f);
    }

    c.stopped();

    for (uint32_t ms = 25100; ms <= 29000; ms += 100) {
        c.sample(ms, 36.0f, 92.0f);
    }

    c.end();
    TEST_ASSERT_EQUAL_INT(51, c.stop()); // first point after 25 s
    TEST_ASSERT_EQUAL_INT(59, c.count());
    c.sample(40000, 40.0f, 92.0f); // ended: nothing more
    TEST_ASSERT_EQUAL_INT(59, c.count());
}

void test_curve_stop_survives_halving() {
    orione::ShotCurve c;
    c.begin(0);

    for (uint32_t ms = 0; ms <= 30000; ms += 100) {
        c.sample(ms, 1.0f, 93.0f);
    }

    c.stopped(); // at point 61 (30.5 s)

    for (uint32_t ms = 30100; ms <= 60000; ms += 100) {
        c.sample(ms, 1.0f, 93.0f);
    }

    TEST_ASSERT_EQUAL_INT(1000, c.intervalMs());
    TEST_ASSERT_EQUAL_INT(31, c.stop()); // 31 s, the first point after 30.5 s
}

void test_curve_without_scale_or_sensor() {
    orione::ShotCurve c;
    c.begin(0);
    c.sample(0, -1.0f, 0.0f / 0.0f);
    TEST_ASSERT_EQUAL_INT(orione::ShotCurve::kNone, c.at(0).grams);
    TEST_ASSERT_EQUAL_INT(orione::ShotCurve::kNone, c.at(0).celsius);
}

void test_curve_survives_save_and_restore() {
    orione::ShotCurve c;
    c.begin(0);

    for (uint32_t ms = 0; ms <= 2000; ms += 500) {
        c.sample(ms, ms / 100.0f, 93.0f);
    }

    c.stopped();
    c.end();
    auto saved = c.stored();
    orione::ShotCurve back;
    TEST_ASSERT_TRUE(back.restore(&saved, sizeof(saved)));
    TEST_ASSERT_EQUAL_INT(5, back.count());
    TEST_ASSERT_EQUAL_INT(5, back.stop());
    TEST_ASSERT_EQUAL_INT(200, back.at(4).grams);
    saved.count = orione::ShotCurve::kMaxPoints + 1;
    TEST_ASSERT_FALSE(back.restore(&saved, sizeof(saved)));
    saved = c.stored();
    saved.intervalMs = 0;
    TEST_ASSERT_FALSE(back.restore(&saved, sizeof(saved)));
    TEST_ASSERT_FALSE(back.restore(&saved, sizeof(saved) - 2));
}

void test_shots_keep_the_beans_of_each_shot() {
    orione::ShotLog log; // Dominik, 08.10.2026: older shots keep the beans they were made with
    log.record(25.0f, 36.0f, 0, 0);
    log.noteRecipe(18.0f, "12", "Gardelli Huila, hell");
    log.record(26.0f, 37.0f, 0, 0);
    log.noteRecipe(18.0f, "11", "Röstwerk Dunkel");
    TEST_ASSERT_EQUAL_STRING("Röstwerk Dunkel", log.at(0).beans);
    TEST_ASSERT_EQUAL_STRING("Gardelli Huila, hell", log.at(1).beans);
    log.noteRecipe(18.0f, "11", nullptr);
    TEST_ASSERT_EQUAL_STRING("", log.at(0).beans);
    // 40 bytes fit; longer is cut, never inside a letter: "ö" is 2 bytes, the 41st byte would split it
    const char* longBeans = "Espresso Bohne aus Brasilien, Hochland ölig"; // 39 ASCII bytes, then "ö" on bytes 40 and 41
    log.noteRecipe(18.0f, "11", longBeans);
    TEST_ASSERT_EQUAL_INT(39, static_cast<int>(std::strlen(log.at(0).beans)));
    TEST_ASSERT_EQUAL_INT(0, std::strncmp(longBeans, log.at(0).beans, 39));
    log.noteRecipe(18.0f, "11", "1234567890123456789012345678901234567890"); // exactly 40
    TEST_ASSERT_EQUAL_STRING("1234567890123456789012345678901234567890", log.at(0).beans);
    const auto saved = log.stored();
    orione::ShotLog back;
    TEST_ASSERT_TRUE(back.restore(&saved, sizeof(saved)));
    TEST_ASSERT_EQUAL_STRING("Gardelli Huila, hell", back.at(1).beans);
}

void test_shots_keep_recipe_and_taste() {
    orione::ShotLog log;
    log.noteRecipe(18.0f, "12"); // no shot yet: nothing to note
    log.record(25.0f, 36.0f, 0, 0);
    log.noteRecipe(18.04f, "2.5 (fein)");
    TEST_ASSERT_EQUAL_UINT16(180, log.at(0).doseTenths);
    TEST_ASSERT_EQUAL_STRING("2.5 (fein", log.at(0).grind); // cut to the field, always terminated
    log.noteRecipe(0.0f / 0.0f, nullptr);
    TEST_ASSERT_EQUAL_UINT16(0, log.at(0).doseTenths);
    TEST_ASSERT_EQUAL_STRING("", log.at(0).grind);
    TEST_ASSERT_TRUE(log.rate(0, orione::kSour));
    TEST_ASSERT_FALSE(log.rate(1, orione::kGood)); // no second shot
    TEST_ASSERT_FALSE(log.rate(0, 9));
    TEST_ASSERT_EQUAL_UINT8(orione::kSour, log.at(0).taste);
    log.record(26.0f, 37.0f, 0, 0); // the rating stays with its shot
    TEST_ASSERT_EQUAL_UINT8(orione::kNotRated, log.at(0).taste);
    TEST_ASSERT_EQUAL_UINT8(orione::kSour, log.at(1).taste);
}

void test_shots_count_since_backflush() {
    orione::ShotLog log;

    for (int i = 0; i < 7; ++i) {
        log.record(25.0f, 36.0f, 0, 0);
    }

    TEST_ASSERT_EQUAL_UINT16(7, log.sinceBackflush()); // counts beyond the five kept
    log.backflushDone();
    log.record(25.0f, 36.0f, 0, 0);
    log.record(3.0f, 1.0f, 0, 0); // not a shot
    TEST_ASSERT_EQUAL_UINT16(1, log.sinceBackflush());
    const auto saved = log.stored();
    orione::ShotLog back;
    TEST_ASSERT_TRUE(back.restore(&saved, sizeof(saved)));
    TEST_ASSERT_EQUAL_UINT16(1, back.sinceBackflush());
}

// ---------- flow, heap watch ----------
void test_flow_is_the_slope_over_the_last_second() {
    orione::FlowMeter f;
    f.start(1000);

    for (uint32_t ms = 1000; ms <= 9000; ms += 100) { // scale at 10 Hz: nothing for 5 s, then 2 g/s
        const float t = (ms - 1000) / 1000.0f;
        f.add(ms, t < 5.0f ? 0.0f : (t - 5.0f) * 2.0f);
    }

    TEST_ASSERT_EQUAL_INT(200, static_cast<int>(f.flow() * 100.0f + 0.5f));
    TEST_ASSERT_EQUAL_INT(53, static_cast<int>(f.firstDropSeconds() * 10.0f + 0.5f)); // 0.5 g at 5.25 s, seen at 5.3 s
}

void test_flow_needs_some_time_and_ignores_a_lifted_cup() {
    orione::FlowMeter f;
    f.add(0, 5.0f); // not started: nothing
    TEST_ASSERT_TRUE(f.firstDropSeconds() < 0);
    f.start(0);
    f.add(0, 0.0f);
    f.add(200, 1.0f);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.flow() * 100.0f)); // 0.2 s: too short to tell
    f.add(400, 2.0f);
    TEST_ASSERT_EQUAL_INT(500, static_cast<int>(f.flow() * 100.0f + 0.5f)); // 5 g/s over 0.4 s
    f.add(500, 0.0f / 0.0f);                                                // NaN ignored
    f.add(600, -0.3f);                                                      // cup lifted
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.flow() * 100.0f));
}

void test_flow_skips_calls_between_scale_reports() {
    orione::FlowMeter f;
    f.start(0);

    for (uint32_t ms = 0; ms <= 3000; ms += 1) { // loop() every millisecond, the weight changes every 100 ms
        f.add(ms, static_cast<float>(ms / 100) * 0.2f);
    }

    TEST_ASSERT_EQUAL_INT(200, static_cast<int>(f.flow() * 100.0f + 0.5f));
}

void test_heap_watch_restarts_after_a_minute_low_but_not_during_a_shot() {
    orione::HeapWatch w;
    TEST_ASSERT_FALSE(w.update(1000, true, false));
    TEST_ASSERT_FALSE(w.update(60999, true, false)); // 59.999 s
    TEST_ASSERT_FALSE(w.update(61000, true, true));  // a shot runs: wait
    TEST_ASSERT_TRUE(w.update(70000, true, false));  // after it
    TEST_ASSERT_FALSE(w.update(70100, false, false)); // recovered: counts from the start again
    TEST_ASSERT_FALSE(w.update(100000, true, false));
    TEST_ASSERT_FALSE(w.update(159999, true, false));
    TEST_ASSERT_TRUE(w.update(160000, true, false));
}

void test_heap_watch_uses_the_restart_limits_and_survives_millis_wrap() {
    TEST_ASSERT_TRUE(orione::HeapWatch::low(30 * 1024, 4 * 1024));  // fragmented
    TEST_ASSERT_TRUE(orione::HeapWatch::low(9 * 1024, 8 * 1024));   // too little
    TEST_ASSERT_FALSE(orione::HeapWatch::low(30 * 1024, 16 * 1024));
    // under the brake but working (scale connected, app open, 08.10.2026: free 15.8 KB, block 7 KB): no restart
    TEST_ASSERT_FALSE(orione::HeapWatch::low(15764, 7156));
    TEST_ASSERT_TRUE(orione::gateDecide(0, 0, 15764, 7156) != orione::GateAction::Start); // the brake still refuses pages
    orione::HeapWatch w;
    TEST_ASSERT_FALSE(w.update(0xFFFFF000u, true, false));
    TEST_ASSERT_TRUE(w.update(0xFFFFF000u + 60000u, true, false)); // wraps past 0
}

void test_shots_keep_start_temperature_and_first_drops() {
    orione::ShotLog log;
    log.record(25.0f, 36.0f, 0, 0);
    log.noteFacts(93.46f, 6.24f);
    TEST_ASSERT_EQUAL_INT(935, log.at(0).startTenths);
    TEST_ASSERT_EQUAL_UINT16(62, log.at(0).firstDropTenths);
    log.noteFacts(0.0f / 0.0f, -1.0f); // no sensor value, no scale
    TEST_ASSERT_EQUAL_INT(0, log.at(0).startTenths);
    TEST_ASSERT_EQUAL_UINT16(0, log.at(0).firstDropTenths);
}

void test_curve_keeps_the_flow() {
    orione::ShotCurve c;
    c.begin(0);
    c.sample(0, 0.0f, 93.0f, 0.0f);
    c.sample(500, 1.0f, 93.0f, 2.14f);
    c.sample(1000, -1.0f, 93.0f, -1.0f); // no scale
    TEST_ASSERT_EQUAL_INT(214, c.at(1).flow);
    TEST_ASSERT_EQUAL_INT(orione::ShotCurve::kNone, c.at(2).flow);
}

// ---------- warm-up flush ----------

namespace {
    using Flush = orione::WarmupFlush;
    constexpr Flush::Inputs kAllGood{true, true, true, true, false};

    /** runs the flush in 100 ms steps; returns the times (s, one decimal) at which valve and pump switched */
    std::string switches(Flush& f, const uint32_t fromMs, const uint32_t toMs, const double celsius, const double setpoint, const Flush::Inputs& in, bool on = false) {
        std::string out;

        for (uint32_t t = fromMs; t <= toMs; t += 100) {
            if (const bool now = f.update(t, celsius, setpoint, in); now != on) {
                on = now;
                char buf[24];
                snprintf(buf, sizeof(buf), "%s%.1f%s", out.empty() ? "" : " ", t / 1000.0, on ? "+" : "-");
                out += buf;
            }
        }

        return out;
    }
}

void test_flush_after_a_cold_start_three_pulses_once_settled() {
    Flush f;
    TEST_ASSERT_FALSE(f.update(1000, 22.0, 94.0, kAllGood)); // cold start
    TEST_ASSERT_EQUAL(Flush::kWaiting, f.phase());
    TEST_ASSERT_EQUAL_STRING("", switches(f, 1100, 200000, 80.0, 94.0, kAllGood).c_str()); // still heating: nothing
    // settled from 200 s: first pulse 120 s later, 3 s each, 20 s pauses
    TEST_ASSERT_EQUAL_STRING("320.0+ 323.0- 343.0+ 346.0- 366.0+ 369.0-", switches(f, 200000, 500000, 94.3, 94.0, kAllGood).c_str());
    TEST_ASSERT_EQUAL(Flush::kDone, f.phase());
    TEST_ASSERT_EQUAL_STRING("", switches(f, 500000, 900000, 94.0, 94.0, kAllGood).c_str()); // only once
}

void test_flush_not_after_a_warm_restart() {
    Flush f; // the ESP restarted with a hot block (update, safety net, brownout)
    TEST_ASSERT_EQUAL_STRING("", switches(f, 1000, 600000, 93.8, 94.0, kAllGood).c_str());
    TEST_ASSERT_EQUAL(Flush::kNone, f.phase());
}

void test_flush_ignores_readings_before_the_sensor_has_one() {
    Flush f;
    f.update(500, 0.0, 94.0, kAllGood);   // temperature not read yet
    f.update(600, -49.9, 94.0, kAllGood); // TSIC error value
    TEST_ASSERT_EQUAL(Flush::kNone, f.phase());
    f.update(700, 91.0, 94.0, kAllGood); // first real reading: warm
    TEST_ASSERT_EQUAL(Flush::kNone, f.phase());
}

void test_flush_never_without_a_water_level_sensor() {
    Flush f;
    Flush::Inputs in = kAllGood;
    in.sensor = false;
    f.update(1000, 22.0, 94.0, in);
    TEST_ASSERT_EQUAL_STRING("", switches(f, 1100, 600000, 94.0, 94.0, in).c_str());
    f.requestStart();
    TEST_ASSERT_FALSE_MESSAGE(f.update(600100, 94.0, 94.0, in), "not by hand either");
    TEST_ASSERT_FALSE(f.running());
}

void test_flush_also_with_a_steady_offset_but_not_while_it_swings() {
    Flush f; // the bench's simulated block: settles at 90.2 for a setpoint of 93
    f.update(0, 22.0, 93.0, kAllGood);
    TEST_ASSERT_EQUAL_STRING("120.1+ 123.1-", switches(f, 100, 123500, 90.2, 93.0, kAllGood).substr(0, 13).c_str());
    Flush g; // swinging by 2 K: not settled, no water
    g.update(0, 22.0, 93.0, kAllGood);
    bool any = false;

    for (uint32_t t = 100; t < 600000; t += 100) {
        any = any || g.update(t, (t / 10000) % 2 ? 94.0 : 92.0, 93.0, kAllGood);
    }

    TEST_ASSERT_FALSE(any);
    Flush h; // swinging by 1 K: settled
    h.update(0, 22.0, 93.0, kAllGood);
    TEST_ASSERT_EQUAL_STRING("120.1+", switches(h, 100, 120500, 93.0, 93.0, kAllGood).substr(0, 6).c_str());
}

void test_flush_settling_restarts_when_the_temperature_leaves_the_band() {
    Flush f;
    f.update(0, 22.0, 94.0, kAllGood);
    switches(f, 100, 100000, 94.0, 94.0, kAllGood);  // 100 s settled
    switches(f, 100100, 101000, 96.0, 94.0, kAllGood); // overshoot: start again
    TEST_ASSERT_EQUAL_STRING("221.1+ 224.1-", switches(f, 101100, 225000, 94.0, 94.0, kAllGood).c_str());
}

void test_flush_cut_short_by_an_empty_tank_and_not_resumed() {
    Flush f;
    f.update(0, 22.0, 94.0, kAllGood);
    switches(f, 100, 121500, 94.0, 94.0, kAllGood); // first pulse running
    TEST_ASSERT_TRUE(f.running());
    Flush::Inputs empty = kAllGood;
    empty.tankOk = false;
    TEST_ASSERT_FALSE(f.update(121600, 94.0, 94.0, empty));
    TEST_ASSERT_EQUAL(Flush::kDone, f.phase());
    TEST_ASSERT_EQUAL_STRING("", switches(f, 121700, 400000, 94.0, 94.0, kAllGood).c_str());
}

void test_flush_user_taking_over_cancels_the_pending_one() {
    Flush f;
    f.update(0, 22.0, 94.0, kAllGood);
    Flush::Inputs brewing = kAllGood;
    brewing.userActive = true;
    f.update(60000, 90.0, 94.0, brewing); // brew switch on before the flush
    TEST_ASSERT_EQUAL(Flush::kDone, f.phase());
    TEST_ASSERT_EQUAL_STRING("", switches(f, 60100, 400000, 94.0, 94.0, kAllGood).c_str());
}

void test_flush_automatic_off_but_by_hand_any_time() {
    Flush f;
    Flush::Inputs manual = kAllGood;
    manual.automatic = false;
    f.update(0, 22.0, 94.0, manual);
    TEST_ASSERT_EQUAL_STRING("", switches(f, 100, 300000, 94.0, 94.0, manual).c_str());
    f.requestStart();
    TEST_ASSERT_TRUE(f.update(300100, 94.0, 94.0, manual));
    TEST_ASSERT_EQUAL(1, f.pulse());
    TEST_ASSERT_EQUAL_STRING("303.1- 323.1+ 326.1- 346.1+ 349.1-", switches(f, 300200, 400000, 94.0, 94.0, manual, true).c_str());
    f.requestStart(); // and again
    TEST_ASSERT_TRUE(f.update(400100, 94.0, 94.0, manual));
}

void test_flush_stopped_by_hand() {
    Flush f;
    f.update(0, 93.0, 94.0, kAllGood);
    f.requestStart();
    TEST_ASSERT_TRUE(f.update(100, 93.0, 94.0, kAllGood));
    TEST_ASSERT_EQUAL(100u, f.elapsedMs(200));
    f.requestStop();
    TEST_ASSERT_FALSE(f.update(200, 93.0, 94.0, kAllGood));
    TEST_ASSERT_EQUAL(Flush::kDone, f.phase());
    TEST_ASSERT_EQUAL(0, f.pulse());
    f.requestStop(); // a stop with nothing running is forgotten, it does not cut the next start short
    f.update(300, 93.0, 94.0, kAllGood);
    f.requestStart();
    TEST_ASSERT_TRUE(f.update(400, 93.0, 94.0, kAllGood));
    TEST_ASSERT_TRUE(f.update(500, 93.0, 94.0, kAllGood));
}

void test_flush_by_hand_needs_water_and_a_free_machine() {
    Flush f;
    f.update(0, 93.0, 94.0, kAllGood);
    Flush::Inputs busy = kAllGood;
    busy.ready = false; // e.g. backflush or standby
    f.requestStart();
    TEST_ASSERT_FALSE(f.update(100, 93.0, 94.0, busy));
    TEST_ASSERT_FALSE_MESSAGE(f.update(200, 93.0, 94.0, kAllGood), "a refused start is not kept for later");
    Flush::Inputs empty = kAllGood;
    empty.tankOk = false;
    f.requestStart();
    TEST_ASSERT_FALSE(f.update(300, 93.0, 94.0, empty));
}

// ---------- found scales ----------

void test_scales_one_entry_per_address_strongest_first() {
    orione::ScaleList l;
    l.seen("BOOKOO_SC U 1234", "C8:2E:18:AA:01:02", -70, 1000);
    l.seen("BOOKOO_SC 5678", "c8:2e:18:aa:03:04", -55, 1100);
    l.seen("BOOKOO_SC U 1234", "c8:2e:18:aa:01:02", -50, 1200); // same scale again, now closer
    orione::FoundScale out[orione::ScaleList::kMax];
    TEST_ASSERT_EQUAL(2, l.list(out, orione::ScaleList::kMax, 1300));
    TEST_ASSERT_EQUAL_STRING("BOOKOO_SC U 1234", out[0].name);
    TEST_ASSERT_EQUAL_STRING("c8:2e:18:aa:01:02", out[0].address);
    TEST_ASSERT_EQUAL(-50, out[0].rssi);
    TEST_ASSERT_EQUAL_STRING("BOOKOO_SC 5678", out[1].name);
}

void test_scales_forgotten_when_not_seen_and_room_made_when_full() {
    orione::ScaleList l;
    l.seen("A", "00:00:00:00:00:01", -60, 0);
    orione::FoundScale out[orione::ScaleList::kMax];
    TEST_ASSERT_EQUAL(1, l.list(out, orione::ScaleList::kMax, orione::ScaleList::kForgetMs));
    TEST_ASSERT_EQUAL_MESSAGE(0, l.list(out, orione::ScaleList::kMax, orione::ScaleList::kForgetMs + 1), "switched off");
    for (int i = 0; i < orione::ScaleList::kMax; ++i) {
        char addr[18];
        snprintf(addr, sizeof(addr), "00:00:00:00:01:%02x", i);
        l.seen("B", addr, -60, 20000 + i);
    }
    l.seen("NEW", "00:00:00:00:02:00", -40, 30000); // full: replaces the one seen longest ago
    TEST_ASSERT_EQUAL(orione::ScaleList::kMax, l.list(out, orione::ScaleList::kMax, 30000));
    TEST_ASSERT_EQUAL_STRING("NEW", out[0].name);
    l.clear();
    TEST_ASSERT_EQUAL(0, l.list(out, orione::ScaleList::kMax, 30000));
}

void test_scale_addresses_normalized_or_refused() {
    char out[18];
    orione::ScaleList::normalize("C8-2E-18-AA-01-02", out);
    TEST_ASSERT_EQUAL_STRING("c8:2e:18:aa:01:02", out);
    orione::ScaleList::normalize("c8:2e:18:aa:01", out);
    TEST_ASSERT_EQUAL_STRING("", out);
    orione::ScaleList::normalize("c8:2e:18:aa:01:02:03", out);
    TEST_ASSERT_EQUAL_STRING("", out);
    orione::ScaleList::normalize("c8:2e:18:aa:01:0g", out);
    TEST_ASSERT_EQUAL_STRING("", out);
    orione::ScaleList::normalize("<script>", out);
    TEST_ASSERT_EQUAL_STRING("", out);
    orione::ScaleList l; // a name too long for the list is cut, an empty one does not overwrite
    l.seen("A_VERY_LONG_SCALE_NAME_THAT_GOES_ON", "00:00:00:00:00:01", -60, 0);
    l.seen("", "00:00:00:00:00:01", -61, 1);
    orione::FoundScale f[1];
    l.list(f, 1, 2);
    TEST_ASSERT_EQUAL_STRING("A_VERY_LONG_SCALE_NAME_", f[0].name);
}

// ---------- brew by weight: lead and drops ----------

void test_lag_learns_half_the_error_in_seconds_of_flow() {
    using L = orione::BrewLag;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.8f, L::leadGrams(1.0f, 1.8f)); // a second of 1.8 g/s
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, L::leadGrams(1.0f, 3.0f)); // a faster shot stops earlier by itself
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, L::leadGrams(1.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, L::leadGrams(1.0f, NAN));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, L::leadGrams(3.0f, 9.0f)); // a bump on the scale: not 27 g early
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2f, L::learn(1.0f, 36.0f, 36.8f, 2.0f));  // 0.8 g over at 2 g/s: 0.2 s earlier
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.875f, L::learn(1.0f, 36.0f, 35.5f, 2.0f)); // 0.5 g short: 0.125 s later
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, L::learn(1.0f, 36.0f, 39.5f, 2.0f));  // 3.5 g over: poured by hand, ignored
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, L::learn(1.0f, 36.0f, 37.0f, 0.2f));  // hardly ran at the stop: says nothing
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, L::learn(0.1f, 36.0f, 34.0f, 1.0f));  // never below 0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, L::learn(2.9f, 36.0f, 38.5f, 1.0f));  // never over 3 s
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, L::learn(1.0f, 0.0f, 36.0f, 2.0f));   // no target: nothing to learn
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, L::learn(1.0f, 36.0f, NAN, 2.0f));
    // the machine's shot of 08.10.2026: 1.7 g lead at 1.8 g/s left 0.2 g over the target
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, L::learn(1.7f / 1.8f, 33.0f, 33.2f, 1.8f));

    // a steady 1.3 s of drops: settles within a few shots, at 2 g/s and then still right at 3 g/s
    float lag = L::kStart;
    for (int shot = 0; shot < 6; ++shot) {
        const float flow = shot % 2 ? 2.0f : 3.0f;
        const float atStop = 36.0f - L::leadGrams(lag, flow);
        lag = L::learn(lag, 36.0f, atStop + 1.3f * flow, flow);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.3f, lag);
}

void test_shots_from_format_6_are_taken_over() {
    orione::ShotLog::StoredV6 old{};
    old.version = 6;
    old.count = 2;
    old.sinceBackflush = 8;
    old.shots[0].seconds = 32.4f;
    old.shots[0].grams = 33.2f;
    old.shots[0].when = 1791401900u;
    old.shots[0].seq = 7;
    old.shots[0].doseTenths = 165;
    std::strcpy(old.shots[0].grind, "21");
    old.shots[0].taste = orione::kGood;
    old.shots[0].startTenths = 929;
    old.shots[0].firstDropTenths = 59;
    old.shots[0].targetTenths = 330;
    old.shots[0].stopTenths = 314;
    old.shots[0].leadTenths = 17;
    std::strcpy(old.shots[0].beans, "Ettli Don Pedro");
    old.shots[1].seconds = 30.1f;
    old.shots[1].seq = 6;

    TEST_ASSERT_TRUE(orione::ShotLog::readable(sizeof(old)));
    TEST_ASSERT_TRUE(orione::ShotLog::readable(sizeof(orione::ShotLog::Stored)));
    TEST_ASSERT_FALSE(orione::ShotLog::readable(sizeof(old) - 4));
    orione::ShotLog log;
    TEST_ASSERT_TRUE(log.restore(&old, sizeof(old)));
    TEST_ASSERT_EQUAL_INT(2, log.count());
    TEST_ASSERT_EQUAL_UINT16(8, log.sinceBackflush());
    const auto& x = log.at(0);
    TEST_ASSERT_EQUAL_FLOAT(32.4f, x.seconds);
    TEST_ASSERT_EQUAL_FLOAT(33.2f, x.grams);
    TEST_ASSERT_EQUAL_UINT32(1791401900u, x.when);
    TEST_ASSERT_EQUAL_UINT16(7, x.seq);
    TEST_ASSERT_EQUAL_UINT16(165, x.doseTenths);
    TEST_ASSERT_EQUAL_STRING("21", x.grind);
    TEST_ASSERT_EQUAL_UINT8(orione::kGood, x.taste);
    TEST_ASSERT_EQUAL_INT16(929, x.startTenths);
    TEST_ASSERT_EQUAL_UINT16(59, x.firstDropTenths);
    TEST_ASSERT_EQUAL_UINT16(330, x.targetTenths);
    TEST_ASSERT_EQUAL_UINT16(314, x.stopTenths);
    TEST_ASSERT_EQUAL_UINT8(17, x.leadTenths);
    TEST_ASSERT_EQUAL_STRING("Ettli Don Pedro", x.beans);
    TEST_ASSERT_EQUAL_UINT16(0, x.lagCs); // not known then
    TEST_ASSERT_EQUAL_UINT8(0, x.piTenths);
    TEST_ASSERT_EQUAL_FLOAT(30.1f, log.at(1).seconds);
    const auto now = log.stored(); // saved again as this format
    orione::ShotLog back;
    TEST_ASSERT_TRUE(back.restore(&now, sizeof(now)));
    TEST_ASSERT_EQUAL_STRING("Ettli Don Pedro", back.at(0).beans);
    old.count = 9; // nonsense stays out
    orione::ShotLog bad;
    TEST_ASSERT_FALSE(bad.restore(&old, sizeof(old)));
}

void test_beans_keep_a_recipe_each() {
    orione::BeanProfiles b;
    orione::BeanRecipe r;
    std::strcpy(r.name, "Ettli Don Pedro");
    r.doseTenths = 165;
    std::strcpy(r.grind, "21");
    r.targetTenths = 330;
    r.setpointTenths = 930;
    r.lagCs = 100;
    b.put(r);
    TEST_ASSERT_EQUAL_INT(0, b.find("  ettli don pedro ")); // case and spaces at the ends do not matter
    TEST_ASSERT_EQUAL_INT(-1, b.find("Ettli"));
    TEST_ASSERT_EQUAL_INT(-1, b.find(""));
    orione::BeanRecipe other = r;
    std::strcpy(other.name, "Röstwerk Hell");
    other.setpointTenths = 950;
    b.put(other);
    r.targetTenths = 360; // the same bean again: new values, no second entry
    b.put(r);
    TEST_ASSERT_EQUAL_INT(2, b.count());
    TEST_ASSERT_EQUAL_UINT16(360, b.at(b.find("Ettli Don Pedro")).targetTenths);
    int order[orione::BeanProfiles::kSize];
    TEST_ASSERT_EQUAL_INT(2, b.byUse(order));
    TEST_ASSERT_EQUAL_STRING("Ettli Don Pedro", b.at(order[0]).name); // used last
    orione::BeanRecipe blank;
    std::strcpy(blank.name, "   ");
    b.put(blank);
    TEST_ASSERT_EQUAL_INT(2, b.count());
    TEST_ASSERT_FALSE(b.remove("RÖSTWERK HELL")); // ASCII case only: "Ö" is not "ö"
    TEST_ASSERT_TRUE(b.remove("röstwerk hell"));
    TEST_ASSERT_EQUAL_INT(1, b.count());
    const auto saved = b.stored();
    orione::BeanProfiles back;
    TEST_ASSERT_TRUE(back.restore(&saved, sizeof(saved)));
    TEST_ASSERT_EQUAL_UINT16(100, back.at(0).lagCs);
    TEST_ASSERT_FALSE(back.restore(&saved, sizeof(saved) - 1));
}

void test_beans_the_longest_unused_makes_room() {
    orione::BeanProfiles b;
    for (int i = 0; i < orione::BeanProfiles::kSize; ++i) {
        orione::BeanRecipe r;
        std::snprintf(r.name, sizeof(r.name), "Bohne %d", i);
        b.put(r);
    }
    orione::BeanRecipe again; // bean 0 used again: bean 1 is now the oldest
    std::strcpy(again.name, "Bohne 0");
    b.put(again);
    orione::BeanRecipe fresh;
    std::strcpy(fresh.name, "Neue Bohne");
    b.put(fresh);
    TEST_ASSERT_EQUAL_INT(orione::BeanProfiles::kSize, b.count());
    TEST_ASSERT_TRUE(b.find("Bohne 0") >= 0);
    TEST_ASSERT_EQUAL_INT(-1, b.find("Bohne 1"));
    TEST_ASSERT_TRUE(b.find("Neue Bohne") >= 0);
}

void test_shot_keeps_target_weight_at_stop_and_lead() {
    orione::ShotLog log;
    log.record(26.0f, 34.6f, 0, 0);
    log.noteWeights(36.0f, 34.6f, 1.5f, 0.83f, 1.8f);
    log.settle(1000, 36.4f); // drops
    TEST_ASSERT_TRUE(log.settle(orione::ShotLog::kSettleMs, 36.4f));
    const auto& x = log.at(0);
    TEST_ASSERT_EQUAL(360, x.targetTenths);
    TEST_ASSERT_EQUAL(346, x.stopTenths);
    TEST_ASSERT_EQUAL(15, x.leadTenths);
    TEST_ASSERT_EQUAL(83, x.lagCs);
    TEST_ASSERT_EQUAL(180, x.flowStopCs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.4f, x.grams);
    log.record(25.0f, -1.0f, 0, 10000); // by time, no scale
    log.noteWeights(0.0f, -1.0f, 1.5f);
    TEST_ASSERT_EQUAL(0, log.at(0).targetTenths);
    TEST_ASSERT_EQUAL(0, log.at(0).stopTenths);
    orione::ShotLog back; // saved and restored with the new fields
    const auto stored = log.stored();
    TEST_ASSERT_TRUE(back.restore(&stored, sizeof(stored)));
    TEST_ASSERT_EQUAL(346, back.at(1).stopTenths);
    TEST_ASSERT_EQUAL(83, back.at(1).lagCs);
}

void test_log_ring_keeps_the_newest_lines() {
    orione::LogRing<16> r;
    std::memset(&r, 0x5A, sizeof(r)); // what RTC memory holds after power-up
    TEST_ASSERT_FALSE(r.valid());
    r.begin(true); // a restart was reported, but the header does not check out: start clean
    TEST_ASSERT_TRUE(r.valid());
    TEST_ASSERT_EQUAL_UINT32(0, r.written);
    r.append("hello ");
    r.append("world\n");
    char out[32] = {};
    TEST_ASSERT_EQUAL(12, r.read(0, r.written, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("hello world\n", out);
    r.append("0123456789"); // 22 written: the oldest 6 are gone
    TEST_ASSERT_EQUAL_UINT32(6, r.first());
    std::memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(16, r.read(0, r.written, out, sizeof(out))); // from before the oldest: starts there
    TEST_ASSERT_EQUAL_STRING("world\n0123456789", out);
    std::memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(4, r.read(11, r.written, out, 4)); // in pieces, as a chunked answer asks
    TEST_ASSERT_EQUAL_STRING("\n012", out);
    std::memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(2, r.read(14, 16, out, sizeof(out))); // up to the end the request started with
    TEST_ASSERT_EQUAL_STRING("23", out);
    TEST_ASSERT_EQUAL(0, r.read(22, r.written, out, sizeof(out)));
    // a restart keeps it, a power-up clears it
    const uint32_t before = r.written;
    r.begin(true);
    TEST_ASSERT_EQUAL_UINT32(before, r.written);
    r.begin(false);
    TEST_ASSERT_EQUAL_UINT32(0, r.written);
    // longer than the whole ring: its end
    r.append("abcdefghijklmnopqrstuvwxyz");
    std::memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(16, r.read(0, r.written, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("klmnopqrstuvwxyz", out);
    r.written ^= 1; // a torn header: not trusted after a restart
    TEST_ASSERT_FALSE(r.valid());
}


void test_schedule_fires_once_at_its_minute_on_its_days() {
    orione::WeekPlan weekdays; // Monday to Friday 6:30-22:00
    TEST_ASSERT_TRUE(orione::WeekPlan::parse("06:30-22:00;06:30-22:00;06:30-22:00;06:30-22:00;06:30-22:00;;", weekdays));
    orione::Schedule s;
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, s.update(true, weekdays, 0, 390)); // just started at 6:30: no catching up
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, s.update(true, weekdays, 0, 391));
    orione::Schedule t;
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 1, 389)); // Tuesday 6:29
    TEST_ASSERT_EQUAL(orione::Schedule::kOn, t.update(true, weekdays, 1, 390));
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 1, 390)); // same minute again: once
    TEST_ASSERT_EQUAL(orione::Schedule::kOff, t.update(true, weekdays, 1, 1320)); // 22:00
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 5, 389)); // Saturday: nothing set
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 5, 390));
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(false, weekdays, 1, 389));
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(false, weekdays, 1, 390)); // switched off
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 1, 410)); // jumped over 6:30: no
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 2, 389));
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, t.update(true, weekdays, 2, 390, true)); // holiday: paused
}

void test_schedule_two_windows_per_day_and_over_midnight() {
    orione::WeekPlan p;
    TEST_ASSERT_TRUE(orione::WeekPlan::parse("06:30-09:00,17:00-20:00;;;;;22:00-01:30;08:00", p));
    TEST_ASSERT_EQUAL(390, p.days[0][0].on);
    TEST_ASSERT_EQUAL(1200, p.days[0][1].off);
    TEST_ASSERT_EQUAL(orione::WeekPlan::kNone, p.days[1][0].on);
    TEST_ASSERT_EQUAL(orione::WeekPlan::kNone, p.days[6][0].off); // Sunday 8:00, no off time
    orione::Schedule s;
    s.update(true, p, 0, 0);
    TEST_ASSERT_EQUAL(orione::Schedule::kOn, s.update(true, p, 0, 390));
    TEST_ASSERT_EQUAL(orione::Schedule::kOff, s.update(true, p, 0, 540));
    TEST_ASSERT_EQUAL(orione::Schedule::kOn, s.update(true, p, 0, 1020)); // the second window
    TEST_ASSERT_EQUAL(orione::Schedule::kOff, s.update(true, p, 0, 1200));
    TEST_ASSERT_EQUAL(orione::Schedule::kOn, s.update(true, p, 5, 1320));  // Saturday 22:00
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, s.update(true, p, 5, 90));  // not Saturday's 1:30
    TEST_ASSERT_EQUAL(orione::Schedule::kOff, s.update(true, p, 6, 90));   // Sunday 1:30: off, over midnight
    TEST_ASSERT_EQUAL(orione::Schedule::kOn, s.update(true, p, 6, 480));
    TEST_ASSERT_EQUAL(orione::Schedule::kNone, s.update(true, p, 0, 90)); // Monday 1:30: Sunday's window has no off time
}

void test_week_plan_text_round_trip_and_the_old_settings() {
    orione::WeekPlan p;
    char text[orione::WeekPlan::kMaxText];
    const char* in = "06:30-09:00,17:00-20:00;06:30-09:00;;;;08:00-11:00;08:00";
    TEST_ASSERT_TRUE(orione::WeekPlan::parse(in, p));
    p.format(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING(in, text);
    TEST_ASSERT_TRUE(orione::WeekPlan::parse("6:05-7:00;;;;;;", p)); // one-digit hours are fine
    p.format(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("06:05-07:00;;;;;;", text);
    TEST_ASSERT_TRUE(orione::WeekPlan::parse("07:00-07:00;;;;;;", p)); // no length: on only
    TEST_ASSERT_EQUAL(orione::WeekPlan::kNone, p.days[0][0].off);
    TEST_ASSERT_FALSE(orione::WeekPlan::parse("", p));                  // not a plan: empty
    TEST_ASSERT_FALSE(orione::WeekPlan::parse("?", p));
    TEST_ASSERT_FALSE(orione::WeekPlan::parse("25:00;;;;;;", p));
    TEST_ASSERT_FALSE(orione::WeekPlan::parse("07:00,08:00,09:00;;;;;;", p)); // three windows
    TEST_ASSERT_FALSE(orione::WeekPlan::parse(";;;;;;;", p));                 // eight days
    TEST_ASSERT_FALSE(orione::WeekPlan::parse("07:00;;;;;", p));              // six days
    TEST_ASSERT_EQUAL(orione::WeekPlan::kNone, p.days[0][0].on);              // left empty
    // Dominik's schedule before 09.10.2026: Monday to Saturday at 7:00, no off time
    orione::WeekPlan::fromDays(63, 420, 1440).format(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("07:00;07:00;07:00;07:00;07:00;07:00;", text);
    orione::WeekPlan::fromDays(0b1000001, 390, 1320).format(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("06:30-22:00;;;;;;06:30-22:00", text);
    char longest[orione::WeekPlan::kMaxText];
    std::strcpy(longest, "00:00-23:59,12:00-13:00");
    for (int d = 1; d < 7; ++d) {
        std::strcat(longest, ";00:00-23:59,12:00-13:00");
    }
    TEST_ASSERT_TRUE(orione::WeekPlan::parse(longest, p));
    p.format(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING(longest, text);
}

void test_standby_wakes_only_on_a_switch_turned_on_during_it() {
    orione::StandbyWake w;
    // the switch was left on when the standby began: no wake, not even after minutes
    TEST_ASSERT_FALSE(w.update(1000, true));
    TEST_ASSERT_FALSE(w.update(200000, true));
    // off, then on: wake once
    TEST_ASSERT_FALSE(w.update(200010, false));
    TEST_ASSERT_TRUE(w.update(200020, true));
    TEST_ASSERT_FALSE(w.update(200030, true));
    // a later standby (a gap in the calls) with the switch still on: no wake
    TEST_ASSERT_FALSE(w.update(900000, true));
    // a later standby with the switch off: the first turn on wakes
    orione::StandbyWake v;
    TEST_ASSERT_FALSE(v.update(5000, false));
    TEST_ASSERT_TRUE(v.update(5010, true));
    // the clock running over (millis() after 49 days) is no gap
    orione::StandbyWake o;
    TEST_ASSERT_FALSE(o.update(0xFFFFFF00u, false));
    TEST_ASSERT_TRUE(o.update(0x00000010u, true));
}

void test_pointer_set_keeps_a_few_without_heap() {
    int a = 1, b = 2, c = 3;
    orione::PointerSet<int, 2> set;
    TEST_ASSERT_TRUE(set.add(&a));
    TEST_ASSERT_TRUE(set.add(&a)); // twice: kept once
    TEST_ASSERT_EQUAL(1, set.size());
    TEST_ASSERT_TRUE(set.add(&b));
    TEST_ASSERT_FALSE(set.add(&c)); // full
    TEST_ASSERT_FALSE(set.contains(&c));
    TEST_ASSERT_FALSE(set.add(nullptr));
    set.remove(&a);
    TEST_ASSERT_FALSE(set.contains(&a));
    TEST_ASSERT_TRUE(set.add(&c)); // the freed slot
    int sum = 0;
    set.forEach([&](int* p) { sum += *p; });
    TEST_ASSERT_EQUAL(5, sum);
    set.remove(&c);
    set.remove(&c); // not there any more: nothing happens
    TEST_ASSERT_EQUAL(1, set.size());
}

void test_stats_keep_the_backflush_date_and_take_format_1_over() {
    orione::MachineStats a;
    a.shot(18.0f, 54, 20000);
    a.backflushed(1791500000);
    a.backflushed(0); // no clock: the date stays
    TEST_ASSERT_EQUAL_UINT32(1791500000, a.backflushAt());
    const auto s = a.stored();
    orione::MachineStats b;
    TEST_ASSERT_TRUE(b.restore(&s, sizeof(s)));
    TEST_ASSERT_EQUAL_UINT32(1791500000, b.backflushAt());
    // format 1 (before the date): the counters come along, no date
    orione::MachineStats::StoredV1 v1{1, 245, 44100, 12300, 1791000000, 20000, 2857, 3, 12};
    orione::MachineStats c;
    TEST_ASSERT_TRUE(orione::MachineStats::readable(sizeof(v1)));
    TEST_ASSERT_TRUE(c.restore(&v1, sizeof(v1)));
    TEST_ASSERT_EQUAL_UINT32(245, c.total());
    TEST_ASSERT_EQUAL_UINT32(12300, c.waterMl());
    TEST_ASSERT_EQUAL_UINT32(1791000000, c.descaledAt());
    TEST_ASSERT_EQUAL_UINT16(3, c.today(20000));
    TEST_ASSERT_EQUAL_UINT32(0, c.backflushAt());
    v1.version = 7; // unknown
    TEST_ASSERT_FALSE(c.restore(&v1, sizeof(v1)));
    TEST_ASSERT_FALSE(orione::MachineStats::readable(5));
}

void test_rinse_after_a_shot_skips_the_preinfusion_for_two_minutes() {
    using R = orione::RinseAfterShot;
    TEST_ASSERT_TRUE(R::expected(true, 100000, 90000));
    TEST_ASSERT_TRUE(R::expected(true, 90000 + R::kWindowMs, 90000));
    TEST_ASSERT_FALSE(R::expected(true, 90001 + R::kWindowMs, 90000)); // later: the next shot is prepared
    TEST_ASSERT_FALSE(R::expected(false, 100000, 90000));               // rinsed already
    TEST_ASSERT_TRUE(R::expected(true, 5000, 0xFFFFF000u));             // millis() ran over
}

void test_standby_keeps_warm_then_goes_off() {
    orione::StandbyWarm w;
    TEST_ASSERT_FALSE(w.update(0, false, 70.0f, 2.0f));
    TEST_ASSERT_TRUE(w.update(1000, true, 70.0f, 2.0f)); // standby from the timer or the button: warm
    TEST_ASSERT_EQUAL(120, w.minutesLeft(1000, 2.0f));
    TEST_ASSERT_EQUAL(1, w.minutesLeft(1000 + 7200000 - 30000, 2.0f));
    TEST_ASSERT_TRUE(w.update(1000 + 7199999, true, 70.0f, 2.0f));
    TEST_ASSERT_FALSE(w.takeEnded());
    TEST_ASSERT_FALSE(w.update(1000 + 7200000, true, 70.0f, 2.0f)); // two hours: off for good
    TEST_ASSERT_TRUE(w.takeEnded());
    TEST_ASSERT_FALSE(w.takeEnded());
    TEST_ASSERT_EQUAL(-1, w.minutesLeft(1000 + 7200000, 2.0f));
    TEST_ASSERT_FALSE(w.update(9000000, true, 70.0f, 2.0f)); // stays off in the same standby
    TEST_ASSERT_FALSE(w.update(9001000, false, 70.0f, 2.0f)); // woken
    TEST_ASSERT_TRUE(w.update(9002000, true, 70.0f, 2.0f));   // the next standby warms again
}

void test_standby_warm_off_by_setting_and_by_the_schedule() {
    orione::StandbyWarm w;
    TEST_ASSERT_FALSE(w.update(1000, true, 0.0f, 2.0f)); // 0 °C: heater off in standby, as before
    w.update(2000, false, 0.0f, 2.0f);
    w.coldNext(); // the schedule's off time puts it into standby
    TEST_ASSERT_FALSE(w.update(3000, true, 70.0f, 2.0f));
    TEST_ASSERT_FALSE(w.update(4000, true, 70.0f, 2.0f));
    w.update(5000, false, 70.0f, 2.0f);
    TEST_ASSERT_TRUE(w.update(6000, true, 70.0f, 2.0f)); // the next one from the timer warms
    w.coldNext();                                         // the schedule's off time during a warm standby
    TEST_ASSERT_FALSE(w.active());
    TEST_ASSERT_FALSE(w.update(7000, true, 70.0f, 2.0f));
    w.update(8000, false, 70.0f, 2.0f);
    TEST_ASSERT_TRUE(w.update(9000, true, 70.0f, 2.0f)); // coldNext() only held for that standby
    TEST_ASSERT_FALSE(w.update(10000, true, 0.0f, 2.0f)); // set to 0 meanwhile: off at once
    TEST_ASSERT_TRUE(w.update(0xFFFFF000u, false, 70.0f, 2.0f) == false);
    TEST_ASSERT_TRUE(w.update(0xFFFFF800u, true, 70.0f, 0.5f));
    TEST_ASSERT_TRUE(w.update(1000, true, 70.0f, 0.5f)); // millis() ran over: still counting
    TEST_ASSERT_FALSE(w.update(0xFFFFF800u + 1800000u, true, 70.0f, 0.5f));
}

void test_descale_program_runs_cold_rounds_then_rinses_twice() {
    using D = orione::DescaleProgram;
    D d;
    const D::Inputs full{true, 93.0f}, cold{true, 55.0f}, empty{false, 40.0f};
    TEST_ASSERT_FALSE(d.running());
    d.start(0);
    TEST_ASSERT_EQUAL(D::kCooling, d.phase());
    TEST_ASSERT_FALSE(d.update(1000, full)); // still hot: waits, no pumping
    TEST_ASSERT_EQUAL(D::kCooling, d.phase());
    uint32_t t = 600000;
    TEST_ASSERT_TRUE(d.update(t, cold)); // cooled down: round 1 pumps
    TEST_ASSERT_EQUAL(D::kDescale, d.phase());
    TEST_ASSERT_EQUAL(1, d.round());
    TEST_ASSERT_EQUAL(10, d.secondsLeft(t));
    TEST_ASSERT_TRUE(d.update(t + D::kPumpMs - 1, cold));
    TEST_ASSERT_FALSE(d.update(t + D::kPumpMs, cold)); // soaking
    TEST_ASSERT_EQUAL(300, d.secondsLeft(t + D::kPumpMs));
    t += D::kPumpMs;
    for (int r = 2; r <= D::kRounds; ++r) {
        TEST_ASSERT_FALSE(d.update(t + D::kSoakMs - 1, cold));
        TEST_ASSERT_TRUE(d.update(t + D::kSoakMs, cold));
        TEST_ASSERT_EQUAL(r, d.round());
        t += D::kSoakMs;
        TEST_ASSERT_FALSE(d.update(t + D::kPumpMs, cold));
        t += D::kPumpMs;
    }
    TEST_ASSERT_TRUE(d.update(t + D::kSoakMs, cold)); // the last soak over: the rest through
    t += D::kSoakMs;
    TEST_ASSERT_EQUAL(D::kRest, d.phase());
    TEST_ASSERT_EQUAL(0, d.round());
    TEST_ASSERT_FALSE(d.update(t + D::kBurstMs, cold)); // a break after each burst
    TEST_ASSERT_TRUE(d.update(t + D::kBurstMs + D::kBurstRestMs, cold));
    t += D::kBurstMs + D::kBurstRestMs + 5000;
    TEST_ASSERT_FALSE(d.update(t, empty)); // tank empty: wait for clear water
    TEST_ASSERT_EQUAL(D::kWaitRinse, d.phase());
    TEST_ASSERT_EQUAL(1, d.pass());
    TEST_ASSERT_FALSE(d.next(t + 1000, false)); // "Weiter" with the tank still empty: no
    TEST_ASSERT_FALSE(d.update(t + 60000, empty));
    TEST_ASSERT_TRUE(d.next(t + 60000, true));
    TEST_ASSERT_TRUE(d.update(t + 60001, cold));
    TEST_ASSERT_EQUAL(D::kRinse, d.phase());
    TEST_ASSERT_FALSE(d.update(t + 70000, empty)); // first tank of clear water through
    TEST_ASSERT_EQUAL(D::kWaitRinse, d.phase());
    TEST_ASSERT_EQUAL(2, d.pass());
    TEST_ASSERT_TRUE(d.next(t + 80000, true));
    TEST_ASSERT_TRUE(d.update(t + 80001, cold));
    TEST_ASSERT_FALSE(d.update(t + 80000 + D::kBurstMs, cold)); // ran empty during a break
    TEST_ASSERT_FALSE(d.update(t + 80000 + D::kBurstMs + 1000, empty));
    TEST_ASSERT_EQUAL(D::kDone, d.phase());
    TEST_ASSERT_FALSE(d.running());
}

void test_descale_program_asks_for_more_solution_and_stops_on_its_own() {
    using D = orione::DescaleProgram;
    const D::Inputs cold{true, 30.0f}, empty{false, 30.0f};
    D d;
    d.start(0);
    TEST_ASSERT_TRUE(d.update(0, cold)); // cold already: pumps at once
    TEST_ASSERT_FALSE(d.update(5000, empty)); // ran empty in round 1
    TEST_ASSERT_EQUAL(D::kRefill, d.phase());
    TEST_ASSERT_EQUAL(1, d.round());
    TEST_ASSERT_FALSE(d.update(200000, empty)); // waits for "Weiter"
    TEST_ASSERT_TRUE(d.next(200000, true));
    TEST_ASSERT_TRUE(d.update(200001, cold)); // round 1 pumps again, the full time
    TEST_ASSERT_TRUE(d.update(200000 + D::kPumpMs - 1, cold));
    TEST_ASSERT_FALSE(d.update(200000 + D::kPumpMs, cold));
    TEST_ASSERT_FALSE(d.next(200000 + D::kPumpMs, true)); // nothing to continue while soaking
    d.stop();
    TEST_ASSERT_EQUAL(D::kOff, d.phase());
    TEST_ASSERT_FALSE(d.update(999999, cold));
    // a pass ends after kPassMaxMs of pumping even if the sensor never says empty
    D r;
    r.start(0);
    r.update(0, cold);
    uint32_t t = 0;
    for (int k = 0; k < D::kRounds; ++k) {
        r.update(t + D::kPumpMs, cold);
        r.update(t + D::kPumpMs + D::kSoakMs, cold);
        t += D::kPumpMs + D::kSoakMs;
    }
    TEST_ASSERT_EQUAL(D::kRest, r.phase());
    const uint32_t restStart = t;
    for (int k = 0; k < 100 && r.phase() == D::kRest; ++k) {
        r.update(t + D::kBurstMs, cold);
        r.update(t + D::kBurstMs + D::kBurstRestMs, cold);
        t += D::kBurstMs + D::kBurstRestMs;
    }
    TEST_ASSERT_EQUAL(D::kWaitRinse, r.phase());
    TEST_ASSERT_TRUE(t - restStart <= (D::kPassMaxMs / D::kBurstMs + 1) * (D::kBurstMs + D::kBurstRestMs)); // 15 min of pumping
}

void test_rinse_stops_after_ten_seconds() {
    using R = orione::RinseAfterShot;
    TEST_ASSERT_FALSE(R::over(0));
    TEST_ASSERT_FALSE(R::over(9999));
    TEST_ASSERT_TRUE(R::over(10000)); // Dominik 09.10.2026: "10s max"
    TEST_ASSERT_TRUE(R::over(25000));
}

void test_flush_by_hand_with_one_pulse_for_the_rinse() {
    orione::WarmupFlush f;
    const orione::WarmupFlush::Inputs ok{true, false, true, true, false};
    f.update(0, 93.0, 93.0, ok); // warm start: nothing pending
    f.requestStart(1);
    TEST_ASSERT_TRUE(f.update(1000, 93.0, 93.0, ok));
    TEST_ASSERT_EQUAL(1, f.pulses());
    TEST_ASSERT_EQUAL(1, f.pulse());
    TEST_ASSERT_TRUE(f.update(1000 + orione::WarmupFlush::kPulseMs - 1, 93.0, 93.0, ok));
    TEST_ASSERT_FALSE(f.update(1000 + orione::WarmupFlush::kPulseMs, 93.0, 93.0, ok));
    TEST_ASSERT_EQUAL(orione::WarmupFlush::kDone, f.phase()); // one pulse, no pause after it
    f.requestStart(); // by hand again: the warm-up flush's three
    TEST_ASSERT_TRUE(f.update(60000, 93.0, 93.0, ok));
    TEST_ASSERT_EQUAL(3, f.pulses());
    f.requestStart(9); // clamped
    orione::WarmupFlush g;
    g.update(0, 93.0, 93.0, ok);
    g.requestStart(0);
    g.update(1, 93.0, 93.0, ok);
    TEST_ASSERT_EQUAL(1, g.pulses());
}

void test_beans_keep_their_roast_date_and_take_format_1_over() {
    orione::BeanProfiles p;
    orione::BeanRecipe r;
    std::snprintf(r.name, sizeof(r.name), "%s", "Ettli Don Pedro");
    r.doseTenths = 165;
    p.put(r);
    TEST_ASSERT_TRUE(p.setRoast("ettli don pedro", 20360));
    TEST_ASSERT_FALSE(p.setRoast("Unbekannt", 20360));
    r.doseTenths = 170; // the settings changed: their recipe has no roast date, the bean keeps its own
    p.put(r);
    TEST_ASSERT_EQUAL_UINT16(20360, p.at(p.find("Ettli Don Pedro")).roastDay);
    TEST_ASSERT_EQUAL_UINT16(170, p.at(p.find("Ettli Don Pedro")).doseTenths);
    TEST_ASSERT_TRUE(p.setRoast("Ettli Don Pedro", 0)); // cleared
    p.put(r);
    TEST_ASSERT_EQUAL_UINT16(0, p.at(0).roastDay);
    p.setRoast("Ettli Don Pedro", 20361);
    const auto s = p.stored();
    orione::BeanProfiles q;
    TEST_ASSERT_TRUE(q.restore(&s, sizeof(s)));
    TEST_ASSERT_EQUAL_UINT16(20361, q.at(0).roastDay);
    // format 1: the recipes come along, without a date
    orione::BeanProfiles::StoredV1 v1{};
    v1.version = 1;
    v1.count = 1;
    v1.clock = 7;
    std::snprintf(v1.beans[0].name, sizeof(v1.beans[0].name), "%s", "Röstwerk Hell");
    std::snprintf(v1.beans[0].grind, sizeof(v1.beans[0].grind), "%s", "19");
    v1.beans[0].doseTenths = 180;
    v1.beans[0].targetTenths = 360;
    v1.beans[0].setpointTenths = 930;
    v1.beans[0].lagCs = 91;
    v1.beans[0].used = 7;
    TEST_ASSERT_TRUE(orione::BeanProfiles::readable(sizeof(v1)));
    orione::BeanProfiles o;
    TEST_ASSERT_TRUE(o.restore(&v1, sizeof(v1)));
    TEST_ASSERT_EQUAL(1, o.count());
    TEST_ASSERT_EQUAL_STRING("Röstwerk Hell", o.at(0).name);
    TEST_ASSERT_EQUAL_STRING("19", o.at(0).grind);
    TEST_ASSERT_EQUAL_UINT16(180, o.at(0).doseTenths);
    TEST_ASSERT_EQUAL_UINT16(360, o.at(0).targetTenths);
    TEST_ASSERT_EQUAL_INT16(930, o.at(0).setpointTenths);
    TEST_ASSERT_EQUAL_UINT16(91, o.at(0).lagCs);
    TEST_ASSERT_EQUAL_UINT16(0, o.at(0).roastDay);
    TEST_ASSERT_FALSE(orione::BeanProfiles::readable(sizeof(v1) + 1));
}

void test_tank_sensor_counts_after_three_seconds_the_same_way() {
    orione::Debounce d(3000);
    TEST_ASSERT_TRUE(d.update(true, 0));        // the first reading at once
    TEST_ASSERT_TRUE(d.update(false, 1000));    // flickers: not yet
    TEST_ASSERT_TRUE(d.update(true, 2000));     // back: nothing happened
    TEST_ASSERT_TRUE(d.update(false, 2500));
    TEST_ASSERT_TRUE(d.update(false, 5400));    // 2.9 s
    TEST_ASSERT_FALSE(d.update(false, 5500));   // 3 s empty: empty
    TEST_ASSERT_FALSE(d.update(true, 6000));    // refilled: 3 s too
    TEST_ASSERT_TRUE(d.update(true, 9000));
    orione::Debounce e(3000);
    TEST_ASSERT_FALSE(e.update(false, 100));    // started empty: empty at once
}

void test_stats_count_the_drip_tray_and_take_format_2_over() {
    orione::MachineStats a;
    a.drip(300);
    TEST_ASSERT_FALSE(a.dripDue(500.0f)); // 60 %
    a.drip(100);
    TEST_ASSERT_TRUE(a.dripDue(500.0f));  // 80 %
    TEST_ASSERT_FALSE(a.dripDue(0.0f));   // no reminder
    const auto s = a.stored();
    orione::MachineStats b;
    TEST_ASSERT_TRUE(b.restore(&s, sizeof(s)));
    TEST_ASSERT_EQUAL_UINT32(400, b.dripMl());
    b.dripEmptied();
    TEST_ASSERT_EQUAL_UINT32(0, b.dripMl());
    orione::MachineStats::StoredV2 v2{2, 245, 44100, 12300, 1791000000, 20000, 2857, 3, 12, 1791500000};
    orione::MachineStats c;
    TEST_ASSERT_TRUE(orione::MachineStats::readable(sizeof(v2)));
    TEST_ASSERT_TRUE(c.restore(&v2, sizeof(v2)));
    TEST_ASSERT_EQUAL_UINT32(1791500000, c.backflushAt());
    TEST_ASSERT_EQUAL_UINT32(245, c.total());
    TEST_ASSERT_EQUAL_UINT32(0, c.dripMl());
}

void test_flush_waits_as_long_as_set_after_ready() {
    orione::WarmupFlush f;
    orione::WarmupFlush::Inputs in{true, true, true, true, false, 10 * 60000u};
    f.update(0, 20.0, 93.0, in); // cold start
    TEST_ASSERT_FALSE(f.update(1000, 93.0, 93.0, in));
    TEST_ASSERT_FALSE(f.update(1000 + 9 * 60000, 93.0, 93.0, in)); // 9 of 10 minutes steady
    TEST_ASSERT_TRUE(f.update(1000 + 10 * 60000, 93.0, 93.0, in));  // 10: the first pulse
    orione::WarmupFlush g;
    in.settledMs = 0; // no wait: as soon as it is near the setpoint
    g.update(0, 20.0, 93.0, in);
    TEST_ASSERT_FALSE(g.update(1000, 80.0, 93.0, in)); // still heating
    TEST_ASSERT_TRUE(g.update(2000, 92.0, 93.0, in));
}

void test_water_estimates() {
    TEST_ASSERT_EQUAL_UINT32(50, orione::Water::shotMl(33.4f, 16.5f, 28.0f)); // cup + what the puck keeps
    TEST_ASSERT_EQUAL_UINT32(56, orione::Water::shotMl(-1.0f, 16.5f, 28.0f)); // no scale: the time
    TEST_ASSERT_EQUAL_UINT32(24, orione::Water::rinseMl(3.0f));
    TEST_ASSERT_EQUAL_UINT32(50, orione::Water::backflushMl(5, 5.0f));
    TEST_ASSERT_EQUAL_UINT32(0, orione::Water::rinseMl(NAN));
}

void test_stats_count_days_weeks_and_descaling() {
    orione::MachineStats m;
    // 2026-10-08 (a Thursday) 07:00 local
    const int64_t thursday = 1791443200LL + 7 * 3600;
    const int32_t d = orione::MachineStats::dayOf(thursday);
    TEST_ASSERT_EQUAL_INT32(orione::MachineStats::weekOf(d), orione::MachineStats::weekOf(d - 3)); // Monday: same week
    TEST_ASSERT_NOT_EQUAL(orione::MachineStats::weekOf(d), orione::MachineStats::weekOf(d - 4)); // Sunday before: not
    m.shot(16.5f, 50, d);
    m.shot(16.5f, 50, d);
    TEST_ASSERT_EQUAL_UINT16(2, m.today(d));
    TEST_ASSERT_EQUAL_UINT16(2, m.week(d));
    m.shot(18.0f, 54, d + 1); // Friday
    TEST_ASSERT_EQUAL_UINT16(0, m.today(d)); // a day later the count is the new day's
    TEST_ASSERT_EQUAL_UINT16(1, m.today(d + 1));
    TEST_ASSERT_EQUAL_UINT16(3, m.week(d + 1));
    m.shot(16.5f, 50, d + 4); // Monday: a new week
    TEST_ASSERT_EQUAL_UINT16(1, m.week(d + 4));
    TEST_ASSERT_EQUAL_UINT32(4, m.total());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 67.5f, m.doseGrams());
    m.shot(0.0f, 30, -1); // clock not set: counts in all, not by day
    TEST_ASSERT_EQUAL_UINT32(5, m.total());
    m.water(24);
    TEST_ASSERT_EQUAL_UINT32(258, m.waterMl());
    TEST_ASSERT_FALSE(m.descaleDue(0.0f)); // no reminder
    TEST_ASSERT_TRUE(m.descaleDue(0.2f));
    m.descaled(1791443200u);
    TEST_ASSERT_EQUAL_UINT32(0, m.waterMl());
    TEST_ASSERT_EQUAL_UINT32(5, m.total()); // the counters stay
    const auto st = m.stored();
    orione::MachineStats back;
    TEST_ASSERT_TRUE(back.restore(&st, sizeof(st)));
    TEST_ASSERT_EQUAL_UINT32(5, back.total());
    TEST_ASSERT_EQUAL_UINT16(1, back.week(d + 4));
    TEST_ASSERT_FALSE(back.restore(&st, sizeof(st) - 1));
}

void test_cleaning_with_detergent_then_rinse() {
    orione::CleaningProgram c;
    TEST_ASSERT_TRUE(c.cyclesDone()); // no program: backflush is over after its cycles
    c.start();
    TEST_ASSERT_EQUAL(orione::CleaningProgram::kDetergent, c.phase());
    c.cyclesStart();
    TEST_ASSERT_EQUAL(orione::CleaningProgram::kDetergent, c.phase());
    TEST_ASSERT_FALSE(c.cyclesDone()); // detergent done: rinse it out
    TEST_ASSERT_EQUAL(orione::CleaningProgram::kRinseOut, c.phase());
    c.cyclesStart(); // brew switch on again: clear water
    TEST_ASSERT_EQUAL(orione::CleaningProgram::kRinse, c.phase());
    TEST_ASSERT_TRUE(c.cyclesDone());
    TEST_ASSERT_EQUAL(orione::CleaningProgram::kOff, c.phase());
}

void test_temperature_course_during_the_shot() {
    using T = orione::TempProfile;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 94.0f, T::setpoint(94.0, -3.0, 0.0, 30.0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 92.5f, T::setpoint(94.0, -3.0, 15.0, 30.0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 91.0f, T::setpoint(94.0, -3.0, 45.0, 30.0)); // after the end: the end value
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 94.0f, T::setpoint(94.0, 0.0, 15.0, 30.0));  // off
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 89.0f, T::setpoint(94.0, -9.0, 30.0, 30.0)); // at most 5 K
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 94.0f, T::setpoint(94.0, 2.0, 10.0, 0.0));   // no duration: off
}

void test_channeling_found_in_a_flow_jump_not_in_a_steady_rise() {
    using C = orione::ChannelCheck;
    // the machine's shot of 08.10.2026, 0.5 s apart: first drops after 12 s (pre-infusion), slow rise to 1.9 g/s
    const float steady[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.1f, 2.1f, 1.8f, 1.4f, 1.2f, 1.1f, 1.1f, 1.1f,
                            1.4f, 1.3f, 1.5f, 1.6f, 1.5f, 1.6f, 1.8f, 1.8f, 1.8f, 1.9f};
    TEST_ASSERT_TRUE(C::find(steady, sizeof(steady) / sizeof(steady[0]), 0.5f, 12.0f) < 0.0f);
    // the same with water breaking through at 17 s: 1.5 -> 3.0 g/s and stays
    float jump[sizeof(steady) / sizeof(steady[0])];
    std::memcpy(jump, steady, sizeof(steady));
    for (int i = 34; i < 42; ++i) {
        jump[i] = 3.0f;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 17.0f, C::find(jump, sizeof(jump) / sizeof(jump[0]), 0.5f, 12.0f));
    // one bumped value is no channel
    float bump[sizeof(steady) / sizeof(steady[0])];
    std::memcpy(bump, steady, sizeof(steady));
    bump[36] = 3.2f;
    TEST_ASSERT_TRUE(C::find(bump, sizeof(bump) / sizeof(bump[0]), 0.5f, 12.0f) < 0.0f);
    // the rise right after the first drops is no channel either
    TEST_ASSERT_TRUE(C::find(steady, 30, 0.5f, 12.0f) < 0.0f);
    TEST_ASSERT_TRUE(C::find(nullptr, 10, 0.5f, 1.0f) < 0.0f);
    TEST_ASSERT_TRUE(C::find(steady, 42, 0.5f, -1.0f) < 0.0f); // no first drops known: no scale
}

void test_shot_keeps_its_preinfusion() {
    orione::ShotLog log;
    log.record(29.0f, 36.0f, 0, 0);
    log.notePreinfusion(2.0f, 4.0f, false);
    TEST_ASSERT_EQUAL(20, log.at(0).piTenths);
    TEST_ASSERT_EQUAL(40, log.at(0).piPauseTenths);
    TEST_ASSERT_EQUAL(0, log.at(0).piFlags);
    log.record(30.0f, 36.0f, 0, 10000);
    log.notePreinfusion(1.5f, 6.0f, true);
    TEST_ASSERT_EQUAL(orione::ShotLog::kPauseValveOpen, log.at(0).piFlags);
    log.record(26.0f, 36.0f, 0, 20000);
    log.notePreinfusion(0.0f, 0.0f, true); // none: no flag either
    TEST_ASSERT_EQUAL(0, log.at(0).piTenths);
    TEST_ASSERT_EQUAL(0, log.at(0).piFlags);
    log.record(26.0f, 36.0f, 0, 30000);
    log.notePreinfusion(40.0f, NAN, false); // over 25.5 s: kept at the limit; not a number: none
    TEST_ASSERT_EQUAL(255, log.at(0).piTenths);
    TEST_ASSERT_EQUAL(0, log.at(0).piPauseTenths);
    const auto stored = log.stored();
    orione::ShotLog back;
    TEST_ASSERT_TRUE(back.restore(&stored, sizeof(stored)));
    log.noteChanneling();
    TEST_ASSERT_EQUAL(orione::ShotLog::kChanneling, log.at(0).piFlags & orione::ShotLog::kChanneling);
    log.notePreinfusion(2.0f, 4.0f, true); // keeps the channeling mark
    TEST_ASSERT_EQUAL(orione::ShotLog::kChanneling | orione::ShotLog::kPauseValveOpen, log.at(0).piFlags);
    TEST_ASSERT_EQUAL(15, back.at(2).piTenths); // newest first: the second shot is third now
    TEST_ASSERT_EQUAL(60, back.at(2).piPauseTenths);
    TEST_ASSERT_EQUAL(orione::ShotLog::kPauseValveOpen, back.at(2).piFlags);
}

void test_shot_stopped_by_hand_is_marked_and_kept() {
    orione::ShotLog log;
    log.noteStoppedByHand(); // no shot yet: nothing to mark
    TEST_ASSERT_EQUAL(0, log.count());
    log.record(18.0f, 24.0f, 0, 10000);
    log.notePreinfusion(2.0f, 4.0f, true);
    log.noteStoppedByHand();
    log.noteChanneling();
    TEST_ASSERT_EQUAL(orione::ShotLog::kStoppedByHand | orione::ShotLog::kChanneling | orione::ShotLog::kPauseValveOpen, log.at(0).piFlags);
    log.notePreinfusion(0.0f, 0.0f, true); // no pre-infusion: only that flag goes
    TEST_ASSERT_EQUAL(orione::ShotLog::kStoppedByHand | orione::ShotLog::kChanneling, log.at(0).piFlags);
    const auto stored = log.stored();
    orione::ShotLog back;
    TEST_ASSERT_TRUE(back.restore(&stored, sizeof(stored)));
    TEST_ASSERT_EQUAL(orione::ShotLog::kStoppedByHand, back.at(0).piFlags & orione::ShotLog::kStoppedByHand);
    log.record(25.0f, 36.0f, 0, 60000); // the next shot starts unmarked
    TEST_ASSERT_EQUAL(0, log.at(0).piFlags & orione::ShotLog::kStoppedByHand);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gate_answers_at_once_when_idle);
    RUN_TEST(test_gate_lets_a_second_request_wait);
    RUN_TEST(test_gate_refuses_when_the_queue_is_full);
    RUN_TEST(test_gate_brake_on_low_heap_even_when_idle);
    RUN_TEST(test_gate_brake_on_a_broken_up_heap);
    RUN_TEST(test_gate_resets_when_the_heap_is_almost_gone);
    RUN_TEST(test_gate_brake_comes_before_the_queue);
    RUN_TEST(test_gate_second_parallel_response_only_with_enough_heap);
    RUN_TEST(test_gate_limits_are_consistent);
    RUN_TEST(test_relays_are_fixed_to_high_trigger);
    RUN_TEST(test_every_relay_setting_is_fixed);
    RUN_TEST(test_brew_switch_is_a_normally_open_toggle);
    RUN_TEST(test_scale_is_bluetooth_and_offline_mode_is_off);
    RUN_TEST(test_ordinary_settings_are_not_fixed);
    RUN_TEST(test_fixed_paths_are_unique);
    RUN_TEST(test_shots_newest_first_and_at_most_five);
    RUN_TEST(test_shots_short_ones_are_left_out);
    RUN_TEST(test_shots_with_a_scale_need_coffee_in_the_cup);
    RUN_TEST(test_shots_drops_counted_until_the_weight_stops_rising);
    RUN_TEST(test_shots_drops_counted_at_most_fifteen_seconds);
    RUN_TEST(test_shots_without_scale_settle_after_four_seconds);
    RUN_TEST(test_heat_boost_while_the_pump_runs);
    RUN_TEST(test_steam_seen_from_the_temperature);
    RUN_TEST(test_shots_delete_keeps_the_others_and_their_curves);
    RUN_TEST(test_shots_delete_the_newest_while_its_drops_are_counted);
    RUN_TEST(test_shots_delete_counts_since_backflush_only_for_newer_ones);
    RUN_TEST(test_brew_weight_ignores_a_cup_put_on_during_the_shot);
    RUN_TEST(test_brew_weight_late_tare_is_not_a_negative_shot);
    RUN_TEST(test_shots_drops_after_the_stop_are_counted);
    RUN_TEST(test_shots_without_scale_keep_no_weight);
    RUN_TEST(test_shots_settle_across_millis_wrap);
    RUN_TEST(test_shots_survive_save_and_restore);
    RUN_TEST(test_shots_reject_foreign_data);
    RUN_TEST(test_shots_numbered_for_their_curves);
    RUN_TEST(test_curve_point_every_half_second);
    RUN_TEST(test_curve_long_shot_keeps_its_length);
    RUN_TEST(test_curve_marks_the_stop_and_keeps_the_drops);
    RUN_TEST(test_curve_stop_survives_halving);
    RUN_TEST(test_curve_without_scale_or_sensor);
    RUN_TEST(test_curve_survives_save_and_restore);
    RUN_TEST(test_shots_keep_recipe_and_taste);
    RUN_TEST(test_shots_keep_the_beans_of_each_shot);
    RUN_TEST(test_shots_count_since_backflush);
    RUN_TEST(test_flow_is_the_slope_over_the_last_second);
    RUN_TEST(test_flow_needs_some_time_and_ignores_a_lifted_cup);
    RUN_TEST(test_flow_skips_calls_between_scale_reports);
    RUN_TEST(test_heap_watch_restarts_after_a_minute_low_but_not_during_a_shot);
    RUN_TEST(test_heap_watch_uses_the_restart_limits_and_survives_millis_wrap);
    RUN_TEST(test_shots_keep_start_temperature_and_first_drops);
    RUN_TEST(test_curve_keeps_the_flow);
    RUN_TEST(test_flush_after_a_cold_start_three_pulses_once_settled);
    RUN_TEST(test_flush_not_after_a_warm_restart);
    RUN_TEST(test_flush_ignores_readings_before_the_sensor_has_one);
    RUN_TEST(test_flush_never_without_a_water_level_sensor);
    RUN_TEST(test_flush_also_with_a_steady_offset_but_not_while_it_swings);
    RUN_TEST(test_flush_settling_restarts_when_the_temperature_leaves_the_band);
    RUN_TEST(test_flush_cut_short_by_an_empty_tank_and_not_resumed);
    RUN_TEST(test_flush_user_taking_over_cancels_the_pending_one);
    RUN_TEST(test_flush_automatic_off_but_by_hand_any_time);
    RUN_TEST(test_flush_stopped_by_hand);
    RUN_TEST(test_flush_by_hand_needs_water_and_a_free_machine);
    RUN_TEST(test_scales_one_entry_per_address_strongest_first);
    RUN_TEST(test_scales_forgotten_when_not_seen_and_room_made_when_full);
    RUN_TEST(test_scale_addresses_normalized_or_refused);
    RUN_TEST(test_lag_learns_half_the_error_in_seconds_of_flow);
    RUN_TEST(test_shots_from_format_6_are_taken_over);
    RUN_TEST(test_beans_keep_a_recipe_each);
    RUN_TEST(test_beans_the_longest_unused_makes_room);
    RUN_TEST(test_shot_keeps_target_weight_at_stop_and_lead);
    RUN_TEST(test_shot_keeps_its_preinfusion);
    RUN_TEST(test_log_ring_keeps_the_newest_lines);
    RUN_TEST(test_schedule_fires_once_at_its_minute_on_its_days);
    RUN_TEST(test_schedule_two_windows_per_day_and_over_midnight);
    RUN_TEST(test_week_plan_text_round_trip_and_the_old_settings);
    RUN_TEST(test_standby_wakes_only_on_a_switch_turned_on_during_it);
    RUN_TEST(test_pointer_set_keeps_a_few_without_heap);
    RUN_TEST(test_stats_keep_the_backflush_date_and_take_format_1_over);
    RUN_TEST(test_rinse_after_a_shot_skips_the_preinfusion_for_two_minutes);
    RUN_TEST(test_standby_keeps_warm_then_goes_off);
    RUN_TEST(test_standby_warm_off_by_setting_and_by_the_schedule);
    RUN_TEST(test_descale_program_runs_cold_rounds_then_rinses_twice);
    RUN_TEST(test_descale_program_asks_for_more_solution_and_stops_on_its_own);
    RUN_TEST(test_rinse_stops_after_ten_seconds);
    RUN_TEST(test_shot_stopped_by_hand_is_marked_and_kept);
    RUN_TEST(test_flush_by_hand_with_one_pulse_for_the_rinse);
    RUN_TEST(test_beans_keep_their_roast_date_and_take_format_1_over);
    RUN_TEST(test_tank_sensor_counts_after_three_seconds_the_same_way);
    RUN_TEST(test_stats_count_the_drip_tray_and_take_format_2_over);
    RUN_TEST(test_flush_waits_as_long_as_set_after_ready);
    RUN_TEST(test_water_estimates);
    RUN_TEST(test_stats_count_days_weeks_and_descaling);
    RUN_TEST(test_cleaning_with_detergent_then_rinse);
    RUN_TEST(test_temperature_course_during_the_shot);
    RUN_TEST(test_channeling_found_in_a_flow_jump_not_in_a_steady_rise);
    return UNITY_END();
}
