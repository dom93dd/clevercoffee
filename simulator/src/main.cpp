/**
 * @file main.cpp
 *
 * @brief Desktop simulator for the round display UI (lib/RoundDisplay).
 *
 * The UI code is exactly the one that runs on the ESP32: it draws band by band into
 * 16 bit sprites; here the bands go into an in-memory "panel" instead of the GC9A01.
 *
 *   roundsim                       interactive window (keys: see side panel)
 *   roundsim --shot ready out.png  one screen as PNG, no window
 *   roundsim --gallery out.png     all screens on one sheet
 *   options: --scale N, --lang de|en, --list
 */

#include <LovyanGFX.hpp>
#include <RoundDisplayControl.h>
#include <RoundDisplayFonts.h>
#include <RoundDisplayUi.h>

#include <SDL.h>

#include "FakeMachine.h"
#include "Png.h"
#include "SimSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

using namespace sim;

namespace {

    constexpr int kBezel = 26;          // black frame around the glass, in display pixels
    constexpr int kMargin = 14;         // steel around the frame
    constexpr int kPanelWidth = 390;

    const char* gBrand = kDefaultBrand; // --brand

    // -------------------------------------------------------------------------------------------
    // RGB image helpers

    struct Image {
            int w = 0;
            int h = 0;
            std::vector<uint8_t> rgb;

            Image(const int width, const int height, const uint32_t fill = 0) :
                w(width), h(height), rgb(static_cast<size_t>(width) * height * 3) {
                for (size_t i = 0; i < rgb.size(); i += 3) {
                    rgb[i] = fill >> 16 & 0xFF;
                    rgb[i + 1] = fill >> 8 & 0xFF;
                    rgb[i + 2] = fill & 0xFF;
                }
            }

            uint8_t* at(const int x, const int y) {
                return &rgb[(static_cast<size_t>(y) * w + x) * 3];
            }
    };

    void spritePixel(const lgfx::LGFX_Sprite& s, const int x, const int y, uint8_t out[3]) {
        const uint16_t raw = static_cast<const uint16_t*>(s.getBuffer())[y * s.width() + x];
        const uint16_t v = static_cast<uint16_t>(raw >> 8 | raw << 8);
        const int r = v >> 11 & 0x1F;
        const int g = v >> 5 & 0x3F;
        const int b = v & 0x1F;
        out[0] = static_cast<uint8_t>(r << 3 | r >> 2);
        out[1] = static_cast<uint8_t>(g << 2 | g >> 4);
        out[2] = static_cast<uint8_t>(b << 3 | b >> 2);
    }

    void blend(uint8_t* dst, const uint8_t* src, const float a) {
        for (int i = 0; i < 3; ++i) {
            dst[i] = static_cast<uint8_t>(dst[i] + (src[i] - dst[i]) * a + 0.5f);
        }
    }

    float clamp01(const float v) {
        return v < 0 ? 0 : (v > 1 ? 1 : v);
    }

    /**
     * Draws the display as it would sit in the machine front: brushed steel, black frame,
     * round glass. Pixels are scaled up without smoothing, the round edge is anti-aliased.
     */
    void drawDevice(Image& img, const int ox, const int oy, const lgfx::LGFX_Sprite& screen, const int scale) {
        const float r = kDisplay * 0.5f;
        const float size = static_cast<float>(kDisplay + 2 * (kBezel + kMargin));
        const float cx = size * 0.5f;

        for (int y = 0; y < static_cast<int>(size) * scale; ++y) {
            for (int x = 0; x < static_cast<int>(size) * scale; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(scale);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(scale);
                const float d = std::hypot(u - cx, v - cx);
                const float px = 1.0f / static_cast<float>(scale);

                // Steel with a soft vertical gradient
                const auto steel = static_cast<uint8_t>(196 - 40 * v / size);
                uint8_t c[3] = {steel, steel, static_cast<uint8_t>(steel + 4)};

                // Frame
                const float frameCover = clamp01((r + kBezel - d) / px + 0.5f);
                const uint8_t frame[3] = {14, 14, 15};
                blend(c, frame, frameCover);

                // Glass
                const float glassCover = clamp01((r - d) / px + 0.5f);

                if (glassCover > 0.0f) {
                    const int sx = std::min(kDisplay - 1, std::max(0, static_cast<int>(u - (cx - r))));
                    const int sy = std::min(kDisplay - 1, std::max(0, static_cast<int>(v - (cx - r))));
                    uint8_t p[3];
                    spritePixel(screen, sx, sy, p);
                    blend(c, p, glassCover);
                }

                const int tx = ox + x;
                const int ty = oy + y;

                if (tx >= 0 && ty >= 0 && tx < img.w && ty < img.h) {
                    std::memcpy(img.at(tx, ty), c, 3);
                }
            }
        }
    }

