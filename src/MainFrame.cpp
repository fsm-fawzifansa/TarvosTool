#include "MainFrame.h"
#include "TarvosProtocol.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/msgdlg.h>
#include <wx/datetime.h>
#include <wx/menu.h>

#include <sstream>
#include <iomanip>
#include <cctype>

// ---------------------------------------------------------------------------
// Command table
// Every documented request command and its UI parameter configuration.
// ---------------------------------------------------------------------------
struct CmdInfo {
    uint8_t     code;
    const char* name;
    const char* desc;
    // Up to 2 parameters.  nullptr = row is hidden for this command.
    const char* p1Label;
    const char* p1Hint;
    const char* p2Label;
    const char* p2Hint;
};

static const CmdInfo kCmds[] = {
//  code   name                  description
//  p1Label                      p1Hint    p2Label               p2Hint
  { 0x00, "DATA_REQ",            "Send payload to currently configured destination",
    nullptr,                     nullptr,  "Data (hex bytes):",  "48 65 6C 6C 6F"  },

  { 0x01, "DATAEX_REQ",          "Send payload with extended addressing",
    "Dest Addr (2 B, LSB 1st):", "00 01",  "Data (hex bytes):",  "48 65 6C 6C 6F"  },

  { 0x04, "SET_MODE_REQ",        "Switch operating mode",
    "Mode (1 byte, hex):",       "00",     nullptr,              nullptr            },

  { 0x05, "RESET_REQ",           "Software reset  —  no payload",
    nullptr,                     nullptr,  nullptr,              nullptr            },

  { 0x06, "SET_CHANNEL_REQ",     "Set RF channel",
    "Channel (1 byte, hex):",    "00",     nullptr,              nullptr            },

  { 0x07, "SET_DESTNETID_REQ",   "Set destination network ID",
    "Net ID (hex bytes):",       "00 01",  nullptr,              nullptr            },

  { 0x08, "SET_DESTADDR_REQ",    "Set destination address",
    "Dest Addr (2 B, LSB 1st):", "00 01",  nullptr,              nullptr            },

  { 0x09, "SET_REQ",             "Write non-volatile user setting",
    "Setting Index (1 B, hex):", "00",     "Value (hex bytes):", "03 84"            },

  { 0x0A, "GET_REQ",             "Read non-volatile user setting",
    "Setting Index (1 B, hex):", "00",     nullptr,              nullptr            },

  { 0x0B, "UNICAST_DATA_REQ",    "Send data to a specific address",
    "Dest Addr (2 B, LSB 1st):", "00 01",  "Data (hex bytes):",  "48 65 6C 6C 6F"  },

  { 0x0D, "RSSI_REQ",            "Read last received signal strength  —  no payload",
    nullptr,                     nullptr,  nullptr,              nullptr            },

  { 0x0E, "SHUTDOWN_REQ",        "Shut down module  —  no payload",
    nullptr,                     nullptr,  nullptr,              nullptr            },

  { 0x0F, "STANDBY_REQ",         "Enter standby / sleep  —  no payload",
    nullptr,                     nullptr,  nullptr,              nullptr            },

  { 0x10, "SET_PAPOWER_REQ",     "Set PA output power level",
    "PA Power (1 byte, hex):",   "04",     nullptr,              nullptr            },

  { 0x12, "FACTORY_RESET_REQ",   "Restore defaults + reset  —  no payload",
    nullptr,                     nullptr,  nullptr,              nullptr            },
};
static constexpr int kCmdCount = static_cast<int>(sizeof(kCmds) / sizeof(kCmds[0]));

// ---------------------------------------------------------------------------
// Event IDs
// ---------------------------------------------------------------------------
namespace ID {
    enum {
        NEW_WINDOW = wxID_HIGHEST + 1,
        REFRESH_PORTS,
        CONNECT,
        CMD_CHOICE,
        CMD_PARAM1,
        CMD_PARAM2,
        SEND_COMMAND,
        POLL_TIMER,
        OPEN_CHAT,
    };
}

// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID::NEW_WINDOW,      MainFrame::OnNewWindow)
    EVT_BUTTON(ID::REFRESH_PORTS, MainFrame::OnRefreshPorts)
    EVT_BUTTON(ID::CONNECT,       MainFrame::OnConnect)
    EVT_CHOICE(ID::CMD_CHOICE,    MainFrame::OnCommandSelected)
    EVT_TEXT(ID::CMD_PARAM1,      MainFrame::OnParamChanged)
    EVT_TEXT(ID::CMD_PARAM2,      MainFrame::OnParamChanged)
    EVT_BUTTON(ID::SEND_COMMAND,  MainFrame::OnSendCommand)
    EVT_BUTTON(ID::OPEN_CHAT,     MainFrame::OnOpenChat)
    EVT_TIMER(ID::POLL_TIMER,     MainFrame::OnTimer)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "TarvosTool",
              wxDefaultPosition, wxSize(820, 560))
    , m_timer(this, ID::POLL_TIMER)
{
    SetMinSize(wxSize(680, 480));
    BuildMenu();

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(6);
    BuildConnectionBar(root);
    root->AddSpacer(6);
    BuildLogPane(root);
    root->AddSpacer(6);
    BuildCommandPanel(root);
    root->AddSpacer(6);

    auto* chatRow = new wxBoxSizer(wxHORIZONTAL);
    chatRow->AddStretchSpacer();
    m_chatBtn = new wxButton(this, ID::OPEN_CHAT, "Open Chat",
                             wxDefaultPosition, wxSize(120, 32));
    chatRow->Add(m_chatBtn, 0, wxALIGN_CENTER_VERTICAL);
    chatRow->AddSpacer(8);
    root->Add(chatRow, 0, wxEXPAND | wxBOTTOM, 8);

    SetSizer(root);
    CreateStatusBar(2);

    PopulatePorts();
    UpdateConnectedState(false);

    m_cmdChoice->SetSelection(0);
    UpdateCommandUI();

    Centre();
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void MainFrame::BuildMenu() {
    auto* menuBar  = new wxMenuBar();
    auto* fileMenu = new wxMenu();
    fileMenu->Append(ID::NEW_WINDOW, "New Window\tCtrl+N",
                     "Open a second window for a second device");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "Quit\tCtrl+Q");
    menuBar->Append(fileMenu, "File");
    SetMenuBar(menuBar);
}

void MainFrame::OnNewWindow(wxCommandEvent&) {
    (new MainFrame())->Show(true);
}

void MainFrame::OnOpenChat(wxCommandEvent&) {
    if (!m_chat)
        m_chat = new ChatFrame(this, m_serial);

    m_chat->Show(true);
    m_chat->Raise();
}

