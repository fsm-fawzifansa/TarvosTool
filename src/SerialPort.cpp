#include "SerialPort.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>

static HANDLE ToHandle(void* h) { return static_cast<HANDLE>(h); }

SerialPort::~SerialPort() {
    Close();
}

bool SerialPort::Open(const std::string& port, int baudRate, bool busyFlowControl) {
    Close();

    // The \\.\ prefix is required for COM10 and above and is valid for all ports.
    std::string path = port.rfind("\\\\.\\", 0) == 0 ? port : "\\\\.\\" + port;

    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    m_handle = h;

    if (!ApplySettings(baudRate, busyFlowControl)) {
        Close();
        return false;
    }

    m_bytesTx = 0;
    m_bytesRx = 0;
    return true;
}

void SerialPort::Close() {
    if (m_handle) {
        ::CloseHandle(ToHandle(m_handle));
        m_handle = nullptr;
    }
}

bool SerialPort::IsOpen() const {
    return m_handle != nullptr;
}

bool SerialPort::Write(const std::vector<uint8_t>& data) {
    if (!IsOpen() || data.empty()) return false;

    DWORD written = 0;
    if (!::WriteFile(ToHandle(m_handle), data.data(),
                     static_cast<DWORD>(data.size()), &written, nullptr))
        return false;

    m_bytesTx += written;
    return written == data.size();
}

std::vector<uint8_t> SerialPort::Read(size_t maxBytes) {
    if (!IsOpen()) return {};

    std::vector<uint8_t> buf(maxBytes);
    DWORD n = 0;
    if (!::ReadFile(ToHandle(m_handle), buf.data(),
                    static_cast<DWORD>(maxBytes), &n, nullptr) || n == 0)
        return {};

    m_bytesRx += n;
    buf.resize(n);
    return buf;
}

std::vector<std::string> SerialPort::ListPorts() {
    std::vector<std::string> ports;

    // Every serial driver registers its port under SERIALCOMM, with the
    // COM name (e.g. "COM3") as the value data.
    HKEY key;
    if (::RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                        0, KEY_READ, &key) == ERROR_SUCCESS) {
        for (DWORD i = 0;; ++i) {
            char  name[256];
            char  data[256];
            DWORD nameLen = sizeof(name);
            DWORD dataLen = sizeof(data) - 1;
            DWORD type    = 0;
            LONG  rc = ::RegEnumValueA(key, i, name, &nameLen, nullptr, &type,
                                       reinterpret_cast<LPBYTE>(data), &dataLen);
            if (rc == ERROR_NO_MORE_ITEMS) break;
            if (rc != ERROR_SUCCESS || type != REG_SZ) continue;
            data[dataLen] = '\0';
            ports.emplace_back(data);
        }
        ::RegCloseKey(key);
    }

    // Natural order: COM2 < COM10
    std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return a.size() < b.size();
        return a < b;
    });
    return ports;
}

bool SerialPort::ApplySettings(int baudRate, bool busyFlowControl) {
    HANDLE h = ToHandle(m_handle);

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(h, &dcb)) return false;

    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = busyFlowControl ? TRUE : FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;
    if (!::SetCommState(h, &dcb)) return false;

    // Non-blocking read: return immediately with whatever is buffered.
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout        = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant   = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 1000;
    if (!::SetCommTimeouts(h, &to)) return false;

    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

#else  // POSIX (macOS)

#include <cerrno>
#include <cstring>
#include <glob.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif

SerialPort::~SerialPort() {
    Close();
}

bool SerialPort::Open(const std::string& port, int baudRate, bool busyFlowControl) {
    Close();

    m_fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        return false;
    }

    if (!ApplySettings(baudRate, busyFlowControl)) {
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

    // The fd is non-blocking: with BUSY flow control the driver holds bytes
    // back while the module is busy, so wait for the rest instead of failing.
    size_t done = 0;
    int waits = 0;
    while (done < data.size()) {
        ssize_t n = ::write(m_fd, data.data() + done, data.size() - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
            m_bytesTx += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (++waits > 100) return false;              // ~1 s without progress

        struct pollfd pfd{ m_fd, POLLOUT, 0 };
        ::poll(&pfd, 1, 10);
    }
    return true;
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

bool SerialPort::ApplySettings(int baudRate, bool busyFlowControl) {
    struct termios tty{};
    if (::tcgetattr(m_fd, &tty) != 0) return false;

    // Map integer baud to speed_t constant. Rates above 230400 have no B*
    // constant on macOS; they are set with IOSSIOSPEED after tcsetattr.
    speed_t speed;
    bool customSpeed = false;
    switch (baudRate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
#if defined(B460800) && defined(B921600)
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
#endif
        default:     speed = B115200; customSpeed = true; break;
    }
#ifndef __APPLE__
    if (customSpeed) return false;
#endif

    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;      // 8 data bits

    // BUSY flow control: only gate our output on CTS. macOS has an output-only
    // flag; elsewhere CRTSCTS also drives RTS, which stays asserted.
#ifdef CCTS_OFLOW
    tty.c_cflag &= ~(CRTSCTS | CCTS_OFLOW);
    if (busyFlowControl) tty.c_cflag |= CCTS_OFLOW;
#else
    tty.c_cflag &= ~CRTSCTS;
    if (busyFlowControl) tty.c_cflag |= CRTSCTS;
#endif

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
#ifdef __APPLE__
    if (customSpeed) {
        speed_t custom = static_cast<speed_t>(baudRate);
        if (::ioctl(m_fd, IOSSIOSPEED, &custom) != 0) return false;
    }
#endif
    ::tcflush(m_fd, TCIOFLUSH);
    return true;
}

#endif  // _WIN32