    int deviceSize(const int scale) {
        return (kDisplay + 2 * (kBezel + kMargin)) * scale;
    }

    void blitSprite(Image& img, const int ox, const int oy, const lgfx::LGFX_Sprite& s) {
        for (int y = 0; y < s.height(); ++y) {
            for (int x = 0; x < s.width(); ++x) {
                if (ox + x < img.w && oy + y < img.h) {
                    spritePixel(s, x, y, img.at(ox + x, oy + y));
                }
            }
        }
    }

    int runShot(const char* name, const char* out, const int scale, const rd::Language lang, const int atMs) {
        const auto all = scenarios();
        const Scenario* sc = findScenario(all, name);

        if (sc == nullptr) {
            fprintf(stderr, "unknown scenario '%s', see --list\n", name);
            return 1;
        }

        Scenario scenario = *sc;

        if (atMs >= 0) {
            scenario.atMs = static_cast<uint32_t>(atMs);
        }

        Panel panel;
        renderScenario(scenario, panel, lang, gBrand);
        Image img(deviceSize(scale), deviceSize(scale));
        drawDevice(img, 0, 0, panel.screen, scale);
        return png::write(out, img.w, img.h, img.rgb.data()) ? 0 : 1;
    }

    int runGallery(const char* out, const int scale, const rd::Language lang) {
        const auto all = scenarios();
        const int cols = 5;
        const int rows = (static_cast<int>(all.size()) + cols - 1) / cols;
        const int tile = deviceSize(scale);
        const int captionHeight = 30;
        Image img(cols * tile, rows * (tile + captionHeight), 0xC4C4C8);

        Panel panel;
        lgfx::LGFX_Sprite caption;
        caption.setColorDepth(16);
        caption.createSprite(tile, captionHeight);

        for (size_t i = 0; i < all.size(); ++i) {
            const int x = static_cast<int>(i % cols) * tile;
            const int y = static_cast<int>(i / cols) * (tile + captionHeight);
            renderScenario(all[i], panel, lang, gBrand);
            drawDevice(img, x, y, panel.screen, scale);

            caption.fillScreen(rd::rgb(196, 196, 200));
            rd::Painter p(caption, 0);
            char text[96];
            snprintf(text, sizeof(text), "%s  (%s)", all[i].caption, all[i].name);
            p.text(rd::fonts::text(), text, tile * 0.5f, 21.0f, rd::rgb(30, 30, 34));
            blitSprite(img, x, y + tile, caption);
        }

        return png::write(out, img.w, img.h, img.rgb.data()) ? 0 : 1;
    }

    /**
     * Plays a fixed sequence (cold start, heat up, shot, steam, water tank) without a window and
     * prints every screen change - a quick check of transitions and hysteresis.
     */
    int runTrace() {
        FakeMachine machine;
        machine.reset();
        rd::RoundUi ui;
        rd::Screen last = rd::Screen::Message;
        const float dt = 0.05f;
        int step = 0;

        for (float t = 0.0f; t < 400.0f; t += dt, ++step) {
            // Events at fixed times
            if (step == static_cast<int>(150.0f / dt)) machine.toggleBrewSwitch();
            if (step == static_cast<int>(185.0f / dt)) machine.toggleBrewSwitch();
            if (step == static_cast<int>(240.0f / dt)) machine.toggleSteam();
            if (step == static_cast<int>(330.0f / dt)) machine.toggleSteam();
            if (step == static_cast<int>(380.0f / dt)) machine.toggleWaterEmpty();

            machine.step(dt);
            const rd::Model m = machine.model();
            ui.update(m, static_cast<uint32_t>(t * 1000.0f));

            if (ui.screen() != last || step % static_cast<int>(10.0f / dt) == 0) {
                printf("%6.1f s  %-15s  T %6.2f  Soll %5.1f  Heizung %5.1f %%  Bezug %4.1f s\n", t, rd::screenName(ui.screen()), m.temperature, m.setpoint, m.heaterPercent, m.brewTime);
                last = ui.screen();
            }
        }

        return 0;
    }