// ---------------------------------------------------------------------------
// Connection bar
// ---------------------------------------------------------------------------
void MainFrame::BuildConnectionBar(wxSizer* parent) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);

    row->AddSpacer(8);
    row->Add(new wxStaticText(this, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(4);

    m_portChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(230, -1));
    row->Add(m_portChoice, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(6);

    auto* refreshBtn = new wxButton(this, ID::REFRESH_PORTS, "Refresh",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    row->Add(refreshBtn, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(14);

    row->Add(new wxStaticText(this, wxID_ANY, "Baud:"), 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(4);

    m_baudChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1));
    for (const char* s : {"9600", "19200", "38400", "57600", "115200"})
        m_baudChoice->Append(s);
    m_baudChoice->SetSelection(4); // 115200 default
    row->Add(m_baudChoice, 0, wxALIGN_CENTER_VERTICAL);
    row->AddStretchSpacer();

    m_connectBtn = new wxButton(this, ID::CONNECT, "Connect");
    row->Add(m_connectBtn, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(8);

    parent->Add(row, 0, wxEXPAND);
}

// ---------------------------------------------------------------------------
// Log pane
// ---------------------------------------------------------------------------
void MainFrame::BuildLogPane(wxSizer* parent) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Communication Log");

    m_log = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                           wxDefaultPosition, wxSize(-1, 220),
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 |
                           wxHSCROLL | wxTE_DONTWRAP);
    wxFont mono(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
    m_log->SetFont(mono);

    box->Add(m_log, 1, wxEXPAND | wxALL, 4);
    parent->Add(box, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
}

// ---------------------------------------------------------------------------
// Command panel
// ---------------------------------------------------------------------------
void MainFrame::BuildCommandPanel(wxSizer* parent) {
    wxFont mono(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));

    m_cmdBoxSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Command Builder");

    // ---- Row 0: command selector ----------------------------------------
    auto* cmdRow = new wxBoxSizer(wxHORIZONTAL);

    cmdRow->Add(new wxStaticText(this, wxID_ANY, "Command:"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_cmdChoice = new wxChoice(this, ID::CMD_CHOICE);
    for (int i = 0; i < kCmdCount; ++i) {
        m_cmdChoice->Append(
            wxString::Format("(0x%02X)  %-22s  %s",
                             kCmds[i].code, kCmds[i].name, kCmds[i].desc));
    }
    cmdRow->Add(m_cmdChoice, 1, wxALIGN_CENTER_VERTICAL);
    cmdRow->AddSpacer(10);

    // Code badge  e.g. "Cmd: 0x09"
    m_cmdCodeLabel = new wxStaticText(this, wxID_ANY, "Cmd: 0x??",
                                       wxDefaultPosition, wxSize(80, -1));
    m_cmdCodeLabel->SetFont(mono);
    cmdRow->Add(m_cmdCodeLabel, 0, wxALIGN_CENTER_VERTICAL);

    m_cmdBoxSizer->Add(cmdRow, 0, wxEXPAND | wxALL, 6);

    // ---- Row 1: parameter 1 (conditionally visible) ---------------------
    m_p1RowSizer = new wxBoxSizer(wxHORIZONTAL);

    m_p1Label = new wxStaticText(this, wxID_ANY, "",
                                  wxDefaultPosition, wxSize(200, -1), wxALIGN_RIGHT);
    m_p1RowSizer->Add(m_p1Label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_p1Input = new wxTextCtrl(this, ID::CMD_PARAM1, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_p1Input->SetFont(mono);
    m_p1RowSizer->Add(m_p1Input, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    m_cmdBoxSizer->Add(m_p1RowSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // ---- Row 2: parameter 2 (conditionally visible) ---------------------
    m_p2RowSizer = new wxBoxSizer(wxHORIZONTAL);

    m_p2Label = new wxStaticText(this, wxID_ANY, "",
                                  wxDefaultPosition, wxSize(200, -1), wxALIGN_RIGHT);
    m_p2RowSizer->Add(m_p2Label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_p2Input = new wxTextCtrl(this, ID::CMD_PARAM2, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_p2Input->SetFont(mono);
    m_p2RowSizer->Add(m_p2Input, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    m_cmdBoxSizer->Add(m_p2RowSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // ---- Row 3: frame preview + Send button ------------------------------
    auto* bottomRow = new wxBoxSizer(wxHORIZONTAL);

    bottomRow->Add(new wxStaticText(this, wxID_ANY, "Frame:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_previewLabel = new wxStaticText(this, wxID_ANY, "\xe2\x80\x94",
                                       wxDefaultPosition, wxDefaultSize,
                                       wxST_ELLIPSIZE_END);
    m_previewLabel->SetFont(mono);
    bottomRow->Add(m_previewLabel, 1, wxALIGN_CENTER_VERTICAL);
    bottomRow->AddSpacer(12);

    m_sendCmdBtn = new wxButton(this, ID::SEND_COMMAND, "Send",
                                 wxDefaultPosition, wxSize(90, -1));
    bottomRow->Add(m_sendCmdBtn, 0, wxALIGN_CENTER_VERTICAL);

    m_cmdBoxSizer->Add(bottomRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    parent->Add(m_cmdBoxSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
}

// ---------------------------------------------------------------------------
// Connected / disconnected state
// ---------------------------------------------------------------------------
void MainFrame::UpdateConnectedState(bool connected) {
    m_connectBtn->SetLabel(connected ? "Disconnect" : "Connect");
    m_sendCmdBtn->Enable(connected);
    m_chatBtn->Enable(connected);

    if (connected) {
        SetStatusText(wxString::Format("Connected  |  %s",
                                       m_portChoice->GetStringSelection()), 0);
    } else {
        SetStatusText("Disconnected", 0);
        SetStatusText("TX: 0 bytes  |  RX: 0 bytes", 1);
    }
}

// ---------------------------------------------------------------------------
// Command panel — refresh labels, visibility and preview
// ---------------------------------------------------------------------------
void MainFrame::UpdateCommandUI() {
    int idx = m_cmdChoice->GetSelection();
    if (idx < 0 || idx >= kCmdCount) return;
    const CmdInfo& c = kCmds[idx];

    // Update the code badge
    m_cmdCodeLabel->SetLabel(wxString::Format("Cmd: 0x%02X", c.code));

    // Param 1 row
    bool showP1 = (c.p1Label != nullptr);
    if (showP1) {
        m_p1Label->SetLabel(c.p1Label);
        m_p1Input->SetHint(c.p1Hint ? wxString::FromUTF8(c.p1Hint) : "");
    } else {
        m_p1Input->ChangeValue("");  // clear without triggering EVT_TEXT
    }
    m_cmdBoxSizer->Show(m_p1RowSizer, showP1);

    // Param 2 row
    bool showP2 = (c.p2Label != nullptr);
    if (showP2) {
        m_p2Label->SetLabel(c.p2Label);
        m_p2Input->SetHint(c.p2Hint ? wxString::FromUTF8(c.p2Hint) : "");
    } else {
        m_p2Input->ChangeValue("");
    }
    m_cmdBoxSizer->Show(m_p2RowSizer, showP2);

    Layout();
    UpdateFramePreview();
}

// ---------------------------------------------------------------------------
// Parse raw hex input — shared helper
// ---------------------------------------------------------------------------
static bool ParseHexBytes(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.empty()) return true;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token) {
        if (token.size() > 2) return false;
        for (char ch : token)
            if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        out.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Build the payload for the currently selected command.
// Returns false (and sets err) if the inputs are invalid.
// The preview calls this too — it shows "—" on failure rather than an error.
// ---------------------------------------------------------------------------
bool MainFrame::BuildCommandPayload(std::vector<uint8_t>& payload, wxString& err) {
    int idx = m_cmdChoice->GetSelection();
    if (idx < 0 || idx >= kCmdCount) { err = "No command selected"; return false; }
    const CmdInfo& c = kCmds[idx];

    payload.clear();

    // Helper: parse input, fail with descriptive message
    auto getBytes = [&](wxTextCtrl* ctrl, const char* label,
                         std::vector<uint8_t>& out) -> bool {
        if (!ParseHexBytes(ctrl->GetValue().Trim().ToStdString(), out)) {
            err = wxString::Format("'%s' — invalid hex", label);
            return false;
        }
        return true;
    };

    // Helper: check exact byte count
    auto exact = [&](const std::vector<uint8_t>& v, size_t n,
                      const char* label) -> bool {
        if (v.size() != n) {
            err = wxString::Format("'%s' must be exactly %zu byte%s",
                                   label, n, n == 1 ? "" : "s");
            return false;
        }
        return true;
    };

    std::vector<uint8_t> p1, p2;

    switch (c.code) {
        // No payload
        case 0x05: case 0x0D: case 0x0E: case 0x0F: case 0x12:
            return true;

        // p2 = raw data bytes
        case 0x00: // DATA_REQ
            return getBytes(m_p2Input, c.p2Label, payload);

        // p1 = 2-byte addr (LSB first), p2 = data
        case 0x01: // DATAEX_REQ
        case 0x0B: // UNICAST_DATA_REQ
            if (!getBytes(m_p1Input, c.p1Label, p1)) return false;
            if (!exact(p1, 2, c.p1Label))             return false;
            if (!getBytes(m_p2Input, c.p2Label, p2))  return false;
            payload = p1;
            payload.insert(payload.end(), p2.begin(), p2.end());
            return true;

        // p1 = 1 byte
        case 0x04: // SET_MODE_REQ
        case 0x06: // SET_CHANNEL_REQ
        case 0x10: // SET_PAPOWER_REQ
            if (!getBytes(m_p1Input, c.p1Label, p1)) return false;
            if (!exact(p1, 1, c.p1Label))             return false;
            payload = p1;
            return true;

        // p1 = variable bytes (net ID)
        case 0x07: // SET_DESTNETID_REQ
            return getBytes(m_p1Input, c.p1Label, payload);

        // p1 = 2-byte addr
        case 0x08: // SET_DESTADDR_REQ
            if (!getBytes(m_p1Input, c.p1Label, p1)) return false;
            if (!exact(p1, 2, c.p1Label))             return false;
            payload = p1;
            return true;

        // p1 = 1-byte index, p2 = value bytes
        case 0x09: // SET_REQ
            if (!getBytes(m_p1Input, c.p1Label, p1)) return false;
            if (!exact(p1, 1, c.p1Label))             return false;
            if (!getBytes(m_p2Input, c.p2Label, p2))  return false;
            payload = p1;
            payload.insert(payload.end(), p2.begin(), p2.end());
            return true;

        // p1 = 1-byte index
        case 0x0A: // GET_REQ
            if (!getBytes(m_p1Input, c.p1Label, p1)) return false;
            if (!exact(p1, 1, c.p1Label))             return false;
            payload = p1;
            return true;

        default:
            return true;
    }
}

// ---------------------------------------------------------------------------
// Live frame preview
// ---------------------------------------------------------------------------
void MainFrame::UpdateFramePreview() {
    std::vector<uint8_t> payload;
    wxString err;
    if (BuildCommandPayload(payload, err)) {
        int idx = m_cmdChoice->GetSelection();
        if (idx < 0) return;
        auto frame = TarvosProtocol::BuildFrame(kCmds[idx].code, payload);
        m_previewLabel->SetLabel(wxString::FromUTF8(TarvosProtocol::HexDump(frame)));
    } else {
        m_previewLabel->SetLabel("\xe2\x80\x94");
    }
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
void MainFrame::Log(const wxString& msg, LogLevel level) {
    wxDateTime now = wxDateTime::UNow();
    wxString ts = now.Format("[%H:%M:%S.") +
                  wxString::Format("%03ld] ", now.GetMillisecond());

    wxString prefix;
    switch (level) {
        case LogLevel::TX:    prefix = wxString::FromUTF8("\xe2\x86\x92") + " TX: "; break;
        case LogLevel::RX:    prefix = wxString::FromUTF8("\xe2\x86\x90") + " RX: "; break;
        case LogLevel::Error: prefix = "!! ERR: "; break;
        default:              prefix = "   ";       break;
    }

    m_log->AppendText(ts + prefix + msg + "\n");
    m_log->ShowPosition(m_log->GetLastPosition());
}

// ---------------------------------------------------------------------------
// Serial helpers
// ---------------------------------------------------------------------------
void MainFrame::PopulatePorts() {
    auto ports = SerialPort::ListPorts();
    m_portChoice->Clear();
    if (ports.empty()) {
        m_portChoice->Append("(no devices found)");
    } else {
        for (const auto& p : ports)
            m_portChoice->Append(wxString::FromUTF8(p));
    }
    m_portChoice->SetSelection(0);
}

void MainFrame::SendFrame(const std::vector<uint8_t>& frame) {
    if (!m_serial.IsOpen()) return;
    if (m_serial.Write(frame)) {
        Log(wxString::FromUTF8(TarvosProtocol::HexDump(frame)), LogLevel::TX);
    } else {
        Log("Write failed", LogLevel::Error);
    }
    UpdateStatusBar();
}

void MainFrame::UpdateStatusBar() {
    SetStatusText(wxString::Format("TX: %zu bytes  |  RX: %zu bytes",
                                    m_serial.BytesTx(), m_serial.BytesRx()), 1);
}

// ---------------------------------------------------------------------------
// Event handlers — connection
// ---------------------------------------------------------------------------
void MainFrame::OnRefreshPorts(wxCommandEvent&) {
    PopulatePorts();
    Log("Port list refreshed");
}

void MainFrame::OnConnect(wxCommandEvent&) {
    if (m_serial.IsOpen()) {
        m_timer.Stop();
        m_serial.Close();
        m_proto.Reset();
        UpdateConnectedState(false);
        Log("Disconnected");
        return;
    }

    wxString portStr = m_portChoice->GetStringSelection();
    if (portStr == "(no devices found)") {
        Log("No valid port — click Refresh", LogLevel::Error);
        return;
    }

    int baudRates[] = {9600, 19200, 38400, 57600, 115200};
    int baud = baudRates[m_baudChoice->GetSelection()];

    if (!m_serial.Open(portStr.ToStdString(), baud)) {
        Log(wxString::Format("Failed to open %s at %d baud", portStr, baud),
            LogLevel::Error);
        return;
    }

    m_proto.Reset();
    UpdateConnectedState(true);
    Log(wxString::Format("Opened %s at %d baud", portStr, baud));
    m_timer.Start(50);
}

// ---------------------------------------------------------------------------
// Event handlers — command panel
// ---------------------------------------------------------------------------
void MainFrame::OnCommandSelected(wxCommandEvent&) {
    // Clear inputs when switching commands so stale values don't carry over
    m_p1Input->ChangeValue("");
    m_p2Input->ChangeValue("");
    UpdateCommandUI();
}

void MainFrame::OnParamChanged(wxCommandEvent&) {
    UpdateFramePreview();
}

void MainFrame::OnSendCommand(wxCommandEvent&) {
    if (!m_serial.IsOpen()) return;

    int idx = m_cmdChoice->GetSelection();
    if (idx < 0 || idx >= kCmdCount) {
        Log("No command selected", LogLevel::Error);
        return;
    }

    // Confirm destructive operations
    if (kCmds[idx].code == 0x12) { // FACTORY_RESET_REQ
        if (wxMessageBox(
                "Send FACTORY_RESET_REQ?\n"
                "All user settings will be restored to factory defaults.",
                "Confirm Factory Reset", wxYES_NO | wxICON_WARNING, this) != wxYES)
            return;
    }

    std::vector<uint8_t> payload;
    wxString err;
    if (!BuildCommandPayload(payload, err)) {
        Log("Cannot build frame: " + err, LogLevel::Error);
        return;
    }

    auto frame = TarvosProtocol::BuildFrame(kCmds[idx].code, payload);
    SendFrame(frame);

    // Also log a human-readable description
    TarvosProtocol::Frame f{ kCmds[idx].code, payload, true };
    Log(wxString::FromUTF8("         " + TarvosProtocol::DescribeFrame(f)));
}

// ---------------------------------------------------------------------------
// Incoming frames — route text messages to the chat window
// ---------------------------------------------------------------------------
void MainFrame::OnFrameParsed(const TarvosProtocol::Frame& f) {
    if (f.cmd != TarvosProtocol::CMD_DATAEX_IND) return;

    // Payload: [data bytes...] [RSSI byte] — strip trailing RSSI
    if (f.payload.size() < 2) return;

    std::string text(f.payload.begin(), f.payload.end() - 1);
    if (m_chat && m_chat->IsShown())
        m_chat->ReceiveText(text);
}

// ---------------------------------------------------------------------------
// Timer — poll serial RX
// ---------------------------------------------------------------------------
void MainFrame::OnTimer(wxTimerEvent&) {
    if (!m_serial.IsOpen()) { m_timer.Stop(); return; }

    auto raw = m_serial.Read();
    if (raw.empty()) return;

    auto frames = m_proto.Feed(raw);
    for (const auto& f : frames) {
        auto wireBytes = TarvosProtocol::BuildFrame(f.cmd, f.payload);
        Log(wxString::FromUTF8(TarvosProtocol::HexDump(wireBytes)), LogLevel::RX);
        Log(wxString::FromUTF8("         " + TarvosProtocol::DescribeFrame(f)));
        if (f.checksumOk) OnFrameParsed(f);
    }

    UpdateStatusBar();
}
