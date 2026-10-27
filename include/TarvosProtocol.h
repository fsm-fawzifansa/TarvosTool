#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// WE-ProWare binary frame protocol for the Würth Elektronik Tarvos-e module.
///
/// Frame layout (all fields exactly as shown in the Tarvos-e datasheet):
///   [STX=0x02] [CMD] [LEN] [PAYLOAD...] [CS]
///   LEN = 1 byte (number of payload bytes, 0–255)
///   CS  = XOR of all preceding bytes (STX through last payload byte)
///
/// Confirmation codes = REQ | 0x40
/// Indication codes   = REQ | 0x80
class TarvosProtocol {
public:
    // -----------------------------------------------------------------------
    // Protocol constant
    // -----------------------------------------------------------------------
    static constexpr uint8_t STX = 0x02;

    // -----------------------------------------------------------------------
    // Request command codes (host → module)
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_DATA_REQ           = 0x00; ///< Send data in command mode
    static constexpr uint8_t CMD_DATAEX_REQ         = 0x01; ///< Send data with extended addressing
    static constexpr uint8_t CMD_SET_MODE_REQ       = 0x04; ///< Switch operating mode
    static constexpr uint8_t CMD_RESET_REQ          = 0x05; ///< Software reset (frame: 02 05 00 07)
    static constexpr uint8_t CMD_SET_CHANNEL_REQ    = 0x06; ///< Set RF channel
    static constexpr uint8_t CMD_SET_DESTNETID_REQ  = 0x07; ///< Set destination network ID
    static constexpr uint8_t CMD_SET_DESTADDR_REQ   = 0x08; ///< Set destination address
    static constexpr uint8_t CMD_SET_REQ            = 0x09; ///< Write non-volatile user setting
    static constexpr uint8_t CMD_GET_REQ            = 0x0A; ///< Read non-volatile user setting
    static constexpr uint8_t CMD_RSSI_REQ           = 0x0D; ///< Read last RSSI
    static constexpr uint8_t CMD_SHUTDOWN_REQ       = 0x0E; ///< Shutdown module
    static constexpr uint8_t CMD_STANDBY_REQ        = 0x0F; ///< Enter standby/sleep
    static constexpr uint8_t CMD_SET_PAPOWER_REQ    = 0x10; ///< Set PA output power
    static constexpr uint8_t CMD_FACTORY_RESET_REQ  = 0x12; ///< Restore defaults + software reset

    // -----------------------------------------------------------------------
    // Confirmation codes (module → host, in response to a request)
    // Confirmation = REQ | 0x40
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_DATA_CNF           = 0x40; ///< Confirmation of CMD_DATA_REQ
    static constexpr uint8_t CMD_RESET_CNF          = 0x45; ///< Confirmation of CMD_RESET_REQ
    static constexpr uint8_t CMD_SET_CNF            = 0x49; ///< Confirmation of CMD_SET_REQ
    static constexpr uint8_t CMD_GET_CNF            = 0x4A; ///< Confirmation of CMD_GET_REQ
    static constexpr uint8_t CMD_FACTORY_RESET_CNF  = 0x52; ///< Confirmation of CMD_FACTORY_RESET_REQ

    // -----------------------------------------------------------------------
    // Indication codes (module → host, spontaneous events)
    // Indication = REQ | 0x80
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_DATAEX_IND         = 0x81; ///< Incoming radio data (0x02|0x81|LEN|payload|RSSI|CS)
    static constexpr uint8_t CMD_RESET_IND          = 0x85; ///< Module ready after boot/reset (5 bytes)
    static constexpr uint8_t CMD_STANDBY_IND        = 0x8F; ///< Module entered standby

    // -----------------------------------------------------------------------
    // Module-specific unicast commands (Tarvos-e extension, not in base protocol)
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_UNICAST_DATA_REQ   = 0x0B;
    static constexpr uint8_t CMD_UNICAST_DATA_CNF   = 0x0C;

    // -----------------------------------------------------------------------
    // Parsed frame
    // -----------------------------------------------------------------------
    struct Frame {
        uint8_t              cmd;
        std::vector<uint8_t> payload;
        bool                 checksumOk;
    };

    // -----------------------------------------------------------------------
    // Frame building
    // -----------------------------------------------------------------------

    /// Build a complete wire-ready frame: STX + CMD + LEN(1 byte) + PAYLOAD + CS.
    /// CS = XOR of all preceding bytes (STX through last payload byte).
    static std::vector<uint8_t> BuildFrame(uint8_t cmd,
                                            const std::vector<uint8_t>& payload = {});

    /// XOR checksum over all bytes in the span.
    static uint8_t CalcChecksum(const std::vector<uint8_t>& bytes);

    // -----------------------------------------------------------------------
    // Convenience command builders
    // -----------------------------------------------------------------------

    /// Send CMD_GET_REQ for setting index 0 (UART baudrate) — useful as an
    /// alive-check since Tarvos has no dedicated ping command.
    static std::vector<uint8_t> CmdPing();

    /// RESET_REQ (0x05) — software reset. Wire frame: 02 05 00 07
    static std::vector<uint8_t> CmdReset();

    /// STANDBY_REQ (0x0F) — enter standby/sleep mode.
    static std::vector<uint8_t> CmdSleep();

    /// GET_REQ (0x0A) — read a module setting by index.
    static std::vector<uint8_t> CmdGet(uint8_t settingId);

    /// SET_REQ (0x09) — write a module setting. Payload = [settingId, value...].
    static std::vector<uint8_t> CmdSet(uint8_t settingId,
                                        const std::vector<uint8_t>& value);

    /// UNICAST_DATA_REQ (0x0B) — send data to a specific 2-byte address.
    static std::vector<uint8_t> CmdUnicastData(uint8_t destAddrLsb,
                                                uint8_t destAddrMsb,
                                                const std::vector<uint8_t>& data);

    // -----------------------------------------------------------------------
    // Frame parser (stateful — accumulates partial frames across calls)
    // -----------------------------------------------------------------------

    /// Feed raw bytes received from the serial port. Returns all complete
    /// frames found. Incomplete frames are held in an internal buffer and
    /// completed on the next call. Frames with bad checksums are flagged
    /// (checksumOk = false) but still returned so the caller can log them.
    std::vector<Frame> Feed(const std::vector<uint8_t>& bytes);

    /// Clear the internal parse buffer (e.g. on disconnect).
    void Reset();

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    /// Return a human-readable one-line description of a frame.
    static std::string DescribeFrame(const Frame& f);

    /// Format a byte vector as space-separated hex (e.g. "02 05 00 07").
    static std::string HexDump(const std::vector<uint8_t>& bytes);

private:
    std::vector<uint8_t> m_buf; ///< Partial-frame accumulation buffer
};
