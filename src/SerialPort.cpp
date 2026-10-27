#include "SerialPort.h"

#include <cerrno>
#include <cstring>
#include <glob.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>

SerialPort::~SerialPort() {
    Close();
}

bool SerialPort::Open(const std::string& port, int baudRate) {
    Close();

    m_fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        return false;
    }

    if (!ApplySettings(baudRate)) {
        Close();
        return false;
    }

    m_bytesTx = 0;
    m_bytesRx = 0;
    return true;
}

void SerialPort::Close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool SerialPort::IsOpen() const {
    return m_fd >= 0;
}

bool SerialPort::Write(const std::vector<uint8_t>& data) {
    if (!IsOpen() || data.empty()) return false;

    ssize_t written = ::write(m_fd, data.data(), data.size());
    if (written < 0) return false;

    m_bytesTx += static_cast<size_t>(written);
    return static_cast<size_t>(written) == data.size();
}

std::vector<uint8_t> SerialPort::Read(size_t maxBytes) {
    if (!IsOpen()) return {};

    std::vector<uint8_t> buf(maxBytes);
    ssize_t n = ::read(m_fd, buf.data(), maxBytes);
    if (n <= 0) return {};

    m_bytesRx += static_cast<size_t>(n);
    buf.resize(static_cast<size_t>(n));
    return buf;
}

std::vector<std::string> SerialPort::ListPorts() {
    std::vector<std::string> ports;

    auto collect = [&](const char* pattern) {
        glob_t g{};
        if (::glob(pattern, 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                ports.emplace_back(g.gl_pathv[i]);
            }
        }
        ::globfree(&g);
    };

    // Use cu.* (call-up / dial-out) devices on macOS — not tty.* (dial-in),
    // which can block waiting for DCD and is wrong for host-initiated comms.
    collect("/dev/cu.usbserial-*");
    collect("/dev/cu.SLAB_USBtoUART*");
    collect("/dev/cu.usbmodem*");
    collect("/dev/cu.wchusbserial*");   // CH340/CH341 adapters

    std::sort(ports.begin(), ports.end());
    return ports;
}

bool SerialPort::ApplySettings(int baudRate) {
    struct termios tty{};
    if (::tcgetattr(m_fd, &tty) != 0) return false;

    // Map integer baud to speed_t constant
    speed_t speed;
    switch (baudRate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:     speed = B115200; break;
    }

    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;      // 8 data bits

    // No hardware flow control
    tty.c_cflag &= ~CRTSCTS;

    // Enable receiver, ignore modem status lines
    tty.c_cflag |= CREAD | CLOCAL;

    // Raw input (no special processing)
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);

    // No software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    // Non-blocking: return immediately with whatever bytes are available
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  // 100 ms timeout

    if (::tcsetattr(m_fd, TCSANOW, &tty) != 0) return false;
    ::tcflush(m_fd, TCIOFLUSH);
    return true;
}
