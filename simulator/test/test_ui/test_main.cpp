/**
 * @file test_main.cpp
 *
 * @brief Behaviour of RoundUi: which screen shows when, hysteresis, when to redraw, transitions.
 */

#include "../support/TestSupport.h"

#include "../../src/Esp32Tempo.h"

#include <RoundDisplayControl.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace rd;

namespace {
    lgfx::LGFX_Sprite bands[2];

    /** Model of a machine near the setpoint in normal operation */
    Model normal(const float temperature = 94.0f, const float setpoint = 94.0f) {
        Model m;
        m.mode = Mode::Normal;
        m.temperature = temperature;
        m.setpoint = setpoint;
        m.readyBand = 0.3f;
        return m;
    }

    void render(RoundUi& ui, const uint32_t now) {
        ui.render(bands, 2, now, [](lgfx::LGFX_Sprite&, int) {});
    }

    int screenOf(const RoundUi& ui) {
        return static_cast<int>(ui.screen());
    }

    constexpr int S(const Screen s) {
        return static_cast<int>(s);
    }
} // namespace

void setUp() {
}

void tearDown() {
}

// --- which screen ------------------------------------------------------------------------

void test_modes_select_their_screens() {
    const struct {
            Mode mode;
            Screen screen;
    } cases[] = {
        {Mode::Init, Screen::Boot},
        {Mode::ManualFlush, Screen::Flush},
        {Mode::HotWater, Screen::HotWater},
        {Mode::Steam, Screen::Steam},
        {Mode::Backflush, Screen::Backflush},
        {Mode::WaterTankEmpty, Screen::WaterTankEmpty},
        {Mode::Standby, Screen::Standby},
        {Mode::PidDisabled, Screen::PidDisabled},
        {Mode::Brew, Screen::Brew},
        {Mode::EmergencyStop, Screen::EmergencyStop},
        {Mode::SensorError, Screen::SensorError},
    };

    for (const auto& c : cases) {
        RoundUi ui;
        Model m = normal();
        m.mode = c.mode;
        ui.update(m, 0);
        TEST_ASSERT_EQUAL_STRING(screenName(c.screen), screenName(ui.screen()));
    }
}

void test_alarms_win_over_brew_timer_and_messages() {
    RoundUi ui;
    Model m = normal();
    m.brewTimerVisible = true;
    m.mode = Mode::SensorError;
    ui.update(m, 0);
    TEST_ASSERT_EQUAL(S(Screen::SensorError), screenOf(ui));

    ui.showMessage(Message{"WLAN", "offline", nullptr});
    m.mode = Mode::EmergencyStop;
    ui.update(m, 10);
    TEST_ASSERT_EQUAL(S(Screen::EmergencyStop), screenOf(ui));
}

void test_brew_timer_keeps_brew_screen_after_the_shot() {
    RoundUi ui;
    Model m = normal();
    m.brewTimerVisible = true; // hold time after the shot, the machine is back to normal
    ui.update(m, 0);
    TEST_ASSERT_EQUAL(S(Screen::Brew), screenOf(ui));
    m.brewTimerVisible = false;
    ui.update(m, 100);
    TEST_ASSERT_EQUAL(S(Screen::Ready), screenOf(ui));
}

void test_message_shows_until_cleared() {
    RoundUi ui;
    ui.showMessage(Message{"VERSION", "4.0.3", nullptr});
    ui.update(normal(), 0);
    TEST_ASSERT_EQUAL(S(Screen::Message), screenOf(ui));
    ui.clearMessage();
    ui.update(normal(), 10);
    TEST_ASSERT_EQUAL(S(Screen::Ready), screenOf(ui));
}

// --- heating / ready -----------------------------------------------------------------------

void test_heating_until_close_to_the_setpoint() {
    RoundUi ui;
    ui.update(normal(22.0f), 0);
    TEST_ASSERT_EQUAL(S(Screen::Heating), screenOf(ui));
    ui.update(normal(89.2f), 100); // 4.8 K below: still heating (hysteresis 0.3 K)
    TEST_ASSERT_EQUAL(S(Screen::Heating), screenOf(ui));
    ui.update(normal(89.4f), 200); // 4.6 K below
    TEST_ASSERT_EQUAL(S(Screen::Ready), screenOf(ui));
}

