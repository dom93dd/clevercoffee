/**
 * @file Png.h
 *
 * @brief Small PNG writer and reader (8 bit RGB) on top of zlib, for screenshots and the
 *        golden images of the tests.
 */

#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace png {

    namespace detail {
        inline void put32(std::vector<uint8_t>& v, const uint32_t x) {
            v.push_back(x >> 24);
            v.push_back(x >> 16 & 0xFF);
            v.push_back(x >> 8 & 0xFF);
            v.push_back(x & 0xFF);
        }

        inline uint32_t get32(const uint8_t* p) {
            return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
        }

        inline void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
            std::vector<uint8_t> buf;
            put32(buf, static_cast<uint32_t>(data.size()));
            buf.insert(buf.end(), type, type + 4);
            buf.insert(buf.end(), data.begin(), data.end());
            put32(buf, static_cast<uint32_t>(crc32(0, buf.data() + 4, static_cast<uInt>(buf.size() - 4))));
            fwrite(buf.data(), 1, buf.size(), f);
        }

        inline int paeth(const int a, const int b, const int c) {
            const int p = a + b - c;
            const int pa = std::abs(p - a);
            const int pb = std::abs(p - b);
            const int pc = std::abs(p - c);
            return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
        }
    } // namespace detail

    /** rgb: width * height * 3 bytes, rows top to bottom */
    inline bool write(const char* path, const int width, const int height, const uint8_t* rgb) {
        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));

        for (int y = 0; y < height; ++y) {
            raw.push_back(0); // no filter
            raw.insert(raw.end(), rgb + static_cast<size_t>(y) * width * 3, rgb + static_cast<size_t>(y + 1) * width * 3);
        }

        uLongf size = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> z(size);

        if (compress2(z.data(), &size, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK) {
            return false;
        }

        z.resize(size);
        FILE* f = fopen(path, "wb");

        if (f == nullptr) {
            return false;
        }

        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        fwrite(signature, 1, 8, f);

        std::vector<uint8_t> header;
        detail::put32(header, static_cast<uint32_t>(width));
        detail::put32(header, static_cast<uint32_t>(height));
        header.insert(header.end(), {8, 2, 0, 0, 0}); // 8 bit, RGB, deflate, no filter, no interlace
        detail::chunk(f, "IHDR", header);
        detail::chunk(f, "IDAT", z);
        detail::chunk(f, "IEND", {});
        return fclose(f) == 0;
    }

    /** Reads 8 bit RGB PNGs without interlacing (what write() produces, and most tools). */
    inline bool read(const char* path, int& width, int& height, std::vector<uint8_t>& rgb) {
        FILE* f = fopen(path, "rb");

        if (f == nullptr) {
            return false;
        }

        std::vector<uint8_t> file;
        uint8_t buf[65536];
        size_t n;

        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            file.insert(file.end(), buf, buf + n);
        }

        fclose(f);

        if (file.size() < 8 || memcmp(file.data(), "\x89PNG\r\n\x1a\n", 8) != 0) {
            return false;
        }

        std::vector<uint8_t> idat;
        width = height = 0;
        size_t pos = 8;

        while (pos + 12 <= file.size()) {
            const uint32_t len = detail::get32(&file[pos]);
            const char* type = reinterpret_cast<const char*>(&file[pos + 4]);
            const uint8_t* data = &file[pos + 8];

            if (pos + 12 + len > file.size()) {
                return false;
            }

            if (memcmp(type, "IHDR", 4) == 0) {
                width = static_cast<int>(detail::get32(data));
                height = static_cast<int>(detail::get32(data + 4));

                if (data[8] != 8 || data[9] != 2 || data[12] != 0) {
                    return false; // only 8 bit RGB, not interlaced
                }
            }
            else if (memcmp(type, "IDAT", 4) == 0) {
                idat.insert(idat.end(), data, data + len);
            }

            pos += 12 + len;
        }

        const size_t stride = static_cast<size_t>(width) * 3;
        std::vector<uint8_t> raw((stride + 1) * height);
        uLongf rawSize = static_cast<uLongf>(raw.size());

        if (width <= 0 || height <= 0 || uncompress(raw.data(), &rawSize, idat.data(), static_cast<uLong>(idat.size())) != Z_OK || rawSize != raw.size()) {
            return false;
        }

        rgb.assign(stride * height, 0);

        for (int y = 0; y < height; ++y) {
            const uint8_t filter = raw[y * (stride + 1)];
            const uint8_t* in = &raw[y * (stride + 1) + 1];
            uint8_t* out = &rgb[y * stride];
            const uint8_t* up = y > 0 ? &rgb[(y - 1) * stride] : nullptr;

            for (size_t x = 0; x < stride; ++x) {
                const int a = x >= 3 ? out[x - 3] : 0;
                const int b = up != nullptr ? up[x] : 0;
                const int c = up != nullptr && x >= 3 ? up[x - 3] : 0;
                int v = in[x];

                switch (filter) {
                    case 1:
                        v += a;
                        break;
                    case 2:
                        v += b;
                        break;
                    case 3:
                        v += (a + b) / 2;
                        break;
                    case 4:
                        v += detail::paeth(a, b, c);
                        break;
                    default:
                        break;
                }

                out[x] = static_cast<uint8_t>(v);
            }
        }

        return true;
    }

} // namespace png
