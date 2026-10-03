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
#include <OrioneShots.h>
#include <OrioneWebGate.h>

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

void test_heap_watch_uses_the_brake_limits_and_survives_millis_wrap() {
    TEST_ASSERT_TRUE(orione::HeapWatch::low(30 * 1024, 7 * 1024));  // fragmented
    TEST_ASSERT_TRUE(orione::HeapWatch::low(17 * 1024, 16 * 1024)); // too little
    TEST_ASSERT_FALSE(orione::HeapWatch::low(30 * 1024, 16 * 1024));
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
    RUN_TEST(test_shots_count_since_backflush);
    RUN_TEST(test_flow_is_the_slope_over_the_last_second);
    RUN_TEST(test_flow_needs_some_time_and_ignores_a_lifted_cup);
    RUN_TEST(test_flow_skips_calls_between_scale_reports);
    RUN_TEST(test_heap_watch_restarts_after_a_minute_low_but_not_during_a_shot);
    RUN_TEST(test_heap_watch_uses_the_brake_limits_and_survives_millis_wrap);
    RUN_TEST(test_shots_keep_start_temperature_and_first_drops);
    RUN_TEST(test_curve_keeps_the_flow);
    return UNITY_END();
}