void test_dip_after_a_shot_stays_on_the_ready_gauge() {
    RoundUi ui;
    ui.update(normal(94.0f), 0);
    ui.update(normal(84.0f), 100); // 10 K below after a shot
    TEST_ASSERT_EQUAL(S(Screen::Ready), screenOf(ui));
    ui.update(normal(78.0f), 200); // 16 K below: e.g. cold water, heating again
    TEST_ASSERT_EQUAL(S(Screen::Heating), screenOf(ui));
}

void test_standby_forgets_the_warm_up() {
    RoundUi ui;
    ui.update(normal(94.0f), 0);
    Model m = normal(70.0f);
    m.mode = Mode::Standby;
    ui.update(m, 100);
    ui.update(normal(84.0f), 200); // back from standby, 10 K below
    TEST_ASSERT_EQUAL(S(Screen::Heating), screenOf(ui));
}

void test_ready_label_with_hysteresis() {
    RoundUi ui;
    ui.update(normal(94.25f), 0);
    TEST_ASSERT_TRUE(ui.ready());
    ui.update(normal(94.5f), 100); // 0.5 K: still within 0.3 + 0.3
    TEST_ASSERT_TRUE(ui.ready());
    ui.update(normal(94.7f), 200);
    TEST_ASSERT_FALSE(ui.ready());
    ui.update(normal(94.5f), 300); // coming back needs 0.3 K again
    TEST_ASSERT_FALSE(ui.ready());
    ui.update(normal(93.8f), 400);
    TEST_ASSERT_TRUE(ui.ready());
}

// --- redraws -------------------------------------------------------------------------------

void test_first_frame_is_always_drawn() {
    RoundUi ui;
    ui.update(normal(), 0);
    TEST_ASSERT_TRUE(ui.needsRedraw(0));
}

void test_no_redraw_without_visible_change() {
    RoundUi ui;
    ui.update(normal(94.02f), 0);
    render(ui, 0);
    ui.update(normal(94.03f), 1000); // below the 0.1 K the screen shows
    TEST_ASSERT_FALSE(ui.needsRedraw(1000));
    ui.update(normal(94.03f), 9999);
    TEST_ASSERT_FALSE(ui.needsRedraw(9999));
}

void test_redraw_when_a_shown_value_changes() {
    RoundUi ui;
    ui.update(normal(94.0f), 0);
    render(ui, 0);
    ui.update(normal(94.1f), 50);
    TEST_ASSERT_FALSE(ui.needsRedraw(50)); // not before the minimum frame interval
    ui.update(normal(94.1f), 80);
    TEST_ASSERT_TRUE(ui.needsRedraw(80));
}

void test_refresh_after_max_interval() {
    RoundUi ui;
    ui.update(normal(), 0);
    render(ui, 0);
    ui.update(normal(), 10000);
    TEST_ASSERT_TRUE(ui.needsRedraw(10000));
}

void test_new_message_text_in_the_same_buffer_redraws() {
    // The firmware reuses one text buffer for all messages
    RoundUi ui;
    char title[16] = "WLAN";
    ui.showMessage(Message{title, nullptr, nullptr});
    ui.update(normal(), 0);
    render(ui, 0);
    snprintf(title, sizeof(title), "IP-ADRESSE");
    ui.showMessage(Message{title, nullptr, nullptr});
    ui.update(normal(), 100);
    TEST_ASSERT_TRUE(ui.needsRedraw(100));
}

void test_alarm_ring_blinks() {
    RoundUi ui;
    Model m = normal();
    m.mode = Mode::SensorError;
    ui.update(m, 0);
    render(ui, 0);
    ui.update(m, 300);
    TEST_ASSERT_FALSE(ui.needsRedraw(300));
    ui.update(m, 500);
    TEST_ASSERT_TRUE(ui.needsRedraw(500));
}

void test_invalidate_forces_a_redraw() {
    RoundUi ui;
    ui.update(normal(), 0);
    render(ui, 0);
    ui.invalidate();
    TEST_ASSERT_TRUE(ui.needsRedraw(1));
}

