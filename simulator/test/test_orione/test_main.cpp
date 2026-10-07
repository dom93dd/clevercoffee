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

void test_lead_learns_half_the_error_and_ignores_outliers() {
    using L = orione::BrewLead;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.9f, L::learn(1.5f, 36.0f, 36.8f));  // 0.8 g over: stop 0.4 g earlier
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, L::learn(1.5f, 36.0f, 35.5f)); // 0.5 g short: 0.25 g later
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, L::learn(1.5f, 36.0f, 39.5f));  // 3.5 g over (the bench test that took it to 5 g)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, L::learn(1.5f, 36.0f, 41.4f));  // cup lifted, poured by hand
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, L::learn(1.5f, 36.0f, 32.9f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, L::learn(0.5f, 36.0f, 34.0f));  // never below 0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, L::learn(4.5f, 36.0f, 38.5f));  // never over 5 g
    float lead = L::kStart; // a steady 1.2 g too much each time: settles within a few shots
    for (int shot = 0; shot < 4; ++shot) {
        const float atStop = 36.0f - lead;
        lead = L::learn(lead, 36.0f, atStop + 2.7f); // drops of 2.7 g after the stop
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.7f, lead);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, L::learn(1.5f, 0.0f, 36.0f));   // no target: nothing to learn
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, L::learn(1.5f, 36.0f, NAN));
}

void test_shot_keeps_target_weight_at_stop_and_lead() {
    orione::ShotLog log;
    log.record(26.0f, 34.6f, 0, 0);
    log.noteWeights(36.0f, 34.6f, 1.5f);
    log.settle(1000, 36.4f); // drops
    TEST_ASSERT_TRUE(log.settle(orione::ShotLog::kSettleMs, 36.4f));
    const auto& x = log.at(0);
    TEST_ASSERT_EQUAL(360, x.targetTenths);
    TEST_ASSERT_EQUAL(346, x.stopTenths);
    TEST_ASSERT_EQUAL(15, x.leadTenths);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.4f, x.grams);
    log.record(25.0f, -1.0f, 0, 10000); // by time, no scale
    log.noteWeights(0.0f, -1.0f, 1.5f);
    TEST_ASSERT_EQUAL(0, log.at(0).targetTenths);
    TEST_ASSERT_EQUAL(0, log.at(0).stopTenths);
    orione::ShotLog back; // saved and restored with the new fields
    const auto stored = log.stored();
    TEST_ASSERT_TRUE(back.restore(&stored, sizeof(stored)));
    TEST_ASSERT_EQUAL(346, back.at(1).stopTenths);
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
    RUN_TEST(test_heap_watch_uses_the_brake_limits_and_survives_millis_wrap);
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
    RUN_TEST(test_lead_learns_half_the_error_and_ignores_outliers);
    RUN_TEST(test_shot_keeps_target_weight_at_stop_and_lead);
    return UNITY_END();
}
