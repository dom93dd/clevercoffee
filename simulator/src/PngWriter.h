/**
 * @file PngWriter.h
 *
 * @brief Minimal PNG writer (RGB, 8 bit, uncompressed deflate blocks) so the simulator needs no image library.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace png {

    inline uint32_t crc32(const uint8_t* data, const size_t len, uint32_t crc = 0) {
        static uint32_t table[256];
        static bool init = false;

        if (!init) {
            for (uint32_t n = 0; n < 256; ++n) {
                uint32_t c = n;

                for (int k = 0; k < 8; ++k) {
                    c = c & 1 ? 0xEDB88320u ^ c >> 1 : c >> 1;
                }

                table[n] = c;
            }

            init = true;
        }

        crc = ~crc;

        for (size_t i = 0; i < len; ++i) {
            crc = table[(crc ^ data[i]) & 0xFF] ^ crc >> 8;
        }

        return ~crc;
    }

    inline void put32(std::vector<uint8_t>& v, const uint32_t x) {
        v.push_back(x >> 24);
        v.push_back(x >> 16 & 0xFF);
        v.push_back(x >> 8 & 0xFF);
        v.push_back(x & 0xFF);
    }

    inline void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> buf;
        put32(buf, static_cast<uint32_t>(data.size()));
        buf.insert(buf.end(), type, type + 4);
        buf.insert(buf.end(), data.begin(), data.end());
        const uint32_t crc = crc32(buf.data() + 4, buf.size() - 4);
        put32(buf, crc);
        fwrite(buf.data(), 1, buf.size(), f);
    }

    /** rgb: width * height * 3 bytes, rows top to bottom */
    inline bool write(const char* path, const int width, const int height, const uint8_t* rgb) {
        FILE* f = fopen(path, "wb");

        if (f == nullptr) {
            return false;
        }

        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        fwrite(signature, 1, 8, f);

        std::vector<uint8_t> header;
        put32(header, width);
        put32(header, height);
        header.insert(header.end(), {8, 2, 0, 0, 0}); // 8 bit, RGB, deflate, no filter, no interlace
        chunk(f, "IHDR", header);

        // Raw scanlines with filter byte 0
        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));

        for (int y = 0; y < height; ++y) {
            raw.push_back(0);
            raw.insert(raw.end(), rgb + static_cast<size_t>(y) * width * 3, rgb + static_cast<size_t>(y + 1) * width * 3);
        }

        // zlib stream with stored blocks
        std::vector<uint8_t> z = {0x78, 0x01};
        size_t pos = 0;

        do {
            const size_t len = std::min<size_t>(65535, raw.size() - pos);
            const bool last = pos + len == raw.size();
            z.push_back(last ? 1 : 0);
            z.push_back(len & 0xFF);
            z.push_back(len >> 8);
            z.push_back(~len & 0xFF);
            z.push_back(~len >> 8 & 0xFF);
            z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + len));
            pos += len;
        } while (pos < raw.size());

        uint32_t a = 1;
        uint32_t b = 0;

        for (const uint8_t byte : raw) {
            a = (a + byte) % 65521;
            b = (b + a) % 65521;
        }

        put32(z, b << 16 | a);
        chunk(f, "IDAT", z);
        chunk(f, "IEND", {});
        fclose(f);
        return true;
    }

} // namespace png
