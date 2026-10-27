#include "TarvosProtocol.h"

#include <sstream>
#include <iomanip>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Frame building
// ---------------------------------------------------------------------------

std::vector<uint8_t> TarvosProtocol::BuildFrame(uint8_t cmd,
                                                  const std::vector<uint8_t>& payload) {
    if (payload.size() > 255) {
        throw std::invalid_argument("Payload exceeds 255 bytes (Tarvos LEN field is 1 byte)");
    }

    const uint8_t len = static_cast<uint8_t>(payload.size());

    // Wire layout: STX | CMD | LEN(1 byte) | PAYLOAD... | CS
    std::vector<uint8_t> frame;
    frame.reserve(3 + payload.size() + 1);
    frame.push_back(STX);
    frame.push_back(cmd);
    frame.push_back(len);
    frame.insert(frame.end(), payload.begin(), payload.end());

    frame.push_back(CalcChecksum(frame));
    return frame;
}

uint8_t TarvosProtocol::CalcChecksum(const std::vector<uint8_t>& bytes) {
    uint8_t cs = 0;
    for (uint8_t b : bytes) cs ^= b;
    return cs;
}

// ---------------------------------------------------------------------------
// Convenience command builders
// ---------------------------------------------------------------------------

std::vector<uint8_t> TarvosProtocol::CmdPing() {
    // Tarvos has no dedicated ping command. Read setting index 0 (UART_Baudrate)
    // as a functional alive-check — module responds with CMD_GET_CNF (0x4A).
    return BuildFrame(CMD_GET_REQ, {0x00});
}

std::vector<uint8_t> TarvosProtocol::CmdReset() {
    // Datasheet: 02 05 00 07  (STX | 0x05 | LEN=0 | CS=XOR(02,05,00)=07)
    return BuildFrame(CMD_RESET_REQ);
}

std::vector<uint8_t> TarvosProtocol::CmdSleep() {
    return BuildFrame(CMD_STANDBY_REQ);
}

std::vector<uint8_t> TarvosProtocol::CmdGet(uint8_t settingId) {
    // Datasheet: 02 0A 01 SettingsIndex CS
    return BuildFrame(CMD_GET_REQ, {settingId});
}

std::vector<uint8_t> TarvosProtocol::CmdSet(uint8_t settingId,
                                              const std::vector<uint8_t>& value) {
    std::vector<uint8_t> payload;
    payload.push_back(settingId);
    payload.insert(payload.end(), value.begin(), value.end());
    return BuildFrame(CMD_SET_REQ, payload);
}

std::vector<uint8_t> TarvosProtocol::CmdUnicastData(uint8_t destAddrLsb,
                                                      uint8_t destAddrMsb,
                                                      const std::vector<uint8_t>& data) {
    std::vector<uint8_t> payload;
    payload.push_back(destAddrLsb);
    payload.push_back(destAddrMsb);
    payload.insert(payload.end(), data.begin(), data.end());
    return BuildFrame(CMD_UNICAST_DATA_REQ, payload);
}

// ---------------------------------------------------------------------------
// Stateful frame parser
// ---------------------------------------------------------------------------

std::vector<TarvosProtocol::Frame> TarvosProtocol::Feed(const std::vector<uint8_t>& bytes) {
    m_buf.insert(m_buf.end(), bytes.begin(), bytes.end());

    std::vector<Frame> frames;

    while (true) {
        // Scan for STX
        auto it = std::find(m_buf.begin(), m_buf.end(), STX);
        if (it == m_buf.end()) {
            m_buf.clear();
            break;
        }

        // Discard bytes before STX
        if (it != m_buf.begin()) m_buf.erase(m_buf.begin(), it);

        // Minimum frame: STX + CMD + LEN + CS = 4 bytes
        if (m_buf.size() < 4) break;

        // LEN is a single byte (Tarvos protocol — NOT 2-byte little-endian)
        const uint8_t payloadLen = m_buf[2];

        // Total frame: STX(1) + CMD(1) + LEN(1) + PAYLOAD(payloadLen) + CS(1)
        const size_t frameLen = 3u + payloadLen + 1u;

        if (m_buf.size() < frameLen) break; // Wait for more bytes

        const uint8_t receivedCs = m_buf[frameLen - 1];
        const uint8_t computedCs =
            CalcChecksum(std::vector<uint8_t>(m_buf.begin(), m_buf.begin() + frameLen - 1));

        Frame f;
        f.cmd        = m_buf[1];
        f.payload    = std::vector<uint8_t>(m_buf.begin() + 3,
                                             m_buf.begin() + 3 + payloadLen);
        f.checksumOk = (receivedCs == computedCs);

        frames.push_back(std::move(f));
        m_buf.erase(m_buf.begin(), m_buf.begin() + frameLen);
    }

    return frames;
}

