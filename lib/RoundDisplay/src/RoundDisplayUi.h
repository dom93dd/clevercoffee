/**
 * @file RoundDisplayUi.h
 *
 * @brief UI for a round 240x240 display (GC9A01): which screen to show and how to draw it.
 *
 * Usage per loop iteration:
 *   ui.update(model, now);
 *   if (ui.needsRedraw(now)) { ui.render(bands, 2, now, push); }
 *
 * Drawing is band by band (see RoundDisplayPaint.h), so the same code runs on the
 * ESP32 without a frame buffer and in the desktop simulator.
 */

#pragma once

#include "RoundDisplayModel.h"
#include "RoundDisplayPaint.h"

namespace rd {

    enum class Screen : uint8_t {
        Boot,
        Message,
        Heating,
        Ready,
        Brew,
        Flush,
        HotWater,
        Steam,
        Backflush,
        WaterTankEmpty,
        Standby,
        PidDisabled,
        EmergencyStop,
        SensorError,
    };

    const char* screenName(Screen s);

    /**
     * Transitions. Intro plays at power-on, Reveal opens an iris onto a screen (after boot and
     * when leaving standby, values count up), Close shuts the iris (entering standby, display off).
     */
    enum class Animation : uint8_t {
        None,
        Intro,
        Reveal,
        Close,
    };

    class RoundUi {
        public:
            static constexpr int kWidth = 240;
            static constexpr int kHeight = 240;

            /** Loads the fonts. Call once before the first frame. */
            static bool begin();

            /** Takes the current machine state. Call once per loop, before needsRedraw(). */
            void update(const Model& model, uint32_t nowMs);

            /** Name shown in the intro and above messages; capitals (see the label font). The text must stay valid. */
            void setBrand(const char* brand) {
                brand_ = brand;
            }

            /** Shows a message screen until clearMessage() is called (boot, WiFi setup). */
            void showMessage(const Message& message);
            void clearMessage();

            /** True if what is on screen would change (values as shown, not raw values) and the minimum frame interval passed. */
            bool needsRedraw(uint32_t nowMs) const;

            /** Starts a transition. Reveal and Close also start by themselves on screen changes (see update()). */
            void play(Animation animation, uint32_t nowMs);

            /** True while a transition runs; draw as often as possible then. */
            bool animating(uint32_t nowMs) const;

            /** Forces a full redraw, e.g. after the panel woke up from sleep. */
            void invalidate() {
                drawnOnce_ = false;
            }

            Screen screen() const {
                return screen_;
            }

            /** Temperature counts as ready (green label), with hysteresis */
            bool ready() const {
                return ready_;
            }

            /** Draws the part of the current screen that falls into the painter's band. */
            void draw(Painter& p, uint32_t nowMs) const;

            /**
             * Draws a whole frame band by band.
             * @param bands     one sprite, or two to let the CPU draw the next band while DMA sends the last one
             * @param push      callable (lgfx::LGFX_Sprite& band, int top) that sends a band to the panel
             */
            template <typename Push>
            void render(lgfx::LGFX_Sprite* bands, const int bandCount, const uint32_t nowMs, Push push) {
                const int bandHeight = bands[0].height();

                for (int top = 0, i = 0; top < kHeight; top += bandHeight, ++i) {
                    lgfx::LGFX_Sprite& band = bands[i % bandCount];
                    Painter p(band, top);
                    draw(p, nowMs);
                    push(band, top);
                }

                drawnSignature_ = signature_;
                lastDrawMs_ = nowMs;
                drawnOnce_ = true;
            }

            uint32_t minFrameIntervalMs = 80;
            uint32_t maxFrameIntervalMs = 10000;
            uint32_t animationFrameIntervalMs = 30;

        private:
            Screen selectScreen(const Model& m) const;
            uint32_t computeSignature(uint32_t nowMs) const;
            float animationProgress(uint32_t nowMs) const;

            void drawScreen(Painter& p, Screen screen, uint32_t nowMs) const;
            void drawIntro(Painter& p, float t) const;
            void drawIris(Painter& p, float radius, float glow) const;

            void drawBoot(Painter& p) const;
            void drawMessage(Painter& p) const;
            void drawTemperatureGauge(Painter& p, bool heating) const;
            void drawSteam(Painter& p) const;
            void drawBrew(Painter& p) const;
            void drawBrewLabel(Painter& p, bool done) const;
            void drawBrewWeightRing(Painter& p, bool done) const;
            void drawStopwatch(Painter& p, const char* label, float seconds) const;
            void drawBackflush(Painter& p) const;
            void drawWaterTankEmpty(Painter& p) const;
            void drawStandby(Painter& p) const;
            void drawPidDisabled(Painter& p) const;
            void drawAlarm(Painter& p, bool sensorError, uint32_t nowMs) const;

            bool drawConnectionHint(Painter& p, float y) const;
            void drawHeaterBar(Painter& p) const;
            void drawBigValue(Painter& p, float value, float y, Color c, bool degree, const char* unit) const;
            void drawTemperatureRow(Painter& p, float y) const;

            const char* brand_ = "CLEVERCOFFEE";
            Model model_;
            mutable Model view_; // model as drawn; differs from model_ while values count up
            Message message_;
            bool hasMessage_ = false;
            Screen screen_ = Screen::Boot;
            bool heating_ = true;   // hysteresis for Heating <-> Ready
            bool warmedUp_ = false; // reached the setpoint since the last cold start
            bool ready_ = false;    // hysteresis for the ready label
            uint32_t signature_ = 0;
            uint32_t drawnSignature_ = 0;
            uint32_t lastDrawMs_ = 0;
            bool drawnOnce_ = false;

            Animation animation_ = Animation::None;
            uint32_t animationStart_ = 0;
            Screen closingScreen_ = Screen::Boot; // what the iris closes over
    };

} // namespace rd
