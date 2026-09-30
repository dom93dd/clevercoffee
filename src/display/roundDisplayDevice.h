/**
 * @file roundDisplayDevice.h
 *
 * @brief Round 1.28" TFT with GC9A01 controller (240x240, SPI), driven by LovyanGFX.
 *        Only used in builds with -D ROUND_DISPLAY (PlatformIO envs esp32_round_*).
 *
 * Wiring to the GPIO header of the CleverCoffee PCB (pins that 4.0.3 leaves free):
 *   SCL/SCK -> IO14, SDA/MOSI -> IO13, CS -> IO15, DC -> IO4, RST -> IO5, VCC -> 3.3 V, GND -> GND
 *   BLK (backlight, if the module has it) -> IO18 with -D PIN_TFT_BL=18, otherwise to 3.3 V
 * IO4/IO5 are the rotary encoder pins and IO18 the dimmer zero crossing; none of them is used by the firmware.
 */

#pragma once

#include <LovyanGFX.hpp>

#ifndef PIN_TFT_SCLK
#define PIN_TFT_SCLK 14
#endif

#ifndef PIN_TFT_MOSI
#define PIN_TFT_MOSI 13
#endif

#ifndef PIN_TFT_CS
#define PIN_TFT_CS 15
#endif

#ifndef PIN_TFT_DC
#define PIN_TFT_DC 4
#endif

#ifndef PIN_TFT_RST
#define PIN_TFT_RST 5
#endif

#ifndef PIN_TFT_BL
#define PIN_TFT_BL -1
#endif

// 27 MHz is safe for 30-40 cm of wire between PCB and front panel; short wires allow 40 or 80 MHz
#ifndef TFT_SPI_FREQ
#define TFT_SPI_FREQ 27000000
#endif

class RoundTft : public lgfx::LGFX_Device {
    public:
        RoundTft() {
            auto bus = bus_.config();
            bus.spi_host = SPI2_HOST; // HSPI: IO14/IO13/IO15 are its native pins
            bus.spi_mode = 0;
            bus.freq_write = TFT_SPI_FREQ;
            bus.freq_read = 16000000;
            bus.pin_sclk = PIN_TFT_SCLK;
            bus.pin_mosi = PIN_TFT_MOSI;
            bus.pin_miso = -1;
            bus.pin_dc = PIN_TFT_DC;
            bus.dma_channel = SPI_DMA_CH_AUTO;
            bus_.config(bus);
            panel_.setBus(&bus_);

            auto panel = panel_.config();
            panel.pin_cs = PIN_TFT_CS;
            panel.pin_rst = PIN_TFT_RST;
            panel.pin_busy = -1;
            panel.panel_width = 240;
            panel.panel_height = 240;
            panel.readable = false;
            panel.invert = true;
            panel.bus_shared = false;
            panel_.config(panel);

            if (PIN_TFT_BL >= 0) {
                auto light = light_.config();
                light.pin_bl = PIN_TFT_BL;
                light.freq = 44100;
                light.pwm_channel = 7;
                light_.config(light);
                panel_.setLight(&light_);
            }

            setPanel(&panel_);
        }

    private:
        lgfx::Bus_SPI bus_;
        lgfx::Panel_GC9A01 panel_;
        lgfx::Light_PWM light_;
};

/**
 * @brief Stands in for the U8g2 object of the OLED code.
 *
 * The standby logic, powerHandler.h and embeddedWebserver.h switch the display with
 * u8g2->setPowerSave(); keeping that name means those files need no changes.
 * The web server calls it from its own task, so this only records the wish; the loop
 * (roundDisplayLoop) closes the iris and puts the panel to sleep, or wakes it up again.
 */
class RoundDisplayPower {
    public:
        void setPowerSave(const uint8_t on) {
            sleepRequested_ = on != 0;
        }

        bool sleepRequested() const {
            return sleepRequested_;
        }

    private:
        volatile bool sleepRequested_ = false;
};

inline RoundDisplayPower* u8g2 = nullptr;