    // -------------------------------------------------------------------------------------------
    // Interactive window

    struct KeyHelp {
            const char* key;
            const char* text;
    };

    const KeyHelp kKeys[] = {
        {"Leertaste", "Bezugsschalter an/aus"},
        {"G", "Waage an/aus"},
        {"S", "Dampfschalter"},
        {"F / H", "Spülen / Heißwasser"},
        {"B", "Rückspülmodus"},
        {"W", "Wassertank leer"},
        {"E", "Sensorfehler (TSIC)"},
        {"O", "Übertemperatur 146 °C"},
        {"P / Y", "PID aus / Standby"},
        {"D", "Display aus/an (Blende)"},
        {"N", "WLAN-Zustand wechseln"},
        {"L", "Sprache DE/EN"},
        {"Pfeil hoch/runter", "Soll +/- 0,5 °C"},
        {"Pfeil links/rechts", "Temperatur -/+ 1 K"},
        {"T", "Zeitraffer 1x / 5x / 20x"},
        {"R", "Neustart (kalt)"},
        {"C", "Screenshot speichern"},
        {"Q / Esc", "Beenden"},
    };

    void drawSidePanel(lgfx::LGFX_Sprite& s, const FakeMachine& m, const rd::RoundUi& ui, const float speed, const float fps, const float drawMs) {
        s.fillScreen(rd::rgb(24, 24, 27));
        rd::Painter p(s, 0);
        const rd::Color head = rd::rgb(240, 180, 92);
        const rd::Color text = rd::rgb(220, 220, 224);
        const rd::Color dim = rd::rgb(140, 140, 148);
        float y = 34;
        char buf[96];

        p.text(rd::fonts::label(), "SIMULATOR", 18, y, head, rd::Align::Left);
        y += 32;

        const rd::Model model = m.model();
        snprintf(buf, sizeof(buf), "Screen: %s", rd::screenName(ui.screen()));
        p.text(rd::fonts::text(), buf, 18, y, text, rd::Align::Left);
        y += 24;
        snprintf(buf, sizeof(buf), "Temp %.1f  Soll %.1f  Heizung %.0f %%", model.temperature, model.setpoint, model.heaterPercent);
        p.text(rd::fonts::text(), buf, 18, y, text, rd::Align::Left);
        y += 24;
        snprintf(buf, sizeof(buf), "Bezugsschalter %s  Waage %s", m.brewSwitch() ? "AN" : "aus", m.scale ? "an" : "aus");
        p.text(rd::fonts::text(), buf, 18, y, text, rd::Align::Left);
        y += 24;
        snprintf(buf, sizeof(buf), "Zeit %gx  %.1f Bilder/s  %.1f ms/Bild", speed, fps, drawMs);
        p.text(rd::fonts::text(), buf, 18, y, dim, rd::Align::Left);
        y += 40;

        p.text(rd::fonts::label(), "TASTEN", 18, y, head, rd::Align::Left);
        y += 30;

        for (const auto& k : kKeys) {
            p.text(rd::fonts::text(), k.key, 18, y, text, rd::Align::Left);
            p.text(rd::fonts::text(), k.text, 168, y, dim, rd::Align::Left);
            y += 23;
        }
    }

