/**
 * @file RoundDisplayPaint.h
 *
 * @brief Anti-aliased drawing into one horizontal band of the screen.
 *
 * The ESP32 has no RAM for a 240x240x16 bit frame buffer next to WiFi and BLE,
 * so every frame is drawn in bands (e.g. 240x40 pixels) that are sent to the
 * panel one after another. The Painter hides that: all coordinates are screen
 * coordinates and everything outside the current band is clipped.
 */

#pragma once

#include "RoundDisplayFont.h"

#include <LovyanGFX.hpp>
#include <cstdint>

namespace rd {

    /** Colors are 0xRRGGBB. Keep the type uint32_t: LovyanGFX treats int as RGB565. */
    using Color = uint32_t;

    constexpr Color rgb(const uint8_t r, const uint8_t g, const uint8_t b) {
        return static_cast<Color>(r) << 16 | static_cast<Color>(g) << 8 | b;
    }

    Color mix(Color a, Color b, float t);

    enum class Align : uint8_t {
        Left,
        Center,
        Right,
    };

    /**
     * What a drawing belongs to: the frame (ring, ticks, markers, status symbols at the ring) or the
     * content inside it (texts, icons). Only the layout tests look at this: they draw one layer at a
     * time to measure the spacing between content and frame.
     */
    enum class Layer : uint8_t {
        Frame = 1,
        Content = 2,
    };

    class Painter {
        public:
            /** Layers that are drawn at all (test hook; normally both) */
            static inline uint8_t drawnLayers = 0xFF;

            /** Called for every text that is drawn (test hook for the layout checks) */
            using TextObserver = void (*)(const Font* font, const char* text, float x, float y, Align align);
            static inline TextObserver textObserver = nullptr;

            /**
             * @param band 16 bit sprite, full screen width
             * @param top  screen row that corresponds to the first row of the sprite
             */
            Painter(lgfx::LGFX_Sprite& band, int top);

            int top() const {
                return top_;
            }

            int bottom() const {
                return top_ + height_;
            }

            bool visible(const float y0, const float y1) const {
                return y1 >= static_cast<float>(top_) && y0 < static_cast<float>(top_ + height_);
            }

            void clear(Color c);

            void setLayer(const Layer layer) {
                layer_ = layer;
            }

            Layer layer() const {
                return layer_;
            }

            /**
             * Ring segment. Angles in degrees, clockwise, 0 = 12 o'clock; a1 > a0.
             * A sweep of 360 degrees or more draws a closed ring.
             */
            void arc(float cx, float cy, float radius, float width, float a0, float a1, Color c, bool roundCaps = true);

            /** Ring segment whose color runs from c0 at a0 to c1 at a1. */
            void arcGradient(float cx, float cy, float radius, float width, float a0, float a1, Color c0, Color c1, bool roundCaps = true);

            void circle(float cx, float cy, float radius, float width, Color c);
            void disc(float cx, float cy, float radius, Color c);

            /** Rectangle with rounded corners (anti-aliased edges) */
            void roundRect(float x0, float y0, float x1, float y1, float radius, Color c);

            /** Whole pixels x0..x1-1, y0..y1-1 without anti-aliasing (QR modules) */
            void block(int x0, int y0, int x1, int y1, Color c);
            void line(float x0, float y0, float x1, float y1, float width, Color c);

            /** Radial line from r0 to r1 at the given angle. */
            void tick(float cx, float cy, float angle, float r0, float r1, float width, Color c);

            /**
             * Darkens everything outside a circle (for iris transitions). Pixels between radius
             * and radius + feather fade out smoothly.
             */
            void mask(float cx, float cy, float radius, float feather);

            /** Text with its baseline at y, x rounded to whole pixels (glyphs stay as crisp as rendered). */
            void text(const Font* font, const char* s, float x, float y, Color c, Align align = Align::Center);
            int textWidth(const Font* font, const char* s);

            /** Point on a circle, angle as for arc(). */
            static float px(float cx, float radius, float angle);
            static float py(float cy, float radius, float angle);

        private:
            /** Ring segment; Gradient: shade() gets the position 0..1 along the arc (costs an angle per pixel) */
            template <bool Gradient, typename Shade>
            void shadeArc(float cx, float cy, float radius, float width, float a0, float a1, bool roundCaps, Shade shade);

            template <typename Distance>
            void fillShape(float x0, float y0, float x1, float y1, Color c, Distance distance);

            void blend(int x, int y, Color c, float alpha);

            /** Blends c over the pixel; a = 0..256 */
            void blendFixed(int x, int y, Color c, int a);

            bool drawing() const {
                return (drawnLayers & static_cast<uint8_t>(layer_)) != 0;
            }

            lgfx::LGFX_Sprite& band_;
            Layer layer_ = Layer::Content;
            uint16_t* buffer_;
            int top_;
            int width_;
            int height_;
    };

} // namespace rd

namespace rd {

    /** Switches a painter to a layer for the lifetime of the object */
    class LayerScope {
        public:
            LayerScope(Painter& p, const Layer layer) :
                p_(p), previous_(p.layer()) {
                p_.setLayer(layer);
            }

            ~LayerScope() {
                p_.setLayer(previous_);
            }

            LayerScope(const LayerScope&) = delete;
            LayerScope& operator=(const LayerScope&) = delete;

        private:
            Painter& p_;
            Layer previous_;
    };

} // namespace rd
