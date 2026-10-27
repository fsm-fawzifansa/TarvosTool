#include "TarvosProtocol.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace {

std::string Hex2(uint8_t b) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(b);
    return oss.str();
}

/// Little-endian (LSB first) unsigned value of up to 4 bytes.
uint32_t LittleEndian(const std::vector<uint8_t>& v, size_t from = 0, size_t count = 4) {
    uint32_t out = 0;
    for (size_t i = 0; i < count && from + i < v.size(); ++i)
        out |= static_cast<uint32_t>(v[from + i]) << (8 * i);
    return out;
}

/// Hex dump plus the ASCII text, when every byte is printable.
std::string DataText(const std::vector<uint8_t>& data) {
    std::string s = TarvosProtocol::HexDump(data);
    bool printable = !data.empty() &&
        std::all_of(data.begin(), data.end(), [](uint8_t b) { return b >= 0x20 && b < 0x7F; });
    if (printable) s += "  \"" + std::string(data.begin(), data.end()) + "\"";
    return s;
}

std::string RssiText(uint8_t raw) {
    if (raw == 0x80) return "no RSSI available";
    return std::to_string(static_cast<int8_t>(raw)) + " dBm";
}

std::string OpModeText(uint8_t mode) {
    switch (mode) {
        case TarvosProtocol::OPMODE_TRANSPARENT: return "transparent mode";
        case TarvosProtocol::OPMODE_COMMAND:     return "command mode";
        default:                                 return "unknown mode " + Hex2(mode);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------

const std::vector<uint8_t>& TarvosProtocol::EscapeSequence() {
    static const std::vector<uint8_t> seq{ 0x2B, 0x2B, 0x2B };
    return seq;
}

uint8_t TarvosProtocol::ConfirmationFor(uint8_t requestCmd) {
    // Manual 7.2.2: DATAEX_REQ is answered with CMD_DATA_REQ | 0x40.
    if (requestCmd == CMD_DATAEX_REQ) return CMD_DATA_CNF;
    return static_cast<uint8_t>(requestCmd | 0x40);
}

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
// Addressing
// ---------------------------------------------------------------------------

size_t TarvosProtocol::AddressBytes(int addrMode) {
    switch (addrMode) {
        case 1:  return 1;
        case 2:  return 2;
        case 3:  return 3;
        default: return 0;
    }
}

size_t TarvosProtocol::MaxPacketSize(int rfProfile) {
    switch (rfProfile) {
        case 0:  return 128;
        case 2:  return 128;
        case 4:  return 64;
        case 5:  return 224;
        default: return 0;
    }
}

double TarvosProtocol::ChannelToMHz(int channel) {
    return 863.0 + channel * 0.05;
}

bool TarvosProtocol::ParseDataExInd(const Frame& f, int addrMode, RxData& out) {
    const size_t addrBytes = AddressBytes(addrMode);
    const auto& p = f.payload;
    if (p.size() < addrBytes + 1) return false; // address + RSSI at minimum

    out = RxData{};
    switch (addrMode) {
        case 1: out.addr = p[0]; break;
        case 2: out.netId = p[0]; out.addr = p[1]; break;
        case 3: out.netId = p[0]; out.addr = p[1] | (p[2] << 8); break;
        default: break;
    }
    out.data.assign(p.begin() + addrBytes, p.end() - 1);
    out.rssiDbm = static_cast<int8_t>(p.back());
    return true;
}

// ---------------------------------------------------------------------------
// User settings
// ---------------------------------------------------------------------------

const std::vector<TarvosProtocol::Setting>& TarvosProtocol::Settings() {
    static const std::vector<Setting> settings = {
        {  0, "UART_Baudrate",          4, 4, true,  "9600 - 921600" },
        {  1, "RADIO_DefaultRfProfile", 1, 1, true,  "0, 2, 4, 5" },
        {  2, "RADIO_DefaultRfTXPower", 1, 1, true,  "0 - 14 dBm" },
        {  3, "RADIO_DefaultRfChannel", 1, 1, true,  "0 - 140" },
        {  4, "MAC_DefaultAddressMode", 1, 1, true,  "0 - 3" },
        {  6, "MAC_NumRetrys",          1, 1, true,  "0 - 255" },
        {  7, "MAC_DefaultDestNetID",   1, 1, true,  "0 - 255 (255 = broadcast)" },
        {  8, "MAC_DefaultDestAddr",    1, 2, true,  "0 - 65535 (255 / 65535 = broadcast)" },
        { 10, "MAC_SourceNetID",        1, 1, true,  "0 - 254" },
        { 11, "MAC_SourceAddr",         1, 2, true,  "0 - 65534 (not 255 / 65535)" },
        { 14, "OpMode",                 1, 1, true,  "0x00 transparent, 0x10 command" },
        { 15, "CfgFlags",               2, 2, true,  "bit 0 sniffer, bit 1 duty-cycle indication" },
        { 16, "RpFlags",                2, 2, true,  "bit 0 repeater" },
        { 17, "RP_NumSlots",            1, 1, true,  "1 - 255" },
        { 18, "OpMode_Flags",           1, 1, true,  "bits 0-3, see manual 8.2.12" },
        { 19, "UART_ETX_Character0",    1, 2, true,  "any byte (2 bytes also sets Character1)" },
        { 20, "UART_ETX_Character1",    1, 1, true,  "any byte" },
        { 21, "UART_Timeout",           1, 1, true,  "5 - 255 ms" },
        { 26, "LBT_ObservationPeriod",  1, 1, true,  "0 - 15 ms (0 = LBT off)" },
        { 27, "LBT_Threshold",          1, 1, true,  "-100 - -45 dBm" },
        { 32, "FactorySettings",        8, 8, false, "read-only" },
        { 33, "FirmwareVersion",        3, 3, false, "read-only" },
    };
    return settings;
}

const TarvosProtocol::Setting* TarvosProtocol::FindSetting(uint8_t index) {
    for (const auto& s : Settings())
        if (s.index == index) return &s;
    return nullptr;
}

bool TarvosProtocol::ValidateSetting(uint8_t index, const std::vector<uint8_t>& value,
                                     std::string& err) {
    const Setting* s = FindSetting(index);
    if (!s) { err = "unknown setting index " + Hex2(index); return false; }
    if (!s->writable) { err = std::string(s->name) + " is read-only"; return false; }

    if (value.size() < s->minLen || value.size() > s->maxLen) {
        std::ostringstream oss;
        oss << s->name << " takes " << static_cast<int>(s->minLen);
        if (s->maxLen != s->minLen) oss << "-" << static_cast<int>(s->maxLen);
        oss << " byte(s), LSB first";
        err = oss.str();
        return false;
    }

    const uint32_t v = LittleEndian(value, 0, value.size());
    auto outOfRange = [&]() {
        err = std::string(s->name) + " must be " + s->range;
        return false;
    };

    switch (index) {
        case 0:  if (v < 9600 || v > 921600) return outOfRange(); break;
        case 1:  if (v != 0 && v != 2 && v != 4 && v != 5) return outOfRange(); break;
        case 2:  if (v > 14) return outOfRange(); break;
        case 3:  if (v > 140) return outOfRange(); break;
        case 4:  if (v > 3) return outOfRange(); break;
        case 10: if (v > 254) return outOfRange(); break;
        case 11: if ((value.size() == 1 && v == 0xFF) || v == 0xFFFF) return outOfRange(); break;
        case 14: if (v != OPMODE_TRANSPARENT && v != OPMODE_COMMAND) return outOfRange(); break;
        case 15: if (v & 0xFFFC) return outOfRange(); break;
        case 16: if (v & 0xFFFE) return outOfRange(); break;
        case 17: if (v < 1) return outOfRange(); break;
        case 18: if (v & 0xF0) return outOfRange(); break;
        case 21: if (v < 5) return outOfRange(); break;
        case 26: if (v > 15) return outOfRange(); break;
        case 27: {
            int dbm = static_cast<int8_t>(value[0]);
            if (dbm < -100 || dbm > -45) return outOfRange();
            break;
        }
        default: break;
    }
    return true;
}

std::string TarvosProtocol::DescribeSettingValue(uint8_t index, const std::vector<uint8_t>& value) {
    if (value.empty()) return "(no value)";

    const uint32_t v = LittleEndian(value, 0, value.size());
    std::ostringstream oss;

    switch (index) {
        case 0:  oss << v << " baud"; break;
        case 1: {
            static const char* profiles[] = {
                "38.4 kcps FSK, max 128 B", "", "100 kcps FSK, max 128 B", "",
                "long range 2.5 kbps, max 64 B", "400 kcps GFSK, max 224 B" };
            oss << "profile " << v;
            if (v <= 5 && *profiles[v]) oss << " (" << profiles[v] << ")";
            break;
        }
        case 2:  oss << static_cast<int>(static_cast<int8_t>(value[0])) << " dBm"; break;
        case 3:
            oss << v << " (" << std::fixed << std::setprecision(3)
                << ChannelToMHz(static_cast<int>(v)) << " MHz)";
            break;
        case 4: {
            static const char* modes[] = { "no addressing, all broadcast", "1-byte address",
                                           "net ID + 1-byte address", "net ID + 2-byte address" };
            oss << "mode " << v;
            if (v <= 3) oss << " (" << modes[v] << ")";
            break;
        }
        case 6:  oss << v << " retries" << (v == 0 ? " (ACK off)" : ""); break;
        case 7:  oss << v << (v == 255 ? " (broadcast)" : ""); break;
        case 8:
            oss << v << ((value.size() == 1 ? v == 0xFF : v == 0xFFFF) ? " (broadcast)" : "");
            break;
        case 10: oss << v; break;
        case 11: oss << v; break;
        case 14: oss << OpModeText(static_cast<uint8_t>(v)); break;
        case 15:
            oss << Hex2(static_cast<uint8_t>(v)) << " (sniffer " << ((v & 1) ? "on" : "off")
                << ", duty-cycle indication " << ((v & 2) ? "on" : "off") << ")";
            break;
        case 16: oss << "repeater " << ((v & 1) ? "on" : "off"); break;
        case 17: oss << v << " slots"; break;
        case 18:
            oss << Hex2(static_cast<uint8_t>(v)) << " (timeout trigger " << ((v & 0x04) ? "on" : "off")
                << ", ETX trigger " << ((v & 0x02) ? "on" : "off") << ")";
            break;
        case 19:
        case 20: oss << HexDump(value); break;
        case 21: oss << v << " ms"; break;
        case 26: oss << v << " ms" << (v == 0 ? " (LBT off)" : ""); break;
        case 27: oss << static_cast<int>(static_cast<int8_t>(value[0])) << " dBm"; break;
        case 32:
            if (value.size() >= 8) {
                oss << "serial " << static_cast<int>(value[3]) << "."
                    << std::setw(6) << std::setfill('0') << LittleEndian(value, 0, 3)
                    << std::setfill(' ')
                    << ", hardware " << static_cast<int>(value[6]) << "."
                    << static_cast<int>(value[5]) << "." << static_cast<int>(value[4])
                    << ", frequency correction " << static_cast<int>(static_cast<int8_t>(value[7]));
            } else {
                oss << HexDump(value);
            }
            break;
        case 33:
            // Sequence in the parameter is patch, minor, major.
            if (value.size() >= 3)
                oss << "firmware " << static_cast<int>(value[2]) << "."
                    << static_cast<int>(value[1]) << "." << static_cast<int>(value[0]);
            else
                oss << HexDump(value);
            break;
        default: oss << HexDump(value); break;
    }
    return oss.str();
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

std::string TarvosProtocol::StatusText(uint8_t cnfCmd, uint8_t status) {
    switch (cnfCmd) {
        case CMD_DATA_CNF:
            switch (status) {
                case 0x00: return "ACK received or not requested";
                case 0x01: return "no ACK received after all retries";
                case 0x02: return "invalid channel selected";
                case 0x03: return "radio channel busy (LBT)";
                case 0x04: return "module busy";
                case 0xFF: return "invalid (payload too long)";
                default:   return "unknown status";
            }
        case CMD_SET_CNF:
            return status == 0x00 ? "success" : status == 0x01 ? "failed: invalid parameter"
                                                               : "unknown status";
        case CMD_REPEAT_IND:
            switch (status) {
                case 0x00: return "OK";
                case 0x01: return "failed";
                case 0x03: return "radio channel busy (LBT)";
                case 0x04: return "module busy";
                default:   return "unknown status";
            }
        default:
            return status == 0x00 ? "OK" : status == 0x01 ? "request not successful"
                                                          : "unknown status";
    }
}

std::string TarvosProtocol::DescribeFrame(const Frame& f, int addrMode) {
    std::ostringstream oss;
    const auto& p = f.payload;

    auto status = [&](const char* name) {
        oss << name;
        if (!p.empty()) oss << " status=" << Hex2(p[0]) << " (" << StatusText(f.cmd, p[0]) << ")";
    };

    switch (f.cmd) {
        // --- Requests (host → module) ---
        case CMD_DATA_REQ:
            oss << "DATA_REQ (0x00) — send to configured destination, data: " << DataText(p);
            break;
        case CMD_DATAEX_REQ: {
            oss << "DATAEX_REQ (0x01)";
            const size_t addrBytes = AddressBytes(addrMode);
            if (p.size() >= 1 + addrBytes) {
                oss << " ch=" << static_cast<int>(p[0]);
                if (addrMode >= 2) oss << " netID=" << static_cast<int>(p[1]);
                if (addrMode == 1) oss << " addr=" << static_cast<int>(p[1]);
                if (addrMode == 2) oss << " addr=" << static_cast<int>(p[2]);
                if (addrMode == 3) oss << " addr=" << LittleEndian(p, 2, 2);
                oss << " data: " << DataText(std::vector<uint8_t>(p.begin() + 1 + addrBytes, p.end()));
            }
            break;
        }
        case CMD_SET_MODE_REQ:
            oss << "SET_MODE_REQ (0x04)";
            if (!p.empty()) oss << " → " << OpModeText(p[0]);
            break;
        case CMD_RESET_REQ:
            oss << "RESET_REQ (0x05) — software reset";
            break;
        case CMD_SET_CHANNEL_REQ:
            oss << "SET_CHANNEL_REQ (0x06)";
            if (!p.empty())
                oss << " ch=" << static_cast<int>(p[0]) << " (" << std::fixed << std::setprecision(3)
                    << ChannelToMHz(p[0]) << " MHz)";
            break;
        case CMD_SET_DESTNETID_REQ:
            oss << "SET_DESTNETID_REQ (0x07)";
            if (!p.empty()) oss << " netID=" << static_cast<int>(p[0]);
            break;
        case CMD_SET_DESTADDR_REQ:
            oss << "SET_DESTADDR_REQ (0x08)";
            if (!p.empty()) oss << " addr=" << LittleEndian(p, 0, p.size());
            break;
        case CMD_SET_REQ:
            oss << "SET_REQ (0x09)";
            if (!p.empty()) {
                const Setting* s = FindSetting(p[0]);
                oss << " " << (s ? s->name : "index " + Hex2(p[0]));
                if (p.size() > 1)
                    oss << " = " << DescribeSettingValue(p[0], std::vector<uint8_t>(p.begin() + 1, p.end()));
            }
            break;
        case CMD_GET_REQ:
            oss << "GET_REQ (0x0A)";
            if (!p.empty()) {
                const Setting* s = FindSetting(p[0]);
                oss << " " << (s ? s->name : "index " + Hex2(p[0]));
            }
            break;
        case CMD_RSSI_REQ:      oss << "RSSI_REQ (0x0D) — RSSI of last received packet"; break;
        case CMD_SHUTDOWN_REQ:  oss << "SHUTDOWN_REQ (0x0E)"; break;
        case CMD_STANDBY_REQ:   oss << "STANDBY_REQ (0x0F)"; break;
        case CMD_SET_PAPOWER_REQ:
            oss << "SET_PAPOWER_REQ (0x11)";
            if (!p.empty()) oss << " power=" << static_cast<int>(static_cast<int8_t>(p[0])) << " dBm";
            break;
        case CMD_FACTORY_RESET_REQ:
            oss << "FACTORY_RESET_REQ (0x12) — restore defaults + reset";
            break;

        // --- Confirmations (module → host) ---
        case CMD_DATA_CNF:          status("DATA_CNF (0x40)"); break;
        case CMD_SET_MODE_CNF:
            status("SET_MODE_CNF (0x44)");
            if (p.size() >= 2) oss << ", now in " << OpModeText(p[1]);
            break;
        case CMD_RESET_CNF:         status("RESET_CNF (0x45)"); break;
        case CMD_SET_CHANNEL_CNF:
            oss << "SET_CHANNEL_CNF (0x46)";
            if (!p.empty())
                oss << " channel now " << static_cast<int>(p[0]) << " (" << std::fixed
                    << std::setprecision(3) << ChannelToMHz(p[0]) << " MHz)";
            break;
        case CMD_SET_DESTNETID_CNF: status("SET_DESTNETID_CNF (0x47)"); break;
        case CMD_SET_DESTADDR_CNF:  status("SET_DESTADDR_CNF (0x48)"); break;
        case CMD_SET_CNF:           status("SET_CNF (0x49)"); break;
        case CMD_GET_CNF:
            status("GET_CNF (0x4A)");
            if (p.size() > 1)
                oss << " value=" << HexDump(std::vector<uint8_t>(p.begin() + 1, p.end()));
            break;
        case CMD_RSSI_CNF:
            oss << "RSSI_CNF (0x4D)";
            if (!p.empty()) oss << " " << RssiText(p[0]);
            break;
        case CMD_SHUTDOWN_CNF:      status("SHUTDOWN_CNF (0x4E)"); break;
        case CMD_STANDBY_CNF:       status("STANDBY_CNF (0x4F)"); break;
        case CMD_SET_PAPOWER_CNF:
            oss << "SET_PAPOWER_CNF (0x51)";
            if (!p.empty()) oss << " power now " << static_cast<int>(static_cast<int8_t>(p[0])) << " dBm";
            break;
        case CMD_FACTORY_RESET_CNF: status("FACTORY_RESET_CNF (0x52)"); break;

        // --- Indications (module → host, spontaneous) ---
        case CMD_REPEAT_IND:
            status("REPEAT_IND (0x80) — packet repeated");
            if (p.size() >= 2) oss << ", address mode " << static_cast<int>(p[1]);
            if (p.size() > 2) oss << ", addresses " << HexDump(std::vector<uint8_t>(p.begin() + 2, p.end()));
            break;
        case CMD_DATAEX_IND: {
            oss << "DATAEX_IND (0x81) — incoming radio data";
            RxData rx;
            if (ParseDataExInd(f, addrMode, rx)) {
                if (rx.netId >= 0) oss << " from netID=" << rx.netId;
                if (rx.addr >= 0)  oss << " addr=" << rx.addr;
                oss << " data: " << DataText(rx.data) << "  RSSI " << RssiText(p.back());
            }
            break;
        }
        case CMD_RESET_IND:
            oss << "RESET_IND (0x85) — module started";
            if (!p.empty()) oss << " in " << OpModeText(p[0]);
            break;
        case CMD_STANDBY_IND:
            oss << "STANDBY_IND (0x8F) — woke up from standby";
            if (!p.empty() && p[0] != 0x00) oss << " status=" << Hex2(p[0]);
            break;
        case CMD_DUTYCYCLE_IND:
            oss << "DUTYCYCLE_IND (0x90)";
            if (p.size() >= 4) oss << " TX time " << LittleEndian(p) << " us";
            break;

        default:
            oss << "UNKNOWN CMD (" << Hex2(f.cmd) << ")";
            if (!p.empty()) oss << " payload: " << HexDump(p);
            break;
    }

    if (!f.checksumOk) oss << "  [CHECKSUM ERROR]";
    return oss.str();
}
