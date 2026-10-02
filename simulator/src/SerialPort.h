/**
 * @file SerialPort.h
 *
 * @brief Minimal serial port for the simulator (macOS and Linux): raw bytes out, text lines in,
 *        never blocking. Used by --display to drive the round display on an ESP32 over USB.
 */

#pragma once

#include <fcntl.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

    class SerialPort {
        public:
            SerialPort() = default;
            SerialPort(const SerialPort&) = delete;
            SerialPort& operator=(const SerialPort&) = delete;

            ~SerialPort() {
                close();
            }

            /** First USB serial adapter that looks like an ESP32 board (CP210x, CH34x, FTDI) */
            static std::string find() {
                for (const char* pattern : {"/dev/cu.usbserial-*", "/dev/cu.SLAB_USBtoUART*", "/dev/cu.wchusbserial*", "/dev/ttyUSB*", "/dev/ttyACM*"}) {
                    glob_t g{};

                    if (glob(pattern, 0, nullptr, &g) == 0 && g.gl_pathc > 0) {
                        std::string path = g.gl_pathv[0];
                        globfree(&g);
                        return path;
                    }

                    globfree(&g);
                }

                return "";
            }

            bool open(const std::string& path, const uint32_t baud) {
                close();
                fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

                if (fd_ < 0) {
                    return false;
                }

                termios tio{};
                tcgetattr(fd_, &tio);
                cfmakeraw(&tio);
                tio.c_cflag |= CLOCAL | CREAD;
                tio.c_cc[VMIN] = 0;
                tio.c_cc[VTIME] = 0;
#if defined(__APPLE__)
                cfsetspeed(&tio, B115200); // the real rate follows via IOSSIOSPEED
                tcsetattr(fd_, TCSANOW, &tio);
                speed_t speed = baud;
                ioctl(fd_, IOSSIOSPEED, &speed);
#else
                cfsetspeed(&tio, baud == 921600 ? B921600 : B115200);
                tcsetattr(fd_, TCSANOW, &tio);
#endif
                // DTR and RTS off: the board's auto-reset circuit then lets the ESP32 run
                int lines = TIOCM_DTR | TIOCM_RTS;
                ioctl(fd_, TIOCMBIC, &lines);
                path_ = path;
                return true;
            }

            void close() {
                if (fd_ >= 0) {
                    ::close(fd_);
                    fd_ = -1;
                }
            }

            bool isOpen() const {
                return fd_ >= 0;
            }

            const std::string& path() const {
                return path_;
            }

            /** Writes what the driver takes right now (a dropped frame is sent again later anyway) */
            void write(const std::vector<uint8_t>& bytes) {
                if (fd_ >= 0 && !bytes.empty()) {
                    (void)::write(fd_, bytes.data(), bytes.size());
                }
            }

            /** Complete text lines received since the last call */
            std::vector<std::string> lines() {
                std::vector<std::string> out;
                char buf[512];
                ssize_t n = 0;

                while (fd_ >= 0 && (n = ::read(fd_, buf, sizeof(buf))) > 0) {
                    pending_.append(buf, static_cast<size_t>(n));
                }

                size_t end = 0;

                while ((end = pending_.find('\n')) != std::string::npos) {
                    out.push_back(pending_.substr(0, end));
                    pending_.erase(0, end + 1);
                }

                if (pending_.size() > 4096) {
                    pending_.clear(); // boot noise without line ends
                }

                return out;
            }

        private:
            int fd_ = -1;
            std::string path_;
            std::string pending_;
    };

} // namespace sim