// --- transitions ---------------------------------------------------------------------------

void test_no_transition_on_the_very_first_frame() {
    RoundUi ui;
    ui.update(normal(), 0);
    TEST_ASSERT_FALSE(ui.animating(0));
}

void test_intro_runs_for_its_length() {
    RoundUi ui;
    ui.update(Model(), 0);
    ui.play(Animation::Intro, 0);
    TEST_ASSERT_TRUE(ui.animating(0));
    TEST_ASSERT_TRUE(ui.animating(1699));
    TEST_ASSERT_FALSE(ui.animating(1700));
}

void test_reveal_after_the_boot_messages() {
    RoundUi ui;
    ui.showMessage(Message{"IP-ADRESSE", "192.168.1.2", nullptr});
    ui.update(normal(), 0);
    render(ui, 0);
    ui.clearMessage();
    ui.update(normal(), 100);
    TEST_ASSERT_TRUE(ui.animating(100));
    TEST_ASSERT_FALSE(ui.animating(1000)); // 900 ms
}

void test_close_into_standby_and_reveal_out_of_it() {
    RoundUi ui;
    ui.update(normal(), 0);
    render(ui, 0);

    Model standby = normal();
    standby.mode = Mode::Standby;
    ui.update(standby, 100);
    TEST_ASSERT_TRUE(ui.animating(100));
    TEST_ASSERT_EQUAL(S(Screen::Standby), screenOf(ui));
    ui.update(standby, 1000);
    TEST_ASSERT_FALSE(ui.animating(1000));
    render(ui, 1000);

    ui.update(normal(), 2000);
    TEST_ASSERT_TRUE(ui.animating(2000));
}

void test_alarm_stops_a_transition() {
    RoundUi ui;
    ui.update(normal(), 0);
    render(ui, 0);
    ui.play(Animation::Reveal, 0);
    Model m = normal();
    m.mode = Mode::EmergencyStop;
    ui.update(m, 100);
    TEST_ASSERT_FALSE(ui.animating(100));
}

void test_transitions_draw_at_frame_rate() {
    RoundUi ui;
    ui.update(normal(), 0);
    render(ui, 0);
    ui.play(Animation::Reveal, 10);
    ui.update(normal(), 10);
    render(ui, 10);
    ui.update(normal(), 30);
    TEST_ASSERT_FALSE(ui.needsRedraw(30)); // 20 ms later
    ui.update(normal(), 45);
    TEST_ASSERT_TRUE(ui.needsRedraw(45));  // 35 ms later, a new animation frame
}

void test_power_sequence_on_the_real_ui() {
    RoundUi ui;
    PowerSequencer power;
    ui.update(normal(), 0);
    render(ui, 0);

    uint32_t t = 0;
    int sleeps = 0;

    for (; t < 2000; t += 20) {
        if (power.update(true, ui, t) == PowerSequencer::Action::Sleep) {
            ++sleeps;
        }

        ui.update(normal(), t);
    }

    TEST_ASSERT_EQUAL_INT(1, sleeps);
    TEST_ASSERT_TRUE(power.asleep());
}

// --- tendency, ready moment, shot statistics ---------------------------------------------

namespace {
    /** Feeds temperatures 100 ms apart, starting at t0, returns the next time */
    uint32_t feed(RoundUi& ui, uint32_t t, const float from, const float to, const float seconds, Model m = normal()) {
        const int steps = static_cast<int>(seconds * 10.0f);

        for (int i = 0; i <= steps; ++i) {
            m.temperature = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
            ui.update(m, t);
            t += 100;
        }

        return t;
    }

    /** One shot: brewing for the given seconds at a constant temperature, then the hold time */
    uint32_t brewShot(RoundUi& ui, uint32_t t, const float seconds, const float temperature) {
        Model m = normal(temperature);
        m.mode = Mode::Brew;
        m.brewPhase = BrewPhase::Running;
        m.brewTimerVisible = true;

        for (float b = 0.0f; b <= seconds; b += 0.1f) {
            m.brewTime = b;
            ui.update(m, t);
            t += 100;
        }

        m.mode = Mode::Normal;
        m.brewPhase = BrewPhase::Finished;
        ui.update(m, t);
        return t + 100;
    }
} // namespace

