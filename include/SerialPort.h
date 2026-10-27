#pragma once

#include <string>
#include <vector>
#include <cstdint>

/// POSIX-based serial port wrapper for FTDI VCP devices on macOS.
/// All I/O is non-blocking; Read() returns immediately with whatever bytes
/// are available (up to maxBytes). Write() is blocking.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    /// Open the given port (e.g. "/dev/tty.usbserial-A10KI1GG") at baudRate.
    /// Returns true on success. Leaves port closed and returns false on error.
    bool Open(const std::string& port, int baudRate = 115200);

    /// Close the port if open. Safe to call when already closed.
    void Close();

    /// Returns true if the port is currently open.
    bool IsOpen() const;

    /// Write all bytes in data to the port. Returns false on error.
    bool Write(const std::vector<uint8_t>& data);

    /// Read up to maxBytes from the port. Returns immediately with whatever
    /// bytes are available (may be empty). Returns empty vector on error.
    std::vector<uint8_t> Read(size_t maxBytes = 512);

    /// Scan /dev/tty.usbserial-* and /dev/tty.SLAB_USBtoUART* for connected
    /// FTDI / CP210x devices. Returns a sorted list of device paths.
    static std::vector<std::string> ListPorts();

    /// Total bytes written since Open().
    size_t BytesTx() const { return m_bytesTx; }

    /// Total bytes read since Open().
    size_t BytesRx() const { return m_bytesRx; }

private:
    /// Apply termios settings for 8N1, no flow control at the given baud.
    bool ApplySettings(int baudRate);

    int    m_fd       = -1;
    size_t m_bytesTx  = 0;
    size_t m_bytesRx  = 0;
};