    int runWindow(const int scale, const rd::Language lang, const int maxFrames, const char* windowPng) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }

        const int device = deviceSize(scale);
        const int height = std::max(device, 640);
        const int width = device + kPanelWidth;

        SDL_Window* window = SDL_CreateWindow("CleverCoffee – rundes Display (GC9A01)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_ALLOW_HIGHDPI);
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);

        Panel panel;
        lgfx::LGFX_Sprite side;
        side.setColorDepth(16);
        side.createSprite(kPanelWidth, height);
        Image img(width, height, 0x18181B);

        FakeMachine machine;
        machine.language = lang;
        machine.reset();
        rd::RoundUi ui;
        ui.setBrand(gBrand);

        const float speeds[] = {1.0f, 5.0f, 20.0f};
        int speedIndex = 0;
        int wifiStep = 0;
        bool booting = true;
        uint32_t bootStart = SDL_GetTicks();
        bool displayOffRequested = false; // like u8g2->setPowerSave(1) in the firmware
        rd::PowerSequencer power;
        ui.showMessage(msgVersion);
        ui.play(rd::Animation::Intro, bootStart);
        uint32_t last = SDL_GetTicks();
        uint32_t fpsStart = last;
        int frames = 0;
        float fps = 0;
        float drawMs = 0;
        bool running = true;
        int shown = 0;

        while (running && (maxFrames <= 0 || shown++ < maxFrames)) {
            SDL_Event e;

            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    running = false;
                }
                else if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE:
                        case SDLK_q:
                            running = false;
                            break;
                        case SDLK_SPACE:
                            machine.toggleBrewSwitch();
                            break;
                        case SDLK_g:
                            machine.scale = !machine.scale;
                            break;
                        case SDLK_s:
                            machine.toggleSteam();
                            break;
                        case SDLK_f:
                            machine.toggleFlush();
                            break;
                        case SDLK_h:
                            machine.toggleHotWater();
                            break;
                        case SDLK_b:
                            machine.toggleBackflush();
                            break;
                        case SDLK_w:
                            machine.toggleWaterEmpty();
                            break;
                        case SDLK_e:
                            machine.toggleSensorError();
                            break;
                        case SDLK_o:
                            machine.triggerOvertemperature();
                            break;
                        case SDLK_p:
                            machine.togglePid();
                            break;
                        case SDLK_y:
                        case SDLK_z:
                            machine.toggleStandby();
                            break;
                        case SDLK_n:
                            wifiStep = (wifiStep + 1) % 4;
                            machine.wifiConnected = wifiStep < 2;
                            machine.wifiBars = wifiStep == 0 ? 3 : 1;
                            machine.offline = wifiStep == 3;
                            break;
                        case SDLK_l:
                            machine.language = machine.language == rd::Language::German ? rd::Language::English : rd::Language::German;
                            break;
                        case SDLK_UP:
                            machine.brewSetpoint += 0.5f;
                            break;
                        case SDLK_DOWN:
                            machine.brewSetpoint -= 0.5f;
                            break;
                        case SDLK_LEFT:
                            machine.nudgeTemperature(-1.0f);
                            break;
                        case SDLK_RIGHT:
                            machine.nudgeTemperature(1.0f);
                            break;
                        case SDLK_t:
                            speedIndex = (speedIndex + 1) % 3;
                            break;
                        case SDLK_r:
                            machine.reset();
                            booting = true;
                            bootStart = SDL_GetTicks();
                            ui.showMessage(msgVersion);
                            ui.play(rd::Animation::Intro, bootStart);
                            break;
                        case SDLK_d:
                            displayOffRequested = !displayOffRequested;
                            break;
                        case SDLK_c:
                            {
                                char name[64];
                                const std::time_t t = std::time(nullptr);
                                std::strftime(name, sizeof(name), "round-display-%Y%m%d-%H%M%S.png", std::localtime(&t));
                                Image shot(deviceSize(scale), deviceSize(scale));
                                drawDevice(shot, 0, 0, panel.screen, scale);
                                png::write(name, shot.w, shot.h, shot.rgb.data());
                                printf("Screenshot: %s\n", name);
                                break;
                            }
                        default:
                            break;
                    }
                }
            }

            const uint32_t now = SDL_GetTicks();
            const float dt = static_cast<float>(now - last) / 1000.0f * speeds[speedIndex];
            last = now;

            // Boot sequence as in setup(): version, WiFi, IP address, then the machine
            if (booting) {
                const uint32_t since = now - bootStart;

                if (since < 3200) {
                    ui.showMessage(msgVersion);
                }
                else if (since < 4700) {
                    ui.showMessage(msgWifi);
                }
                else if (since < 6200) {
                    ui.showMessage(msgIp);
                }
                else {
                    ui.clearMessage();
                    booting = false;
                }
            }
            else {
                for (float left = dt; left > 0.0f; left -= 0.05f) {
                    machine.step(std::min(left, 0.05f));
                }
            }

            ui.update(machine.model(), now);

            // Same sequence as roundDisplayLoop() in the firmware
            if (power.update(displayOffRequested, ui, now) == rd::PowerSequencer::Action::Sleep) {
                panel.screen.fillScreen(rd::rgb(0, 0, 0)); // panel asleep: dark
            }

            if (!power.asleep() && ui.needsRedraw(now)) {
                const auto t0 = std::chrono::steady_clock::now();
                panel.render(ui, now);
                const auto t1 = std::chrono::steady_clock::now();
                drawMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
                ++frames;
            }

            if (now - fpsStart >= 1000) {
                fps = static_cast<float>(frames) * 1000.0f / static_cast<float>(now - fpsStart);
                frames = 0;
                fpsStart = now;
            }

            drawDevice(img, 0, (height - device) / 2, panel.screen, scale);
            drawSidePanel(side, machine, ui, speeds[speedIndex], fps, drawMs);
            blitSprite(img, device, 0, side);

            SDL_UpdateTexture(texture, nullptr, img.rgb.data(), img.w * 3);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        if (windowPng != nullptr) {
            png::write(windowPng, img.w, img.h, img.rgb.data());
        }

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    int scale = 2;
    auto lang = rd::Language::German;
    const char* shot = nullptr;
    const char* shotOut = nullptr;
    const char* gallery = nullptr;
    int frames = 0;
    const char* windowPng = nullptr;
    int atMs = -1;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = std::max(1, std::atoi(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            lang = std::strcmp(argv[++i], "en") == 0 ? rd::Language::English : rd::Language::German;
        }
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 2 < argc) {
            shot = argv[++i];
            shotOut = argv[++i];
        }
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]); // quit after this many window frames (smoke test)
        }
        else if (std::strcmp(argv[i], "--brand") == 0 && i + 1 < argc) {
            gBrand = argv[++i];            // name in the intro, e.g. --brand "CAFFÈ DOMINIK"
        }
        else if (std::strcmp(argv[i], "--at") == 0 && i + 1 < argc) {
            atMs = std::atoi(argv[++i]);   // with --shot: time in ms inside a transition
        }
        else if (std::strcmp(argv[i], "--window-png") == 0 && i + 1 < argc) {
            windowPng = argv[++i];         // save the last window picture when quitting
        }
        else if (std::strcmp(argv[i], "--gallery") == 0 && i + 1 < argc) {
            gallery = argv[++i];
        }
        else if (std::strcmp(argv[i], "--trace") == 0) {
            return runTrace();
        }
        else if (std::strcmp(argv[i], "--list") == 0) {
            for (const auto& s : scenarios()) {
                printf("%-16s %s\n", s.name, s.caption);
            }
            return 0;
        }
        else {
            fprintf(stderr, "usage: %s [--scale N] [--lang de|en] [--frames N] [--brand NAME] [--shot NAME OUT.png | --gallery OUT.png | --list]\n", argv[0]);
            return 1;
        }
    }

    if (!rd::RoundUi::begin()) {
        fprintf(stderr, "fonts could not be loaded\n");
        return 1;
    }

    if (shot != nullptr) {
        return runShot(shot, shotOut, scale, lang, atMs);
    }

    if (gallery != nullptr) {
        return runGallery(gallery, scale, lang);
    }

    return runWindow(scale, lang, frames, windowPng);
}