void test_trend_follows_the_temperature() {
    RoundUi ui;
    uint32_t t = feed(ui, 1, 60.0f, 65.0f, 5.0f); // +1 K/s
    TEST_ASSERT_EQUAL_INT(1, ui.trend());
    t = feed(ui, t, 65.0f, 65.0f, 5.0f);          // steady
    TEST_ASSERT_EQUAL_INT(0, ui.trend());
    t = feed(ui, t, 65.0f, 60.0f, 5.0f);          // -1 K/s
    TEST_ASSERT_EQUAL_INT(-1, ui.trend());
}

void test_trend_ignores_small_wobbles() {
    RoundUi ui;
    uint32_t t = 1;

    for (int i = 0; i < 20; ++i) {
        t = feed(ui, t, 94.0f, 94.1f, 1.0f);
        t = feed(ui, t, 94.1f, 94.0f, 1.0f);
        TEST_ASSERT_EQUAL_INT(0, ui.trend());
    }
}

void test_ready_moment_plays_once_after_warming_up() {
    RoundUi ui;
    uint32_t t = feed(ui, 1, 60.0f, 93.9f, 30.0f); // heating up until the label turns green
    TEST_ASSERT_TRUE(ui.ready());
    TEST_ASSERT_TRUE(ui.effectActive(t - 100));
    t = feed(ui, t, 93.9f, 93.9f, 1.5f);
    TEST_ASSERT_FALSE(ui.effectActive(t)); // 1.4 s

    // A dip after a shot keeps the ready gauge and does not pulse again
    t = feed(ui, t, 93.9f, 88.0f, 5.0f);
    t = feed(ui, t, 88.0f, 93.9f, 5.0f);
    TEST_ASSERT_TRUE(ui.ready());
    TEST_ASSERT_FALSE(ui.effectActive(t - 100));
}

void test_no_ready_moment_when_already_warm() {
    RoundUi ui;
    feed(ui, 1, 94.0f, 94.0f, 3.0f);
    TEST_ASSERT_FALSE(ui.effectActive(500));
}

void test_ready_moment_frames_are_drawn() {
    RoundUi ui;
    uint32_t t = feed(ui, 1, 60.0f, 93.9f, 30.0f) - 100;
    render(ui, t);
    ui.update(normal(93.9f), t + 35);
    TEST_ASSERT_TRUE(ui.needsRedraw(t + 35));
}

void test_shot_average_and_history() {
    RoundUi ui;
    uint32_t t = feed(ui, 1, 94.0f, 94.0f, 1.0f);
    t = brewShot(ui, t, 25.0f, 91.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 91.5f, ui.shotAverage());
    TEST_ASSERT_EQUAL_INT(1, ui.shotCount());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 25.0f, ui.shot(0));

    t = brewShot(ui, t, 28.0f, 93.0f); // a new shot starts a new average
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 93.0f, ui.shotAverage());
    TEST_ASSERT_EQUAL_INT(2, ui.shotCount());
}

void test_shot_history_keeps_the_last_five() {
    RoundUi ui;
    uint32_t t = 1;

    for (int i = 0; i < 7; ++i) {
        t = brewShot(ui, t, 20.0f + static_cast<float>(i), 93.0f);
    }

    TEST_ASSERT_EQUAL_INT(RoundUi::kShotHistory, ui.shotCount());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 22.0f, ui.shot(0)); // the two oldest are gone
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 26.0f, ui.shot(RoundUi::kShotHistory - 1));
}

// --- only changed bands go to the panel --------------------------------------------------

namespace {
    /** Draws a frame like the firmware and counts the bands the filter lets through */
    int sentBands(RoundUi& ui, BandFilter& filter, const uint32_t now) {
        int sent = 0;
        ui.render(bands, 2, now, [&](lgfx::LGFX_Sprite& band, const int top) { sent += filter.changed(top / band.height(), static_cast<const uint16_t*>(band.getBuffer()), band.width() * band.height()) ? 1 : 0; });
        return sent;
    }
} // namespace