void TarvosProtocol::Reset() {
    m_buf.clear();
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

std::string TarvosProtocol::HexDump(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) oss << ' ';
        oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

std::string TarvosProtocol::DescribeFrame(const Frame& f) {
    std::ostringstream oss;

    switch (f.cmd) {
        // --- Requests (host → module) ---
        case CMD_DATA_REQ:
            oss << "DATA_REQ (0x00) — send payload to configured destination";
            break;
        case CMD_DATAEX_REQ:
            oss << "DATAEX_REQ (0x01) — send payload with extended addressing";
            break;
        case CMD_SET_MODE_REQ:
            oss << "SET_MODE_REQ (0x04)";
            if (!f.payload.empty())
                oss << " mode=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;
        case CMD_RESET_REQ:
            oss << "RESET_REQ (0x05) — software reset";
            break;
        case CMD_SET_CHANNEL_REQ:
            oss << "SET_CHANNEL_REQ (0x06)";
            if (!f.payload.empty())
                oss << " ch=" << static_cast<int>(f.payload[0]);
            break;
        case CMD_SET_DESTNETID_REQ:
            oss << "SET_DESTNETID_REQ (0x07)";
            if (!f.payload.empty())
                oss << " netid=" << HexDump(f.payload);
            break;
        case CMD_SET_DESTADDR_REQ:
            oss << "SET_DESTADDR_REQ (0x08)";
            if (!f.payload.empty())
                oss << " addr=" << HexDump(f.payload);
            break;
        case CMD_SET_REQ:
            oss << "SET_REQ (0x09)";
            if (!f.payload.empty()) {
                oss << " settingId=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
                if (f.payload.size() > 1)
                    oss << " value=" << HexDump(
                        std::vector<uint8_t>(f.payload.begin() + 1, f.payload.end()));
            }
            break;
        case CMD_GET_REQ:
            oss << "GET_REQ (0x0A)";
            if (!f.payload.empty())
                oss << " settingId=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;
        case CMD_RSSI_REQ:
            oss << "RSSI_REQ (0x0D) — read last RSSI";
            break;
        case CMD_SHUTDOWN_REQ:
            oss << "SHUTDOWN_REQ (0x0E)";
            break;
        case CMD_STANDBY_REQ:
            oss << "STANDBY_REQ (0x0F) — enter standby";
            break;
        case CMD_SET_PAPOWER_REQ:
            oss << "SET_PAPOWER_REQ (0x10)";
            if (!f.payload.empty())
                oss << " power=" << static_cast<int>(f.payload[0]);
            break;
        case CMD_FACTORY_RESET_REQ:
            oss << "FACTORY_RESET_REQ (0x12) — restore defaults + reset";
            break;
        case CMD_UNICAST_DATA_REQ:
            oss << "UNICAST_DATA_REQ (0x0B)";
            if (f.payload.size() >= 2) {
                oss << " → dest 0x"
                    << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[1])
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
                if (f.payload.size() > 2)
                    oss << " data: " << HexDump(
                        std::vector<uint8_t>(f.payload.begin() + 2, f.payload.end()));
            }
            break;

        // --- Confirmations (module → host) ---
        case CMD_DATA_CNF:
            oss << "DATA_CNF (0x40)";
            if (!f.payload.empty())
                oss << " status=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;
        case CMD_RESET_CNF:
            oss << "RESET_CNF (0x45)";
            if (!f.payload.empty())
                oss << " status=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;
        case CMD_SET_CNF:
            oss << "SET_CNF (0x49)";
            if (!f.payload.empty())
                oss << " status=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0])
                    << (f.payload[0] == 0x00 ? " (success)"
                        : f.payload[0] == 0x01 ? " (invalid parameter)" : "");
            break;
        case CMD_GET_CNF:
            oss << "GET_CNF (0x4A)";
            if (!f.payload.empty()) {
                oss << " status=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
                if (f.payload.size() > 1)
                    oss << " value=" << HexDump(
                        std::vector<uint8_t>(f.payload.begin() + 1, f.payload.end()));
            }
            break;
        case CMD_UNICAST_DATA_CNF:
            oss << "UNICAST_DATA_CNF (0x0C)";
            if (!f.payload.empty())
                oss << " status=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;
        case CMD_FACTORY_RESET_CNF:
            oss << "FACTORY_RESET_CNF (0x52)";
            if (!f.payload.empty())
                oss << " status=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;

        // --- Indications (module → host, spontaneous) ---
        case CMD_DATAEX_IND:
            // Format: ... payload | RSSI | CS  (RSSI is the last payload byte)
            oss << "DATAEX_IND (0x81) — incoming radio data";
            if (!f.payload.empty()) {
                if (f.payload.size() > 1) {
                    oss << " data: " << HexDump(
                        std::vector<uint8_t>(f.payload.begin(), f.payload.end() - 1));
                }
                oss << " RSSI=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0')
                    << static_cast<int>(f.payload.back());
            }
            break;
        case CMD_RESET_IND:
            oss << "RESET_IND (0x85) — module ready after boot/reset";
            if (!f.payload.empty())
                oss << " reason=0x" << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0') << static_cast<int>(f.payload[0]);
            break;
        case CMD_STANDBY_IND:
            oss << "STANDBY_IND (0x8F) — module entered standby";
            break;

        default:
            oss << "UNKNOWN CMD (0x" << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0') << static_cast<int>(f.cmd) << ")";
            if (!f.payload.empty())
                oss << " payload: " << HexDump(f.payload);
            break;
    }

    if (!f.checksumOk) oss << "  [CHECKSUM ERROR]";
    return oss.str();
}
