/**
 * @file RemoteLink.h
 *
 * @brief Simulator -> ESP32 over USB: the simulator sends the machine state, the ESP32 with the round
 *        display draws it with the firmware's UI code at the chip's own speed (esp32-bench env remote).
 *
 * Frames: 0xA5 0x5A, type, payload length (2 bytes LE), payload, CRC-8 over type, length and payload.
 *   'M' model (encodeModel), 'T' message (title, line1..3 as 0-terminated strings; all empty = none),
 *   'A' play animation (1 byte rd::Animation), 'P' display off requested (1 byte 0/1).
 * The ESP32 answers once a second with a text line: "STATUS <frames per s> <models> <bad frames> <slowest ms>".
 *
 * Header only, shared by the simulator (sender) and esp32-bench/src/remote.cpp (receiver).
 */

#pragma once

#include <RoundDisplayModel.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sim::link {

    constexpr uint8_t kSync0 = 0xA5;
    constexpr uint8_t kSync1 = 0x5A;
    constexpr size_t kMaxPayload = 600;
    constexpr uint32_t kBaud = 921600;

    enum Type : uint8_t {
        Model = 'M',
        Text = 'T',
        Play = 'A',
        Power = 'P',
    };

    inline uint8_t crc8(const uint8_t* data, const size_t n, uint8_t crc = 0) {
        for (size_t i = 0; i < n; ++i) {
            crc ^= data[i];

            for (int b = 0; b < 8; ++b) {
                crc = static_cast<uint8_t>((crc & 0x80) != 0 ? (crc << 1) ^ 0x07 : crc << 1);
            }
        }

        return crc;
    }

    // --- model ---------------------------------------------------------------------------------

    class Writer {
        public:
            void u8(const uint8_t v) {
                bytes.push_back(v);
            }

            void f32(const float v) {
                uint8_t b[4];
                std::memcpy(b, &v, 4); // IEEE 754, little endian on both sides
                bytes.insert(bytes.end(), b, b + 4);
            }

            std::vector<uint8_t> bytes;
    };

    class Reader {
        public:
            Reader(const uint8_t* data, const size_t n) :
                data_(data), n_(n) {
            }

            uint8_t u8() {
                return pos_ < n_ ? data_[pos_++] : (ok_ = false, 0);
            }

            float f32() {
                float v = 0.0f;

                if (pos_ + 4 <= n_) {
                    std::memcpy(&v, data_ + pos_, 4);
                }
                else {
                    ok_ = false;
                }

                pos_ += 4;
                return v;
            }

            bool ok() const {
                return ok_ && pos_ == n_;
            }

        private:
            const uint8_t* data_;
            size_t n_;
            size_t pos_ = 0;
            bool ok_ = true;
    };

    inline std::vector<uint8_t> encodeModel(const rd::Model& m) {
        Writer w;
        w.u8(static_cast<uint8_t>(m.mode));
        w.u8(static_cast<uint8_t>(m.language));
        w.f32(m.temperature);
        w.f32(m.setpoint);
        w.f32(m.heaterPercent);
        w.f32(m.readyBand);
        w.f32(m.emergencyResetTemp);
        w.u8(m.brewTimerVisible);
        w.u8(static_cast<uint8_t>(m.brewPhase));
        w.f32(m.brewTime);
        w.f32(m.brewTargetTime);
        w.f32(m.lastBrewTime);
        w.f32(m.flushTime);
        w.f32(m.hotWaterTime);
        w.u8(m.scaleEnabled);
        w.u8(m.scaleFault);
        w.u8(m.bleScale);
        w.u8(m.bleScaleConnected);
        w.f32(m.brewWeight);
        w.f32(m.brewTargetWeight);
        w.u8(static_cast<uint8_t>(m.backflushPhase));
        w.u8(m.backflushCycle);
        w.u8(m.backflushCycles);
        w.u8(m.offlineMode);
        w.u8(m.wifiConnected);
        w.u8(m.wifiBars);
        w.u8(m.mqttEnabled);
        w.u8(m.mqttConnected);
        return w.bytes;
    }

    inline bool decodeModel(const uint8_t* data, const size_t n, rd::Model& m) {
        Reader r(data, n);
        rd::Model out;
        out.mode = static_cast<rd::Mode>(r.u8());
        out.language = static_cast<rd::Language>(r.u8());
        out.temperature = r.f32();
        out.setpoint = r.f32();
        out.heaterPercent = r.f32();
        out.readyBand = r.f32();
        out.emergencyResetTemp = r.f32();
        out.brewTimerVisible = r.u8() != 0;
        out.brewPhase = static_cast<rd::BrewPhase>(r.u8());
        out.brewTime = r.f32();
        out.brewTargetTime = r.f32();
        out.lastBrewTime = r.f32();
        out.flushTime = r.f32();
        out.hotWaterTime = r.f32();
        out.scaleEnabled = r.u8() != 0;
        out.scaleFault = r.u8() != 0;
        out.bleScale = r.u8() != 0;
        out.bleScaleConnected = r.u8() != 0;
        out.brewWeight = r.f32();
        out.brewTargetWeight = r.f32();
        out.backflushPhase = static_cast<rd::BackflushPhase>(r.u8());
        out.backflushCycle = r.u8();
        out.backflushCycles = r.u8();
        out.offlineMode = r.u8() != 0;
        out.wifiConnected = r.u8() != 0;
        out.wifiBars = r.u8();
        out.mqttEnabled = r.u8() != 0;
        out.mqttConnected = r.u8() != 0;

        // Unknown enum values would select no screen; keep the old model then
        if (!r.ok() || static_cast<uint8_t>(out.mode) > static_cast<uint8_t>(rd::Mode::SensorError) || static_cast<uint8_t>(out.language) > 1 ||
            static_cast<uint8_t>(out.brewPhase) > static_cast<uint8_t>(rd::BrewPhase::Finished) || static_cast<uint8_t>(out.backflushPhase) > static_cast<uint8_t>(rd::BackflushPhase::Finished)) {
            return false;
        }

        m = out;
        return true;
    }

    // --- message -------------------------------------------------------------------------------

    /** A message as four strings; all empty = no message */
    struct TextMessage {
            std::string title, line1, line2, line3;

            bool empty() const {
                return title.empty() && line1.empty() && line2.empty() && line3.empty();
            }

            rd::Message message() const {
                return {title.c_str(), line1.c_str(), line2.c_str(), line3.c_str()};
            }

            bool operator==(const TextMessage& o) const {
                return title == o.title && line1 == o.line1 && line2 == o.line2 && line3 == o.line3;
            }
    };

    inline std::vector<uint8_t> encodeText(const TextMessage& t) {
        std::vector<uint8_t> out;

        for (const std::string* s : {&t.title, &t.line1, &t.line2, &t.line3}) {
            out.insert(out.end(), s->begin(), s->begin() + std::min<size_t>(s->size(), 140));
            out.push_back(0);
        }

        return out;
    }

    inline bool decodeText(const uint8_t* data, const size_t n, TextMessage& t) {
        std::string* parts[] = {&t.title, &t.line1, &t.line2, &t.line3};
        size_t pos = 0;

        for (std::string* part : parts) {
            const void* end = pos < n ? std::memchr(data + pos, 0, n - pos) : nullptr;

            if (end == nullptr) {
                return false;
            }

            const size_t len = static_cast<const uint8_t*>(end) - (data + pos);
            part->assign(reinterpret_cast<const char*>(data + pos), len);
            pos += len + 1;
        }

        return pos == n;
    }

    // --- framing -------------------------------------------------------------------------------

    inline std::vector<uint8_t> frame(const Type type, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> out = {kSync0, kSync1, static_cast<uint8_t>(type), static_cast<uint8_t>(payload.size() & 0xFF), static_cast<uint8_t>(payload.size() >> 8)};
        out.insert(out.end(), payload.begin(), payload.end());
        out.push_back(crc8(out.data() + 2, out.size() - 2));
        return out;
    }

    /** Collects bytes and hands out complete frames with a good checksum; resyncs after garbage */
    class Parser {
        public:
            /** Feeds one byte; true when a frame is complete (type() and payload() hold it) */
            bool push(const uint8_t b) {
                switch (state_) {
                    case 0:
                        state_ = b == kSync0 ? 1 : 0;
                        return false;
                    case 1:
                        state_ = b == kSync1 ? 2 : (b == kSync0 ? 1 : 0);
                        return false;
                    case 2:
                        type_ = b;
                        state_ = 3;
                        return false;
                    case 3:
                        length_ = b;
                        state_ = 4;
                        return false;
                    case 4:
                        length_ |= static_cast<size_t>(b) << 8;
                        payload_.clear();

                        if (length_ > kMaxPayload) {
                            ++bad;
                            state_ = 0;
                            return false;
                        }

                        state_ = length_ == 0 ? 6 : 5;
                        return false;
                    case 5:
                        payload_.push_back(b);
                        state_ = payload_.size() == length_ ? 6 : 5;
                        return false;
                    default:
                        {
                            state_ = 0;
                            uint8_t head[3] = {type_, static_cast<uint8_t>(length_ & 0xFF), static_cast<uint8_t>(length_ >> 8)};
                            const uint8_t crc = crc8(payload_.data(), payload_.size(), crc8(head, 3));

                            if (crc != b) {
                                ++bad;
                                return false;
                            }

                            return true;
                        }
                }
            }

            uint8_t type() const {
                return type_;
            }

            const std::vector<uint8_t>& payload() const {
                return payload_;
            }

            uint32_t bad = 0; // frames with a wrong checksum or length

        private:
            int state_ = 0;
            uint8_t type_ = 0;
            size_t length_ = 0;
            std::vector<uint8_t> payload_;
    };

} // namespace sim::link