void test_band_filter_sends_new_and_changed_bands() {
    BandFilter f;
    uint16_t a[100] = {};
    uint16_t b[100] = {};
    b[57] = 1;
    TEST_ASSERT_TRUE(f.changed(0, a, 100)); // panel content unknown at first
    TEST_ASSERT_FALSE(f.changed(0, a, 100));
    TEST_ASSERT_TRUE(f.changed(1, a, 100)); // each band on its own
    TEST_ASSERT_TRUE(f.changed(0, b, 100));
    TEST_ASSERT_FALSE(f.changed(0, b, 100));
    f.invalidate();
    TEST_ASSERT_TRUE(f.changed(0, b, 100));
    TEST_ASSERT_TRUE(f.changed(-1, b, 100));
    TEST_ASSERT_TRUE(f.changed(BandFilter::kMaxBands, b, 100));
    TEST_ASSERT_NOT_EQUAL(pixelHash(a, 100), pixelHash(b, 100));
}

void test_small_changes_send_few_bands() {
    // 0.1 degrees more: only the digits and the marker on the ring change, not the whole picture
    RoundUi ui;
    BandFilter filter;
    ui.update(normal(94.0f), 1000);
    TEST_ASSERT_EQUAL_INT(6, sentBands(ui, filter, 1000));
    TEST_ASSERT_EQUAL_INT(0, sentBands(ui, filter, 1000)); // same picture again: nothing to send

    ui.update(normal(94.1f), 2000);
    const int sent = sentBands(ui, filter, 2000);
    TEST_ASSERT_GREATER_THAN(0, sent);
    TEST_ASSERT_LESS_OR_EQUAL(3, sent);
}

// --- one band per loop iteration ---------------------------------------------------------

namespace {
    /** Copies the pushed bands into a full picture */
    struct Collector {
            lgfx::LGFX_Sprite screen;

            Collector() {
                screen.setColorDepth(16);
                screen.createSprite(240, 240);
            }

            void operator()(lgfx::LGFX_Sprite& band, const int top) {
                band.pushSprite(&screen, 0, top);
            }

            bool same(const Collector& other) const {
                return std::memcmp(screen.getBuffer(), other.screen.getBuffer(), 240 * 240 * 2) == 0;
            }
    };
} // namespace

void test_band_by_band_gives_the_same_picture() {
    RoundUi a;
    RoundUi b;
    a.update(normal(93.4f), 1000);
    b.update(normal(93.4f), 1000);
    Collector whole;
    Collector stepwise;
    a.render(bands, 2, 1000, [&](lgfx::LGFX_Sprite& band, const int top) { whole(band, top); });

    int steps = 1;
    TEST_ASSERT_FALSE(b.renderBand(bands, 2, [&](lgfx::LGFX_Sprite& band, const int top) { stepwise(band, top); }, 1000));
    TEST_ASSERT_TRUE(b.frameOpen());

    while (!b.renderBand(bands, 2, [&](lgfx::LGFX_Sprite& band, const int top) { stepwise(band, top); })) {
        ++steps;
    }

    TEST_ASSERT_EQUAL_INT(5, steps); // six bands of 40 rows
    TEST_ASSERT_FALSE(b.frameOpen());
    TEST_ASSERT_TRUE(whole.same(stepwise));
    TEST_ASSERT_FALSE(b.needsRedraw(1000));
}

void test_all_bands_of_a_frame_show_the_same_moment() {
    // Intro animation: the bands are drawn over several loop iterations, still one moment
    RoundUi a;
    RoundUi b;
    a.play(Animation::Intro, 0);
    b.play(Animation::Intro, 0);
    Collector whole;
    Collector stepwise;
    a.render(bands, 2, 600, [&](lgfx::LGFX_Sprite& band, const int top) { whole(band, top); });
    b.renderBand(bands, 2, [&](lgfx::LGFX_Sprite& band, const int top) { stepwise(band, top); }, 600);

    while (!b.renderBand(bands, 2, [&](lgfx::LGFX_Sprite& band, const int top) { stepwise(band, top); }, 9999)) {}

    TEST_ASSERT_TRUE(whole.same(stepwise));
}

