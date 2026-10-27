#pragma once

#include <string>
#include <vector>
#include <cstdint>

/// Serial port wrapper for USB-UART adapters (FTDI VCP etc.).
/// POSIX (macOS) and Win32 (COM ports) back ends share this interface.
/// All I/O is non-blocking; Read() returns immediately with whatever bytes
/// are available (up to maxBytes). Write() is blocking.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    /// Open the given port (e.g. "/dev/cu.usbserial-A10KI1GG" on macOS or "COM3"
    /// on Windows) at baudRate.
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

    /// List connected serial ports: /dev/cu.* USB-serial devices on macOS,
    /// COM ports on Windows. Returns a sorted list of port names.
    static std::vector<std::string> ListPorts();

    /// Total bytes written since Open().
    size_t BytesTx() const { return m_bytesTx; }

    /// Total bytes read since Open().
    size_t BytesRx() const { return m_bytesRx; }

private:
    /// Apply 8N1, no flow control at the given baud.
    bool ApplySettings(int baudRate);

#ifdef _WIN32
    void*  m_handle   = nullptr;   ///< HANDLE; nullptr = closed
#else
    int    m_fd       = -1;
#endif
    size_t m_bytesTx  = 0;
    size_t m_bytesRx  = 0;
};
