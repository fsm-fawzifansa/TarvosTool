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

bool SerialPort::Open(const std::string& port, int baudRate) {
    Close();

    // The \\.\ prefix is required for COM10 and above and is valid for all ports.
    std::string path = port.rfind("\\\\.\\", 0) == 0 ? port : "\\\\.\\" + port;

    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    m_handle = h;

    if (!ApplySettings(baudRate)) {
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

bool SerialPort::ApplySettings(int baudRate) {
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
    dcb.fOutxCtsFlow = FALSE;
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

#endif  // _WIN32
