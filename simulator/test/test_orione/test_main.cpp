/**
 * @file test_main.cpp
 *
 * @brief Pure logic of the slim Orione build (lib/Orione): the web request gate with its
 *        emergency brake, the settings that are fixed on Dominik's machine and the log of the
 *        last shots.
 */

#include "../support/TestSupport.h"

#include <OrioneFixed.h>
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
    return UNITY_END();
}
