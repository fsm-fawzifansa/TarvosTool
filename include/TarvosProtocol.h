#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// WE-ProWare binary frame protocol for the Würth Elektronik Tarvos-e module.
/// Reference: Tarvos-e user manual v2.18, chapter 7 (command interface) and
/// chapter 8 (user settings).
///
/// Frame layout:
///   [STX=0x02] [CMD] [LEN] [PAYLOAD...] [CS]
///   LEN = 1 byte (number of payload bytes, 0–255)
///   CS  = XOR of all preceding bytes (STX through last payload byte)
///
/// Confirmation codes = REQ | 0x40  (exception: DATAEX_REQ is confirmed by DATA_CNF)
/// Indication codes   = REQ | 0x80
class TarvosProtocol {
public:
    // -----------------------------------------------------------------------
    // Protocol constant
    // -----------------------------------------------------------------------
    static constexpr uint8_t STX = 0x02;

    // -----------------------------------------------------------------------
    // Request command codes (host → module), manual table 12
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_DATA_REQ           = 0x00; ///< Send data to the configured destination
    static constexpr uint8_t CMD_DATAEX_REQ         = 0x01; ///< Send data with channel + address in the frame
    static constexpr uint8_t CMD_SET_MODE_REQ       = 0x04; ///< Switch command → transparent mode
    static constexpr uint8_t CMD_RESET_REQ          = 0x05; ///< Software reset (frame: 02 05 00 07)
    static constexpr uint8_t CMD_SET_CHANNEL_REQ    = 0x06; ///< Set RF channel (volatile)
    static constexpr uint8_t CMD_SET_DESTNETID_REQ  = 0x07; ///< Set destination network ID (volatile)
    static constexpr uint8_t CMD_SET_DESTADDR_REQ   = 0x08; ///< Set destination address (volatile)
    static constexpr uint8_t CMD_SET_REQ            = 0x09; ///< Write non-volatile user setting
    static constexpr uint8_t CMD_GET_REQ            = 0x0A; ///< Read non-volatile user setting
    static constexpr uint8_t CMD_RSSI_REQ           = 0x0D; ///< Read RSSI of the last received packet
    static constexpr uint8_t CMD_SHUTDOWN_REQ       = 0x0E; ///< Enter shutdown mode
    static constexpr uint8_t CMD_STANDBY_REQ        = 0x0F; ///< Enter standby mode
    static constexpr uint8_t CMD_SET_PAPOWER_REQ    = 0x11; ///< Set TX power (volatile)
    static constexpr uint8_t CMD_FACTORY_RESET_REQ  = 0x12; ///< Restore defaults + software reset

    // -----------------------------------------------------------------------
    // Confirmation codes (module → host, in response to a request)
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_DATA_CNF           = 0x40; ///< Confirms DATA_REQ and DATAEX_REQ
    static constexpr uint8_t CMD_SET_MODE_CNF       = 0x44;
    static constexpr uint8_t CMD_RESET_CNF          = 0x45;
    static constexpr uint8_t CMD_SET_CHANNEL_CNF    = 0x46;
    static constexpr uint8_t CMD_SET_DESTNETID_CNF  = 0x47;
    static constexpr uint8_t CMD_SET_DESTADDR_CNF   = 0x48;
    static constexpr uint8_t CMD_SET_CNF            = 0x49;
    static constexpr uint8_t CMD_GET_CNF            = 0x4A;
    static constexpr uint8_t CMD_RSSI_CNF           = 0x4D;
    static constexpr uint8_t CMD_SHUTDOWN_CNF       = 0x4E;
    static constexpr uint8_t CMD_STANDBY_CNF        = 0x4F;
    static constexpr uint8_t CMD_SET_PAPOWER_CNF    = 0x51;
    static constexpr uint8_t CMD_FACTORY_RESET_CNF  = 0x52;

