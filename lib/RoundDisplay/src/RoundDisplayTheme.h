/**
 * @file RoundDisplayTheme.h
 *
 * @brief Colors and geometry of the round display UI (GC9A01, 240x240).
 */

#pragma once

#include "RoundDisplayPaint.h"

namespace rd::theme {

    // Geometry
    constexpr int kSize = 240;
    constexpr float kCx = 120.0f;
    constexpr float kCy = 120.0f;
    constexpr float kRingRadius = 111.0f;  // center line of the outer ring
    constexpr float kRingWidth = 9.0f;
    constexpr float kGaugeStart = -135.0f; // 270 degree gauge, open at the bottom
    constexpr float kGaugeEnd = 135.0f;

    // Vertical layout (baselines). Content keeps 8 px to the ring, ticks and status symbols,
    // 12 px between the big digits and the rows below (simulator/test/test_layout checks it)
    constexpr float kLabelY = 64.0f;
    constexpr float kValueY = 139.0f;
    constexpr float kRowAY = 172.0f;
    constexpr float kRowBY = 197.0f;
    constexpr float kSecondNumberY = 177.0f; // second number under the big one (brewing with the scale)

    // Colors
    constexpr Color kBackground = rgb(0, 0, 0);
    constexpr Color kTrack = rgb(34, 34, 36);
    constexpr Color kTickMinor = rgb(78, 78, 82);
    constexpr Color kTickMajor = rgb(210, 210, 214);
    constexpr Color kText = rgb(242, 242, 244);
    constexpr Color kTextDim = rgb(142, 142, 148);
    constexpr Color kTextFaint = rgb(86, 86, 92);

    constexpr Color kHeat = rgb(255, 146, 38);
    constexpr Color kHeatDark = rgb(150, 52, 8);
    constexpr Color kReady = rgb(52, 211, 153);
    constexpr Color kCool = rgb(125, 190, 255);
    constexpr Color kBrew = rgb(240, 180, 92);
    constexpr Color kBrewDark = rgb(110, 62, 24);
    constexpr Color kWeight = rgb(236, 226, 208);
    constexpr Color kSteam = rgb(160, 216, 255);
    constexpr Color kWater = rgb(56, 170, 255);
    constexpr Color kAlarm = rgb(239, 68, 68);
    constexpr Color kAlarmDark = rgb(90, 18, 18);

} // namespace rd::theme
