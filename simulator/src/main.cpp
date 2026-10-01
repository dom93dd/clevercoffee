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
 *   roundsim --spi 27              window at the speed of the ESP32 (SPI 27/40/80 MHz; key X switches)
 *   roundsim --tempo               pictures per second and loop() blocking at ESP32 speed, without window
 *   roundsim --bench               drawing time per scenario here (compare: esp32-bench)
 *   roundsim --inspect DIR         sheets of all screens, animations, sequences and limits to look at
 *   roundsim --review DIR          every screen in German and English plus DIR/index.html to sign them off
 *            [--compare OLD]       marks screens that differ from an earlier review in OLD (copied to DIR/previous)
 *            [--notes FILE]        JSON {"scenario": "what was changed"}, shown on the screens
 *   options: --scale N, --lang de|en, --list
 */

#include <LovyanGFX.hpp>
#include <RoundDisplayControl.h>
#include <RoundDisplayFonts.h>
#include <RoundDisplayUi.h>

#include <SDL.h>
#include <zlib.h>

#include "Esp32Tempo.h"
#include "FakeMachine.h"
#include "Png.h"
#include "ReviewPage.h"
#include "SimSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <limits>
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

    /** Every scenario in German and English as PNG files plus an index.html to go through them */
    std::string readFile(const char* path) {
        std::string out;
        FILE* f = std::fopen(path, "rb");

        if (f != nullptr) {
            char buf[4096];
            size_t n = 0;

            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
                out.append(buf, n);
            }

            std::fclose(f);
        }

        return out;
    }

    /** More than a few pixels changed clearly (ignores the slightly different edge shades of a font update) */
    bool visiblyDifferent(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        int count = 0;

        for (size_t i = 0; i + 2 < a.size() && i + 2 < b.size(); i += 3) {
            int d = 0;

            for (int c = 0; c < 3; ++c) {
                d = std::max(d, std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c])));
            }

            count += d > 24 ? 1 : 0;
        }

        return count > 40;
    }

    int runReview(const char* dir, const int scale, const char* compare, const char* notesFile) {
        std::error_code error;
        std::filesystem::create_directories(dir, error);

        if (error) {
            fprintf(stderr, "cannot create %s: %s\n", dir, error.message().c_str());
            return 1;
        }

        const auto all = scenarios();
        const int size = deviceSize(scale);
        Panel panel;
        std::string list = "[\n";

        int changedCount = 0;

        for (size_t i = 0; i < all.size(); ++i) {
            char file[64];
            snprintf(file, sizeof(file), "%02d-%s", static_cast<int>(i + 1), all[i].name);
            bool changed = false;

            for (const auto lang : {rd::Language::German, rd::Language::English}) {
                renderScenario(all[i], panel, lang, gBrand);
                Image img(size, size, 0xFFFFFF);
                drawDevice(img, 0, 0, panel.screen, scale);
                const std::string name = std::string(file) + (lang == rd::Language::German ? "-de.png" : "-en.png");
                const std::string path = std::string(dir) + "/" + name;

                if (compare != nullptr) {
                    // Keep the earlier picture next to the new one when they differ
                    const std::string old = std::string(compare) + "/" + name;
                    int w = 0;
                    int h = 0;
                    std::vector<uint8_t> before;

                    if (png::read(old.c_str(), w, h, before) && (w != img.w || h != img.h || visiblyDifferent(before, img.rgb))) {
                        const std::string previous = std::string(dir) + "/previous";
                        std::filesystem::create_directories(previous, error);
                        std::filesystem::copy_file(old, previous + "/" + name, std::filesystem::copy_options::overwrite_existing, error);
                        changed = true;
                    }
                }

                if (!png::write(path.c_str(), img.w, img.h, img.rgb.data())) {
                    fprintf(stderr, "cannot write %s\n", path.c_str());
                    return 1;
                }
            }

            changedCount += changed ? 1 : 0;

            // Names and captions are plain text without quotes or backslashes
            list += std::string("  {\"name\": \"") + all[i].name + "\", \"caption\": \"" + all[i].caption + "\", \"file\": \"" + file + "\", \"changed\": " + (changed ? "true" : "false") + "},\n";
        }

        list += "]";

        // Optional notes per screen: a JSON object {"scenario": "what was changed"}
        std::string notes = "{}";

        if (notesFile != nullptr) {
            notes = readFile(notesFile);

            if (notes.empty() || notes.find("</") != std::string::npos) {
                fprintf(stderr, "cannot use notes %s\n", notesFile);
                return 1;
            }
        }

        char stamp[64];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "Stand %d.%m.%Y %H:%M", std::localtime(&now));

        const std::string html = std::string(kReviewHead) + list + ";\nconst STAMP = \"" + stamp + ", " + std::to_string(all.size()) +
                                 " Screens, Branch feature/round-display\";\nconst COMPARED = " + (compare != nullptr ? "true" : "false") + ";\nconst NOTES = " + notes + kReviewTail;
        const std::string index = std::string(dir) + "/index.html";
        FILE* f = std::fopen(index.c_str(), "wb");

        if (f == nullptr || std::fwrite(html.data(), 1, html.size(), f) != html.size()) {
            fprintf(stderr, "cannot write %s\n", index.c_str());
            return 1;
        }

        std::fclose(f);
        printf("%s (%zu screens, German and English", index.c_str(), all.size());
        printf(compare != nullptr ? ", %d changed)\n" : ")\n", changedCount);
        return 0;
    }

    /**
     * Drawing time per scenario on this computer, in the format of the ESP32 bench (esp32-bench/), so both
     * can be compared: BENCH <scenario> us <frame> (fastest of many runs)
     */
    int runBench() {
        lgfx::LGFX_Sprite bands[2];

        for (auto& b : bands) {
            b.setColorDepth(16);
            b.createSprite(kDisplay, kBandHeight);
        }

        for (const auto& sc : scenarios()) {
            FakeMachine machine;
            machine.language = rd::Language::German;
            machine.reset();
            rd::RoundUi ui;
            ui.setBrand(gBrand);
            gSimulatedMs = 0;
            sc.setup(machine, ui);
            const uint32_t at = gSimulatedMs > 0 ? gSimulatedMs + sc.atMs : sc.atMs;
            ui.update(machine.model(), at);
            double best = 1e9;

            for (int r = 0; r < 200; ++r) {
                const auto start = std::chrono::steady_clock::now();
                ui.render(bands, 2, at, [](lgfx::LGFX_Sprite&, int) {});
                best = std::min(best, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
            }

            // Checksum of the band buffers as they go to the panel; esp32-bench prints the same on the chip
            uLong crc = crc32(0L, Z_NULL, 0);
            ui.render(bands, 2, at, [&crc](lgfx::LGFX_Sprite& band, int) { crc = crc32(crc, static_cast<const Bytef*>(band.getBuffer()), static_cast<uInt>(band.width() * band.height() * 2)); });
            printf("BENCH %s us %.1f crc %08lx\n", sc.name, best, crc);

            if (getenv("RD_DUMP") != nullptr && std::strcmp(getenv("RD_DUMP"), sc.name) == 0) {
                ui.render(bands, 2, at, [](lgfx::LGFX_Sprite& band, const int top) {
                    const auto* px = static_cast<const uint16_t*>(band.getBuffer());

                    for (int y = 0; y < band.height(); ++y) {
                        printf("DUMP %d ", top + y);

                        for (int x = 0; x < band.width(); ++x) {
                            printf("%04x", px[y * band.width() + x]);
                        }

                        printf("\n");
                    }
                });
            }
        }

        return 0;
    }

    /**
     * Smoothness at ESP32 speed without a window: plays power-on (intro, boot messages, iris), the
     * ready moment, a 25 s shot with shimmer and standby in simulated time, draws every frame the
     * firmware would draw (Esp32Tempo) and prints per phase how many pictures per second the panel
     * gets and how long loop() is blocked per picture.
     */
    int runTempoReport() {
        struct Phase {
                const char* name;
                int frames = 0;
                uint32_t first = 0;
                uint32_t last = 0;
                float maxBlock = 0.0f;
                float sumFrame = 0.0f;
                int sumSent = 0;
        };

        for (const uint32_t spiHz : {27000000u, 40000000u}) {
            for (const bool bandwise : {false, true}) {
                FakeMachine machine;
                machine.reset(80.0f); // warm, so heating ends and the ready moment comes within the run
                rd::RoundUi ui;
                ui.setBrand(gBrand);
                Esp32Tempo tempo;
                tempo.setSpiHz(spiHz);
                tempo.bandwise = bandwise;
                Phase phases[] = {{"Intro beim Einschalten"}, {"Blende auf"}, {"Bereit-Moment (Wellen)"}, {"Bezug mit Lichtreflex"}, {"Blende zu (Standby)"}, {"übrige Bilder"}};
                bool brewed = false;
                bool pulsed = false;
                uint32_t brewAt = 0;

                ui.showMessage(msgVersion);
                ui.play(rd::Animation::Intro, 0);

                for (uint32_t now = 0; now <= 240000; ++now) {
                    if (now == 3200) ui.showMessage(msgWifi);
                    if (now == 4700) ui.showMessage(msgIp);
                    if (now == 6200) ui.clearMessage();

                    if (now > 6200) {
                        machine.step(0.001f);
                    }

                    // Shot 10 s after the ready moment, standby 20 s after the shot
                    if (pulsed && brewAt == 0 && ui.ready() && !ui.effectActive(now)) {
                        brewAt = now + 10000;
                    }

                    if (brewAt != 0 && now == brewAt) machine.toggleBrewSwitch();
                    if (brewAt != 0 && now == brewAt + 26000) machine.toggleBrewSwitch();
                    if (brewAt != 0 && now == brewAt + 46000) machine.toggleStandby();
                    if (brewAt != 0 && now > brewAt + 48000) break;

                    if (!tempo.busy(now)) {
                        ui.update(machine.model(), now);
                    }

                    brewed = brewed || ui.screen() == rd::Screen::Brew;

                    if (!tempo.busy(now) && ui.needsRedraw(now)) {
                        tempo.render(ui, now);
                        int p = 5;

                        if (now < 1700)
                            p = 0;
                        else if (now >= 6200 && now < 7200)
                            p = 1;
                        else if (ui.effectActive(now))
                            p = 2, pulsed = true;
                        else if (ui.screen() == rd::Screen::Brew && machine.model().brewPhase == rd::BrewPhase::Running)
                            p = 3;
                        else if (brewAt != 0 && now >= brewAt + 46000 && ui.animating(now))
                            p = 4;

                        Phase& ph = phases[p];
                        ph.first = ph.frames == 0 ? now : ph.first;
                        ph.last = now;
                        ++ph.frames;
                        ph.maxBlock = std::max(ph.maxBlock, tempo.lastBlockMs);
                        ph.sumFrame += tempo.lastFrameMs;
                        ph.sumSent += tempo.lastSentBands;
                    }
                }

                printf("\nSPI %u MHz, %s%s\n", spiHz / 1000000, bandwise ? "ein Streifen je loop() (Firmware jetzt)" : "ganzes Bild am Stück (vorher)", brewed && pulsed ? "" : "  (Ablauf unvollständig!)");
                printf("  %-26s %6s %9s %12s %20s %10s\n", "Phase", "Bilder", "Bilder/s", "Bild Ø ms", "loop() am Stück max", "Streifen");

                for (const Phase& ph : phases) {
                    const float seconds = ph.frames > 1 ? static_cast<float>(ph.last - ph.first) / 1000.0f : 0.0f;
                    const float rate = seconds > 0.0f ? static_cast<float>(ph.frames - 1) / seconds : 0.0f;
                    printf("  %-26s %6d %9.1f %12.0f %17.0f ms %10.1f\n", ph.name, ph.frames, rate, ph.frames > 0 ? ph.sumFrame / static_cast<float>(ph.frames) : 0.0f, ph.maxBlock,
                           ph.frames > 0 ? static_cast<float>(ph.sumSent) / static_cast<float>(ph.frames) : 0.0f);
                }
            }
        }

        return 0;
    }

    // -------------------------------------------------------------------------------------------
    // Sheets for the visual check (--inspect DIR; the round-display-ui-check skill looks at them)

    struct Tile {
            std::string caption;
            Image image;
    };

    void blitImage(Image& dst, const int ox, const int oy, const Image& src) {
        for (int y = 0; y < src.h && oy + y < dst.h; ++y) {
            std::memcpy(dst.at(ox, oy + y), &src.rgb[static_cast<size_t>(y) * src.w * 3], static_cast<size_t>(std::min(src.w, dst.w - ox)) * 3);
        }
    }

    class Inspector {
        public:
            explicit Inspector(std::string dir) :
                dir_(std::move(dir)) {
            }

            /** The UI as it is drawn at this moment, as one tile */
            void shot(rd::RoundUi& ui, const uint32_t now, const std::string& caption) {
                Panel panel(40);
                panel.render(ui, now);
                Tile t{caption, Image(deviceSize(1), deviceSize(1), 0xC4C4C8)};
                drawDevice(t.image, 0, 0, panel.screen, 1);
                tiles_.push_back(std::move(t));
            }

            /** Writes the tiles collected so far as sheets of 4 x 3: <name>-1.png, <name>-2.png, ... */
            void sheets(const std::string& name) {
                constexpr int kCols = 4;
                constexpr int kPerSheet = 12;
                constexpr int kCaption = 28;
                const int tile = deviceSize(1);
                lgfx::LGFX_Sprite caption;
                caption.setColorDepth(16);
                caption.createSprite(tile, kCaption);

                for (size_t start = 0, n = 1; start < tiles_.size(); start += kPerSheet, ++n) {
                    const size_t count = std::min<size_t>(kPerSheet, tiles_.size() - start);
                    const int rows = static_cast<int>((count + kCols - 1) / kCols);
                    Image sheet(kCols * tile, rows * (tile + kCaption), 0xC4C4C8);

                    for (size_t i = 0; i < count; ++i) {
                        const int x = static_cast<int>(i % kCols) * tile;
                        const int y = static_cast<int>(i / kCols) * (tile + kCaption);
                        blitImage(sheet, x, y, tiles_[start + i].image);
                        caption.fillScreen(rd::rgb(196, 196, 200));
                        rd::Painter p(caption, 0);
                        p.text(rd::fonts::textCompact(), tiles_[start + i].caption.c_str(), static_cast<float>(tile) * 0.5f, 20.0f, rd::rgb(30, 30, 34));
                        blitSprite(sheet, x, y + tile, caption);
                    }

                    const std::string path = dir_ + "/" + name + "-" + std::to_string(n) + ".png";
                    png::write(path.c_str(), sheet.w, sheet.h, sheet.rgb.data());
                    files.push_back(path);
                }

                tiles_.clear();
            }

            std::vector<std::string> files;

        private:
            std::string dir_;
            std::vector<Tile> tiles_;
    };

    /** The simulated machine in 50 ms steps, as the firmware looks at it */
    struct Run {
            FakeMachine m;
            rd::RoundUi ui;
            uint32_t now = 1000;

            explicit Run(const float startTemperature, const rd::Language lang = rd::Language::German) {
                m.language = lang;
                m.reset(startTemperature);
                ui.setBrand(gBrand);
                ui.update(m.model(), now);
            }

            void advance(const uint32_t ms) {
                for (uint32_t t = 0; t < ms; t += 50) {
                    m.step(0.05f);
                    now += 50;
                    ui.update(m.model(), now);
                }
            }

            /** Runs until the condition holds (at most maxMs); false if it never did */
            template <typename Condition>
            bool until(Condition done, const uint32_t maxMs) {
                for (uint32_t t = 0; t <= maxMs; t += 50) {
                    if (done()) {
                        return true;
                    }

                    advance(50);
                }

                return false;
            }
    };

    rd::Model normalModel(const float temperature, const float setpoint = 94.0f) {
        rd::Model m;
        m.mode = rd::Mode::Normal;
        m.temperature = temperature;
        m.setpoint = setpoint;
        m.heaterPercent = 35.0f;
        m.wifiConnected = true;
        return m;
    }

    int runInspect(const char* dir) {
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        Inspector in(dir);
        Panel panel(40);

        // 1. Every scenario in both languages
        for (const auto lang : {rd::Language::German, rd::Language::English}) {
            for (const auto& sc : scenarios()) {
                FakeMachine machine;
                machine.language = lang;
                machine.reset();
                rd::RoundUi ui;
                ui.setBrand(gBrand);
                gSimulatedMs = 0;
                sc.setup(machine, ui);
                const uint32_t at = gSimulatedMs > 0 ? gSimulatedMs + sc.atMs : sc.atMs;
                ui.update(machine.model(), at);
                in.shot(ui, at, std::string(sc.name) + (lang == rd::Language::German ? " DE" : " EN"));
            }

            in.sheets(lang == rd::Language::German ? "screens-de" : "screens-en");
        }

        // 2. Animations as film strips
        {
            rd::RoundUi ui;
            ui.setBrand(gBrand);
            ui.showMessage(msgVersion);
            ui.update(rd::Model(), 0);
            ui.play(rd::Animation::Intro, 0);

            for (const uint32_t t : {100u, 300u, 500u, 700u, 900u, 1100u, 1300u, 1500u, 1650u, 1700u, 1750u, 2000u}) {
                ui.update(rd::Model(), t);
                in.shot(ui, t, "Intro " + std::to_string(t) + " ms");
            }

            in.sheets("anim-intro");
        }

        {
            Run r(40.0f);
            r.ui.showMessage(msgIp);
            r.advance(500);
            r.ui.clearMessage(); // the iris opens as after the boot messages
            const uint32_t start = r.now;

            for (int i = 0; i < 12; ++i) {
                r.advance(i == 0 ? 0 : 80);
                in.shot(r.ui, r.now, "Blende auf +" + std::to_string(r.now - start) + " ms");
            }

            in.sheets("anim-reveal");
        }

        {
            Run r(94.0f);
            r.m.settle();
            r.advance(3000);
            r.m.toggleStandby();
            const uint32_t start = r.now;

            for (int i = 0; i < 12; ++i) {
                r.advance(i == 0 ? 0 : 70);
                in.shot(r.ui, r.now, "Blende zu +" + std::to_string(r.now - start) + " ms");
            }

            in.sheets("anim-close");
        }

        {
            Run r(80.0f);
            r.until([&] { return r.ui.effectActive(r.now); }, 400000);
            const uint32_t start = r.now;

            for (int i = 0; i < 12; ++i) {
                r.advance(i == 0 ? 0 : 130);
                in.shot(r.ui, r.now, "Bereit-Moment +" + std::to_string(r.now - start) + " ms");
            }

            in.sheets("anim-ready");
        }

        {
            Run r(94.0f);
            r.m.settle();
            r.advance(3000);
            r.m.toggleBrewSwitch();
            r.advance(9000);
            const uint32_t start = r.now;

            for (int i = 0; i < 12; ++i) {
                r.advance(i == 0 ? 0 : 150);
                in.shot(r.ui, r.now, "Lichtreflex +" + std::to_string(r.now - start) + " ms");
            }

            in.sheets("anim-shimmer");
        }

        // 3. Sequences over time
        {
            Run r(70.0f);
            bool switched = false;

            for (const float t : {75.0f, 85.0f, 88.5f}) {
                r.until([&] { return r.m.model().temperature >= t; }, 600000);
                char caption[48];
                snprintf(caption, sizeof(caption), "Aufheizen %.1f °C", static_cast<double>(r.m.model().temperature));
                in.shot(r.ui, r.now, caption);
            }

            switched = r.until([&] { return r.ui.screen() == rd::Screen::Ready; }, 600000);
            in.shot(r.ui, r.now, switched ? "Umschalten auf Bereit-Skala" : "FEHLER: kein Umschalten");
            r.advance(600);
            in.shot(r.ui, r.now, "Umschalten +0,6 s");
            r.until([&] { return r.ui.ready(); }, 600000);
            in.shot(r.ui, r.now, "BEREIT erreicht");
            r.advance(2000);
            in.shot(r.ui, r.now, "BEREIT +2 s");
            in.sheets("seq-heating");
        }

        for (const bool scale : {false, true}) {
            Run r(94.0f, scale ? rd::Language::English : rd::Language::German);
            r.m.scale = scale;
            r.m.settle();
            r.advance(3000);
            in.shot(r.ui, r.now, scale ? "EN, scale: ready" : "Bereit vor dem Bezug");
            r.m.toggleBrewSwitch();

            uint32_t elapsed = 0;

            for (const uint32_t t : {500u, 3000u, 8000u, 15000u}) {
                r.advance(t - elapsed);
                elapsed = t;
                char caption[48];
                snprintf(caption, sizeof(caption), "%s%.1f s nach Start", scale ? "EN, scale: " : "Bezug ", static_cast<double>(t) / 1000.0);
                in.shot(r.ui, r.now, caption);
            }

            r.until([&] { return r.m.model().brewPhase == rd::BrewPhase::Finished; }, 60000);
            in.shot(r.ui, r.now, scale ? "EN, scale: done" : "Bezug fertig");
            r.m.toggleBrewSwitch();
            r.advance(1500);
            in.shot(r.ui, r.now, scale ? "EN, scale: hold" : "Haltezeit");
            r.advance(4000);
            in.shot(r.ui, r.now, scale ? "EN, scale: after the shot" : "nach dem Bezug");
            in.sheets(scale ? "seq-brew-scale" : "seq-brew");
        }

        {
            Run r(94.0f);
            r.m.settle();
            r.advance(3000);
            in.shot(r.ui, r.now, "Bereit");
            r.m.toggleSteam();
            r.advance(2000);
            in.shot(r.ui, r.now, "Dampf +2 s");
            r.advance(30000);
            in.shot(r.ui, r.now, "Dampf +32 s");
            r.m.toggleSteam();
            r.advance(2000);
            in.shot(r.ui, r.now, "Dampf aus +2 s");
            r.m.toggleWaterEmpty();
            r.advance(500);
            in.shot(r.ui, r.now, "Wassertank leer");
            r.m.toggleWaterEmpty();
            r.advance(1000);
            in.shot(r.ui, r.now, "nachgefüllt");
            r.m.toggleStandby();
            r.advance(1500);
            in.shot(r.ui, r.now, "Standby");
            r.m.toggleStandby();
            r.advance(300);
            in.shot(r.ui, r.now, "aus Standby +0,3 s");
            r.advance(2000);
            in.shot(r.ui, r.now, "aus Standby +2,3 s");
            r.m.toggleSensorError();
            r.advance(500);
            in.shot(r.ui, r.now, "Sensorfehler");
            r.m.toggleSensorError();
            r.advance(1000);
            in.shot(r.ui, r.now, "Sensor wieder ok");
            r.m.triggerOvertemperature();
            r.advance(500);
            in.shot(r.ui, r.now, "Übertemperatur");
            in.sheets("seq-modes");
        }

        // 4. Limits and broken values
        {
            struct Case {
                    const char* caption;
                    rd::Model m;
            };
            std::vector<Case> cases;
            const float nan = std::numeric_limits<float>::quiet_NaN();
            cases.push_back({"Temperatur 150,0", normalModel(150.0f)});
            cases.push_back({"Temperatur 999,9", normalModel(999.9f)});
            cases.push_back({"Temperatur 1234", normalModel(1234.0f)});
            cases.push_back({"Temperatur 10000 (--)", normalModel(10000.0f)});
            cases.push_back({"Temperatur NaN (--)", normalModel(nan)});
            cases.push_back({"Temperatur -5,0", normalModel(-5.0f)});
            cases.push_back({"Soll 0", normalModel(20.0f, 0.0f)});
            rd::Model hot = normalModel(94.0f);
            hot.heaterPercent = 100.0f;
            hot.wifiConnected = false;
            cases.push_back({"Heizung 100 %, kein WLAN", hot});

            const auto brew = [](const float time, const float target, const float weight, const float targetWeight) {
                rd::Model m = normalModel(93.0f);
                m.mode = rd::Mode::Brew;
                m.brewPhase = rd::BrewPhase::Running;
                m.brewTimerVisible = true;
                m.brewTime = time;
                m.brewTargetTime = target;
                m.scaleEnabled = targetWeight > 0.0f || weight != 0.0f;
                m.bleScale = true;
                m.bleScaleConnected = true;
                m.brewWeight = weight;
                m.brewTargetWeight = targetWeight;
                return m;
            };
            cases.push_back({"Bezug 0,0 s", brew(0.0f, 25.0f, 0.0f, 0.0f)});
            cases.push_back({"Bezug 37,5 s (über Ziel)", brew(37.5f, 25.0f, 0.0f, 0.0f)});
            cases.push_back({"Bezug 99,9 s ohne Ziel", brew(99.9f, 0.0f, 0.0f, 0.0f)});
            cases.push_back({"Bezug 600 s", brew(600.0f, 25.0f, 0.0f, 0.0f)});
            cases.push_back({"Waage -0,4 g", brew(1.0f, 0.0f, -0.4f, 36.0f)});
            cases.push_back({"Waage 50 g (über Ziel)", brew(30.0f, 0.0f, 50.0f, 36.0f)});
            cases.push_back({"Waage 1234 g", brew(30.0f, 0.0f, 1234.0f, 36.0f)});
            cases.push_back({"Waage NaN", brew(10.0f, 0.0f, nan, 36.0f)});
            cases.push_back({"Waage ohne Ziel, 18 g", brew(12.0f, 25.0f, 18.0f, 0.0f)});

            for (const int cycles : {1, 20}) {
                rd::Model m = normalModel(94.0f);
                m.mode = rd::Mode::Backflush;
                m.backflushPhase = rd::BackflushPhase::Flushing;
                m.backflushCycle = static_cast<uint8_t>(cycles == 1 ? 1 : 7);
                m.backflushCycles = static_cast<uint8_t>(cycles);
                cases.push_back({cycles == 1 ? "Rückspülen 1/1" : "Rückspülen 7/20", m});
            }

            for (const auto& c : cases) {
                rd::RoundUi ui;
                ui.setBrand(gBrand);
                ui.update(c.m, 5000);
                ui.update(c.m, 9000); // past the hysteresis start
                in.shot(ui, 9000, c.caption);
            }

            // Messages the firmware may send with long names
            const std::pair<const char*, rd::Message> messages[] = {
                {"langer WLAN-Name", {"WLAN", "Verbinde mit", "MeinSehrLangesWLAN_Wohnzimmer_5GHz"}},
                {"langer Hostname", {"IP-ADRESSE", "kaffeemaschine-orione3000.local", "192.168.178.142"}},
                {"lange Überschrift", {"WLAN-EINRICHTUNGSASSISTENT", "Hotspot: silvia", "192.168.4.1"}},
                {"vier lange Zeilen", {"KALIBRIERUNG", "Bitte das bekannte Gewicht auflegen", "und zehn Sekunden warten, bis", "die Messung abgeschlossen ist 500.00g"}},
            };

            for (const auto& [caption, message] : messages) {
                rd::RoundUi ui;
                ui.setBrand(gBrand);
                ui.showMessage(message);
                ui.update(rd::Model(), 1000);
                in.shot(ui, 1000, caption);
            }

            in.sheets("limits");
        }

        for (const auto& f : in.files) {
            printf("%s\n", f.c_str());
        }

        return 0;
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
        {"G", "Waage in Einstellungen an/aus"},
        {"V", "Waage verbinden/trennen"},
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
        {"X", "ESP32-Tempo, SPI-Takt"},
        {"R", "Neustart (kalt)"},
        {"C", "Screenshot speichern"},
        {"Q / Esc", "Beenden"},
    };

    void drawSidePanel(lgfx::LGFX_Sprite& s, const FakeMachine& m, const rd::RoundUi& ui, const float speed, const float fps, const float drawMs, const Esp32Tempo& tempo) {
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
        y += 24;

        if (tempo.spiHz() > 0) {
            snprintf(buf, sizeof(buf), "ESP32-Tempo, SPI %u MHz: %.0f Bilder/s", tempo.spiHz() / 1000000, tempo.fps);
            p.text(rd::fonts::text(), buf, 18, y, head, rd::Align::Left);
            y += 24;
            snprintf(buf, sizeof(buf), "Bild %.0f ms, loop() max %.0f ms, %d/6 neu", tempo.lastFrameMs, tempo.lastBlockMs, tempo.lastSentBands);
            p.text(rd::fonts::text(), buf, 18, y, head, rd::Align::Left);
        }
        else {
            p.text(rd::fonts::text(), "ESP32-Tempo aus (Taste X)", 18, y, dim, rd::Align::Left);
        }

        y += 40;

        p.text(rd::fonts::label(), "TASTEN", 18, y, head, rd::Align::Left);
        y += 30;

        for (const auto& k : kKeys) {
            p.text(rd::fonts::text(), k.key, 18, y, text, rd::Align::Left);
            p.text(rd::fonts::text(), k.text, 168, y, dim, rd::Align::Left);
            y += 23;
        }
    }

    int runWindow(const int scale, const rd::Language lang, const int maxFrames, const char* windowPng, const int spiMhz) {
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
        Esp32Tempo tempo;
        constexpr uint32_t kSpiSteps[] = {0, 27000000, 40000000, 80000000};
        int spiStep = 0;

        for (int i = 0; i < 4; ++i) {
            spiStep = kSpiSteps[i] == static_cast<uint32_t>(spiMhz) * 1000000u ? i : spiStep;
        }

        tempo.setSpiHz(kSpiSteps[spiStep]);
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
                        case SDLK_v:
                            machine.scaleConnected = !machine.scaleConnected;
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
                        case SDLK_x:
                            spiStep = (spiStep + 1) % 4;
                            tempo.setSpiHz(kSpiSteps[spiStep]);
                            ui.invalidate();
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
                tempo.darken();
                tempo.invalidate();
            }

            if (tempo.spiHz() > 0) {
                // As on the chip: a new frame only when loop() is free again; bands appear when sent
                if (!power.asleep() && !tempo.busy(now) && ui.needsRedraw(now)) {
                    tempo.render(ui, now);
                    ++frames;
                }

                tempo.present(now);
            }
            else if (!power.asleep() && ui.needsRedraw(now)) {
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

            drawDevice(img, 0, (height - device) / 2, tempo.spiHz() > 0 ? tempo.panel() : panel.screen, scale);
            drawSidePanel(side, machine, ui, speeds[speedIndex], fps, drawMs, tempo);
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
    const char* review = nullptr;
    const char* compare = nullptr;
    const char* notes = nullptr;
    int frames = 0;
    const char* windowPng = nullptr;
    int atMs = -1;
    int spiMhz = 0;

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
        else if (std::strcmp(argv[i], "--spi") == 0 && i + 1 < argc) {
            spiMhz = std::atoi(argv[++i]); // window in ESP32 tempo with this SPI clock (27, 40 or 80)
        }
        else if (std::strcmp(argv[i], "--window-png") == 0 && i + 1 < argc) {
            windowPng = argv[++i];         // save the last window picture when quitting
        }
        else if (std::strcmp(argv[i], "--gallery") == 0 && i + 1 < argc) {
            gallery = argv[++i];
        }
        else if (std::strcmp(argv[i], "--review") == 0 && i + 1 < argc) {
            review = argv[++i];
        }
        else if (std::strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            compare = argv[++i]; // with --review: earlier review folder
        }
        else if (std::strcmp(argv[i], "--notes") == 0 && i + 1 < argc) {
            notes = argv[++i];   // with --review: JSON {"scenario": "what was changed"}
        }
        else if (std::strcmp(argv[i], "--bench") == 0) {
            if (!rd::RoundUi::begin()) {
                return 1;
            }

            return runBench();
        }
        else if (std::strcmp(argv[i], "--inspect") == 0 && i + 1 < argc) {
            if (!rd::RoundUi::begin()) {
                return 1;
            }

            return runInspect(argv[++i]); // sheets for the visual check (skill round-display-ui-check)
        }
        else if (std::strcmp(argv[i], "--tempo") == 0) {
            if (!rd::RoundUi::begin()) {
                return 1;
            }

            return runTempoReport();
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
            fprintf(stderr,
                    "usage: %s [--scale N] [--lang de|en] [--frames N] [--brand NAME] [--spi MHZ] [--shot NAME OUT.png | --gallery OUT.png | --review DIR [--compare OLD] [--notes FILE] | --inspect DIR | --tempo | --bench | "
                    "--list]\n",
                    argv[0]);
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

    if (review != nullptr) {
        return runReview(review, scale, compare, notes);
    }

    return runWindow(scale, lang, frames, windowPng, spiMhz);
}