    // -----------------------------------------------------------------------
    // Indication codes (module → host, spontaneous events)
    // -----------------------------------------------------------------------
    static constexpr uint8_t CMD_REPEAT_IND         = 0x80; ///< Packet repeated (repeater mode)
    static constexpr uint8_t CMD_DATAEX_IND         = 0x81; ///< Incoming radio data
    static constexpr uint8_t CMD_RESET_IND          = 0x85; ///< Module (re)started
    static constexpr uint8_t CMD_STANDBY_IND        = 0x8F; ///< Module woke up from standby
    static constexpr uint8_t CMD_DUTYCYCLE_IND      = 0x90; ///< TX time since last indication (CfgFlags bit 1)

    // -----------------------------------------------------------------------
    // Operating modes (SET_MODE_REQ / SET_MODE_CNF / RESET_IND / OpMode)
    // -----------------------------------------------------------------------
    static constexpr uint8_t OPMODE_TRANSPARENT     = 0x00;
    static constexpr uint8_t OPMODE_COMMAND         = 0x10;

    /// Escape sequence that switches transparent → command mode ("+++").
    static const std::vector<uint8_t>& EscapeSequence();

    /// Confirmation code the module answers a request with.
    static uint8_t ConfirmationFor(uint8_t requestCmd);

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
    // Addressing (MAC_DefaultAddressMode, manual 5.3.1 / 8.2.5)
    // -----------------------------------------------------------------------

    /// Address bytes (net ID + address) carried in a frame for an address mode:
    /// mode 0 → 0, mode 1 → 1 (addr), mode 2 → 2 (net ID, addr), mode 3 → 3 (net ID, addr LSB/MSB).
    static size_t AddressBytes(int addrMode);

    /// Maximum radio packet size for an RF profile (manual table 13), or 0 if unknown.
    static size_t MaxPacketSize(int rfProfile);

    /// RF channel → centre frequency in MHz (863 MHz + channel × 50 kHz).
    static double ChannelToMHz(int channel);

    /// Decoded CMD_DATAEX_IND.
    struct RxData {
        int                  netId = -1;   ///< -1 if not present in this address mode
        int                  addr  = -1;   ///< -1 if not present in this address mode
        std::vector<uint8_t> data;
        int                  rssiDbm = 0;
    };

    /// Split a DATAEX_IND payload into sender address, data and RSSI according
    /// to the address mode. Returns false if the payload is too short.
    static bool ParseDataExInd(const Frame& f, int addrMode, RxData& out);

    // -----------------------------------------------------------------------
    // User settings (manual chapter 8)
    // -----------------------------------------------------------------------
    struct Setting {
        uint8_t     index;
        const char* name;
        uint8_t     minLen;   ///< Parameter size in bytes (LSB first)
        uint8_t     maxLen;
        bool        writable;
        const char* range;    ///< Permissible values, as stated in the manual
    };

    static const std::vector<Setting>& Settings();
    static const Setting* FindSetting(uint8_t index);

    /// Check a SET_REQ value against the setting's size and permissible values.
    /// The module does not validate SET_REQ itself — a bad value can make it
    /// unreachable — so this is the only line of defence.
    static bool ValidateSetting(uint8_t index, const std::vector<uint8_t>& value,
                                std::string& err);

    /// Human-readable form of a setting value, e.g. "115200 baud" or "106 (868.300 MHz)".
    static std::string DescribeSettingValue(uint8_t index, const std::vector<uint8_t>& value);

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

    /// True while a partial frame is waiting for more bytes.
    bool HasPartialFrame() const { return !m_buf.empty(); }

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    /// Return a human-readable one-line description of a frame. DATAEX frames
    /// are decoded according to addrMode.
    static std::string DescribeFrame(const Frame& f, int addrMode = 0);

    /// Meaning of a status byte in a confirmation, e.g. "no ACK received".
    static std::string StatusText(uint8_t cnfCmd, uint8_t status);

    /// Format a byte vector as space-separated hex (e.g. "02 05 00 07").
    static std::string HexDump(const std::vector<uint8_t>& bytes);

private:
    std::vector<uint8_t> m_buf; ///< Partial-frame accumulation buffer
};