void test_aborted_frame_starts_over() {
    RoundUi ui;
    ui.update(normal(), 1000);
    int pushed = 0;
    const auto count = [&](lgfx::LGFX_Sprite&, int top) { pushed += top == 0 ? 100 : 1; };
    ui.renderBand(bands, 2, count, 1000);
    ui.renderBand(bands, 2, count);
    ui.abortFrame();
    TEST_ASSERT_FALSE(ui.frameOpen());
    TEST_ASSERT_TRUE(ui.needsRedraw(1000)); // the frame never completed
    pushed = 0;
    ui.renderBand(bands, 2, count, 1000);
    TEST_ASSERT_EQUAL_INT(100, pushed);     // first band again
}

void test_shimmer_runs_at_animation_pace_during_a_shot() {
    // The light along the brew ring moves with the time, not only when the shown brew time changes
    RoundUi ui;
    Model m = normal();
    m.mode = Mode::Brew;
    m.brewPhase = BrewPhase::Running;
    m.brewTimerVisible = true;
    m.brewTime = 12.0f;
    m.brewTargetTime = 25.0f;
    ui.update(m, 1000);
    TEST_ASSERT_TRUE(ui.shimmering());
    render(ui, 1000);
    ui.update(m, 1000 + ui.animationFrameIntervalMs);
    TEST_ASSERT_TRUE(ui.needsRedraw(1000 + ui.animationFrameIntervalMs));

    // Shot done: back to redraws on changes only
    m.brewPhase = BrewPhase::Finished;
    ui.update(m, 2000);
    TEST_ASSERT_FALSE(ui.shimmering());
    render(ui, 2000);
    ui.update(m, 2000 + ui.minFrameIntervalMs);
    TEST_ASSERT_FALSE(ui.needsRedraw(2000 + ui.minFrameIntervalMs));
}

void test_esp32_tempo_ends_with_the_whole_frame() {
    // In the simulator's ESP32 tempo the bands appear one after another (a digit can be half old,
    // half new for a few ms, as on the real panel); once all bands are sent the picture is complete
    sim::Esp32Tempo tempo;
    tempo.setSpiHz(27000000);
    RoundUi ui;
    ui.update(normal(94.0f), 1000);
    tempo.render(ui, 1000);
    tempo.present(2000);
    ui.update(normal(94.1f), 2000);
    tempo.render(ui, 2000);

    Collector expected;
    ui.render(bands, 2, 2000, [&](lgfx::LGFX_Sprite& band, const int top) { expected(band, top); });
    tempo.present(2000 + static_cast<uint32_t>(tempo.lastFrameMs) + 1);
    TEST_ASSERT_EQUAL_MEMORY(expected.screen.getBuffer(), tempo.panel().getBuffer(), 240 * 240 * 2);
}

// --- found by the visual check of 01.10.2026 (round-display-ui-check) ------------------------

namespace {
    std::vector<std::string> gDrawnTexts;

    void collectText(const Font*, const char* text, float, float, Align) {
        gDrawnTexts.emplace_back(text);
    }

    /** Texts of a frame, in drawing order */
    std::vector<std::string> textsOf(RoundUi& ui, const uint32_t now) {
        gDrawnTexts.clear();
        Painter::textObserver = collectText;
        ui.render(bands, 2, now, [](lgfx::LGFX_Sprite&, int) {});
        Painter::textObserver = nullptr;
        return gDrawnTexts;
    }

    bool contains(const std::vector<std::string>& texts, const char* text) {
        return std::find(texts.begin(), texts.end(), text) != texts.end();
    }

    std::string lastOf(const std::vector<std::string>& texts) {
        return texts.empty() ? std::string() : texts.back();
    }
} // namespace

void test_label_shows_the_real_state_while_the_number_counts_up() {
    // Waking up hot (after steam): the number rolls up from 0, the label must still say cooling
    RoundUi ui;
    ui.update(normal(120.0f), 1000);
    render(ui, 1000);
    ui.play(Animation::Reveal, 2000);
    ui.update(normal(120.0f), 2300);
    const auto texts = textsOf(ui, 2300);
    TEST_ASSERT_TRUE(contains(texts, "KÜHLT AB"));
    TEST_ASSERT_FALSE(contains(texts, "HEIZT"));
}

void test_long_host_name_breaks_after_a_separator() {
    RoundUi ui;
    ui.showMessage({"IP-ADRESSE", "kaffeemaschine-orione3000.local", "192.168.178.142"});
    ui.update(Model(), 1000);
    const auto texts = textsOf(ui, 1000);
    TEST_ASSERT_TRUE(contains(texts, "kaffeemaschine-"));
    TEST_ASSERT_TRUE(contains(texts, "orione3000.local"));
}

void test_title_in_a_narrow_row_stays_whole_and_cut_text_ends_with_dots() {
    RoundUi ui;
    ui.showMessage({"KALIBRIERUNG", "Bitte das bekannte Gewicht auflegen", "und zehn Sekunden warten, bis", "die Messung abgeschlossen ist 500.00g"});
    ui.update(Model(), 1000);
    const auto texts = textsOf(ui, 1000);
    TEST_ASSERT_TRUE(contains(texts, "KALIBRIERUNG"));
    const std::string last = lastOf(texts);
    TEST_ASSERT_TRUE_MESSAGE(last.size() > 3 && last.compare(last.size() - 3, 3, "...") == 0, last.c_str());
}

int main() {
    if (!RoundUi::begin()) {
        return 1;
    }

    for (auto& b : bands) {
        b.setColorDepth(16);
        b.createSprite(240, 40);
    }

    UNITY_BEGIN();
    RUN_TEST(test_band_filter_sends_new_and_changed_bands);
    RUN_TEST(test_band_by_band_gives_the_same_picture);
    RUN_TEST(test_all_bands_of_a_frame_show_the_same_moment);
    RUN_TEST(test_aborted_frame_starts_over);
    RUN_TEST(test_shimmer_runs_at_animation_pace_during_a_shot);
    RUN_TEST(test_esp32_tempo_ends_with_the_whole_frame);
    RUN_TEST(test_label_shows_the_real_state_while_the_number_counts_up);
    RUN_TEST(test_long_host_name_breaks_after_a_separator);
    RUN_TEST(test_title_in_a_narrow_row_stays_whole_and_cut_text_ends_with_dots);
    RUN_TEST(test_small_changes_send_few_bands);
    RUN_TEST(test_modes_select_their_screens);
    RUN_TEST(test_alarms_win_over_brew_timer_and_messages);
    RUN_TEST(test_brew_timer_keeps_brew_screen_after_the_shot);
    RUN_TEST(test_message_shows_until_cleared);
    RUN_TEST(test_heating_until_close_to_the_setpoint);
    RUN_TEST(test_dip_after_a_shot_stays_on_the_ready_gauge);
    RUN_TEST(test_standby_forgets_the_warm_up);
    RUN_TEST(test_ready_label_with_hysteresis);
    RUN_TEST(test_first_frame_is_always_drawn);
    RUN_TEST(test_no_redraw_without_visible_change);
    RUN_TEST(test_redraw_when_a_shown_value_changes);
    RUN_TEST(test_refresh_after_max_interval);
    RUN_TEST(test_new_message_text_in_the_same_buffer_redraws);
    RUN_TEST(test_alarm_ring_blinks);
    RUN_TEST(test_invalidate_forces_a_redraw);
    RUN_TEST(test_no_transition_on_the_very_first_frame);
    RUN_TEST(test_intro_runs_for_its_length);
    RUN_TEST(test_reveal_after_the_boot_messages);
    RUN_TEST(test_close_into_standby_and_reveal_out_of_it);
    RUN_TEST(test_alarm_stops_a_transition);
    RUN_TEST(test_transitions_draw_at_frame_rate);
    RUN_TEST(test_power_sequence_on_the_real_ui);
    RUN_TEST(test_trend_follows_the_temperature);
    RUN_TEST(test_trend_ignores_small_wobbles);
    RUN_TEST(test_ready_moment_plays_once_after_warming_up);
    RUN_TEST(test_no_ready_moment_when_already_warm);
    RUN_TEST(test_ready_moment_frames_are_drawn);
    RUN_TEST(test_shot_average_and_history);
    RUN_TEST(test_shot_history_keeps_the_last_five);
    return UNITY_END();
}
