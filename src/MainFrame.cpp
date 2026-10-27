#include "MainFrame.h"
#include "TarvosProtocol.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/msgdlg.h>
#include <wx/datetime.h>
#include <wx/menu.h>
#include <wx/utils.h>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>

using TP = TarvosProtocol;

// ---------------------------------------------------------------------------
// Command table
// Every request command in the manual (table 12) and its UI parameter
// configuration. Labels for DATAEX_REQ and SET_DESTADDR_REQ depend on the
// address mode and are set in UpdateCommandUI.
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
//  p1Label                         p1Hint    p2Label                    p2Hint
  { 0x00, "DATA_REQ",            "Send payload to the configured destination",
    nullptr,                        nullptr,  "Data (hex bytes):",       "48 65 6C 6C 6F"  },

  { 0x01, "DATAEX_REQ",          "Send payload on a channel to an address",
    "Channel + address:",           "6A",     "Data (hex bytes):",       "48 65 6C 6C 6F"  },

  { 0x04, "SET_MODE_REQ",        "Switch to transparent mode",
    "OpMode (00 = transparent):",   "00",     nullptr,                   nullptr            },

  { 0x05, "RESET_REQ",           "Software reset  —  no payload",
    nullptr,                        nullptr,  nullptr,                   nullptr            },

  { 0x06, "SET_CHANNEL_REQ",     "Set RF channel (runtime)",
    "Channel (1 B, 00-8C):",        "6A",     nullptr,                   nullptr            },

  { 0x07, "SET_DESTNETID_REQ",   "Set destination network ID (runtime)",
    "Dest Net ID (1 B):",           "01",     nullptr,                   nullptr            },

  { 0x08, "SET_DESTADDR_REQ",    "Set destination address (runtime)",
    "Dest Addr:",                   "02",     nullptr,                   nullptr            },

  { 0x09, "SET_REQ",             "Write a user setting to flash",
    "Setting:",                     nullptr,  "Value (hex, LSB first):", nullptr            },

  { 0x0A, "GET_REQ",             "Read a user setting",
    "Setting:",                     nullptr,  nullptr,                   nullptr            },

  { 0x0D, "RSSI_REQ",            "Read RSSI of the last received packet  —  no payload",
    nullptr,                        nullptr,  nullptr,                   nullptr            },

  { 0x0E, "SHUTDOWN_REQ",        "Enter shutdown mode  —  no payload",
    nullptr,                        nullptr,  nullptr,                   nullptr            },

  { 0x0F, "STANDBY_REQ",         "Enter standby mode  —  no payload",
    nullptr,                        nullptr,  nullptr,                   nullptr            },

  { 0x11, "SET_PAPOWER_REQ",     "Set TX power (runtime)",
    "TX power (1 B, 00-0E dBm):",   "0E",     nullptr,                   nullptr            },

  { 0x12, "FACTORY_RESET_REQ",   "Restore defaults + reset  —  no payload",
    nullptr,                        nullptr,  nullptr,                   nullptr            },
};
static constexpr int kCmdCount = static_cast<int>(sizeof(kCmds) / sizeof(kCmds[0]));

// Manual 7.1: "500 ms should cover the worst-case time". Radio transmissions
// with retries and long-range packets get more headroom.
static constexpr long long kConfirmTimeoutMs     = 500;
static constexpr long long kDataConfirmTimeoutMs = 2000;
static constexpr long long kEscapeTimeoutMs      = 1000;
static constexpr long long kStaleFrameMs         = 200;

static const int kBaudRates[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };

static long long NowMs() { return wxGetLocalTimeMillis().GetValue(); }

// ---------------------------------------------------------------------------
// Event IDs
// ---------------------------------------------------------------------------
namespace ID {
    enum {
        NEW_WINDOW = wxID_HIGHEST + 1,
        READ_MODULE_INFO,
        ESCAPE_TRANSPARENT,
        REFRESH_PORTS,
        CONNECT,
        CMD_CHOICE,
        SETTING_CHOICE,
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
    EVT_MENU(ID::NEW_WINDOW,          MainFrame::OnNewWindow)
    EVT_MENU(ID::READ_MODULE_INFO,    MainFrame::OnReadModuleInfo)
    EVT_MENU(ID::ESCAPE_TRANSPARENT,  MainFrame::OnEscapeTransparent)
    EVT_BUTTON(ID::REFRESH_PORTS,     MainFrame::OnRefreshPorts)
    EVT_BUTTON(ID::CONNECT,           MainFrame::OnConnect)
    EVT_CHOICE(ID::CMD_CHOICE,        MainFrame::OnCommandSelected)
    EVT_CHOICE(ID::SETTING_CHOICE,    MainFrame::OnSettingSelected)
    EVT_TEXT(ID::CMD_PARAM1,          MainFrame::OnParamChanged)
    EVT_TEXT(ID::CMD_PARAM2,          MainFrame::OnParamChanged)
    EVT_BUTTON(ID::SEND_COMMAND,      MainFrame::OnSendCommand)
    EVT_BUTTON(ID::OPEN_CHAT,         MainFrame::OnOpenChat)
    EVT_TIMER(ID::POLL_TIMER,         MainFrame::OnTimer)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "TarvosTool",
              wxDefaultPosition, wxSize(900, 600))
    , m_timer(this, ID::POLL_TIMER)
{
    SetMinSize(wxSize(760, 520));
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
    CreateStatusBar(3);
    const int widths[] = { -2, -2, -3 };
    SetStatusWidths(3, widths);

    PopulatePorts();
    UpdateConnectedState(false);
    UpdateModuleInfo();

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

    m_moduleMenu = new wxMenu();
    m_moduleMenu->Append(ID::READ_MODULE_INFO, "Read Address Mode && RF Profile",
                         "Read MAC_DefaultAddressMode and RADIO_DefaultRfProfile");
    m_moduleMenu->Append(ID::ESCAPE_TRANSPARENT, "Return to Command Mode (+++)",
                         "Send the transparent-mode escape sequence");
    menuBar->Append(m_moduleMenu, "Module");

    SetMenuBar(menuBar);
}

void MainFrame::OnNewWindow(wxCommandEvent&) {
    (new MainFrame())->Show(true);
}

void MainFrame::OnOpenChat(wxCommandEvent&) {
    if (!m_chat) {
        m_chat = new ChatFrame(this, [this](uint8_t cmd, const std::vector<uint8_t>& payload) {
            return SubmitRequest(cmd, payload, Origin::Chat);
        });
        m_chat->SetModuleInfo(m_addrMode, TP::MaxPacketSize(m_rfProfile));
    }

    m_chat->Show(true);
    m_chat->Raise();
}

void MainFrame::OnReadModuleInfo(wxCommandEvent&) {
    QueryModuleInfo();
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
    for (int baud : kBaudRates)
        m_baudChoice->Append(wxString::Format("%d", baud));
    m_baudChoice->SetSelection(4); // 115200 factory default
    row->Add(m_baudChoice, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(14);

    m_busyCheck = new wxCheckBox(this, wxID_ANY, "BUSY flow control");
    m_busyCheck->SetToolTip("Hold back each byte while the module signals BUSY on the "
                            "adapter's CTS line. Only enable it if your board routes "
                            "BUSY to CTS, otherwise every write will fail.");
    row->Add(m_busyCheck, 0, wxALIGN_CENTER_VERTICAL);
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
                                  wxDefaultPosition, wxSize(240, -1), wxALIGN_RIGHT);
    m_p1RowSizer->Add(m_p1Label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_p1Input = new wxTextCtrl(this, ID::CMD_PARAM1, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_p1Input->SetFont(mono);
    m_p1RowSizer->Add(m_p1Input, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    // Setting selector for SET_REQ / GET_REQ (shown instead of m_p1Input)
    m_settingChoice = new wxChoice(this, ID::SETTING_CHOICE);
    for (const auto& s : TP::Settings())
        m_settingChoice->Append(wxString::Format("%2d  %s%s", s.index, s.name,
                                                 s.writable ? "" : "  (read-only)"));
    m_settingChoice->SetSelection(0);
    m_p1RowSizer->Add(m_settingChoice, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    m_cmdBoxSizer->Add(m_p1RowSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // ---- Row 2: parameter 2 (conditionally visible) ---------------------
    m_p2RowSizer = new wxBoxSizer(wxHORIZONTAL);

    m_p2Label = new wxStaticText(this, wxID_ANY, "",
                                  wxDefaultPosition, wxSize(240, -1), wxALIGN_RIGHT);
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
    m_baudChoice->Enable(!connected);
    m_busyCheck->Enable(!connected);

    if (connected) {
        SetStatusText(wxString::Format("Connected  |  %s",
                                       m_portChoice->GetStringSelection()), 0);
    } else {
        SetStatusText("Disconnected", 0);
        SetStatusText("TX: 0 bytes  |  RX: 0 bytes", 1);
    }
    UpdateModeUI();
}

void MainFrame::UpdateModeUI() {
    const bool connected = m_serial.IsOpen();
    const bool commands  = connected && !m_transparent;

    m_sendCmdBtn->Enable(commands);
    m_chatBtn->Enable(commands);
    m_moduleMenu->Enable(ID::READ_MODULE_INFO, commands);
    m_moduleMenu->Enable(ID::ESCAPE_TRANSPARENT, connected && m_transparent);
}

// ---------------------------------------------------------------------------
// Module information (address mode, RF profile)
// ---------------------------------------------------------------------------
void MainFrame::QueryModuleInfo() {
    SubmitRequest(TP::CMD_GET_REQ, { 4 }, Origin::Auto); // MAC_DefaultAddressMode
    SubmitRequest(TP::CMD_GET_REQ, { 1 }, Origin::Auto); // RADIO_DefaultRfProfile
}

void MainFrame::UpdateModuleInfo() {
    wxString mode    = m_addrMode  >= 0 ? wxString::Format("%d", m_addrMode)  : wxString("?");
    wxString profile = m_rfProfile >= 0 ? wxString::Format("%d", m_rfProfile) : wxString("?");
    wxString text = "Address mode " + mode + "  |  RF profile " + profile;
    if (m_transparent)    text += "  |  TRANSPARENT MODE";
    if (m_restartPending) text += "  |  reset pending";
    SetStatusText(text, 2);

    if (m_chat) m_chat->SetModuleInfo(m_addrMode, TP::MaxPacketSize(m_rfProfile));
    UpdateCommandUI();
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

    const bool settingCmd = (c.code == TP::CMD_SET_REQ || c.code == TP::CMD_GET_REQ);

    // Labels that depend on the address mode
    wxString p1Label = c.p1Label ? wxString::FromUTF8(c.p1Label) : wxString();
    wxString p1Hint  = c.p1Hint  ? wxString::FromUTF8(c.p1Hint)  : wxString();
    if (c.code == TP::CMD_DATAEX_REQ) {
        switch (m_addrMode) {
            case 0:  p1Label = "Channel (1 B):";                     p1Hint = "6A"; break;
            case 1:  p1Label = "Channel, Dest Addr (2 B):";          p1Hint = "6A 02"; break;
            case 2:  p1Label = "Channel, Net ID, Dest Addr (3 B):";  p1Hint = "6A 01 02"; break;
            case 3:  p1Label = "Channel, Net ID, Addr LSB MSB (4 B):"; p1Hint = "6A 01 02 00"; break;
            default: p1Label = "Channel + address (mode unknown):";  break;
        }
    } else if (c.code == TP::CMD_SET_DESTADDR_REQ) {
        switch (m_addrMode) {
            case 0:  p1Label = "Dest Addr (unused in mode 0):";      break;
            case 1:
            case 2:  p1Label = "Dest Addr (1 B):";                   p1Hint = "02"; break;
            case 3:  p1Label = "Dest Addr (2 B, LSB first):";        p1Hint = "02 00"; break;
            default: p1Label = "Dest Addr (1-2 B, LSB first):";      p1Hint = "02"; break;
        }
    }

    // Param 1 row
    bool showP1 = (c.p1Label != nullptr);
    if (showP1) {
        m_p1Label->SetLabel(p1Label);
        m_p1Input->SetHint(p1Hint);
    } else {
        m_p1Input->ChangeValue("");  // clear without triggering EVT_TEXT
    }
    m_cmdBoxSizer->Show(m_p1RowSizer, showP1);
    m_p1RowSizer->Show(m_p1Input, showP1 && !settingCmd);
    m_p1RowSizer->Show(m_settingChoice, settingCmd);

    // Param 2 row
    bool showP2 = (c.p2Label != nullptr);
    if (showP2) {
        m_p2Label->SetLabel(c.p2Label);
        wxString hint = c.p2Hint ? wxString::FromUTF8(c.p2Hint) : wxString();
        if (c.code == TP::CMD_SET_REQ) {
            if (const TP::Setting* s = TP::FindSetting(static_cast<uint8_t>(SelectedSettingIndex()))) {
                hint = wxString::Format("%d", s->minLen);
                if (s->maxLen != s->minLen) hint += wxString::Format("-%d", s->maxLen);
                hint += wxString::Format(" byte(s): %s", s->range);
            }
        }
        m_p2Input->SetHint(hint);
    } else {
        m_p2Input->ChangeValue("");
    }
    m_cmdBoxSizer->Show(m_p2RowSizer, showP2);

    Layout();
    UpdateFramePreview();
}

int MainFrame::SelectedSettingIndex() const {
    int sel = m_settingChoice->GetSelection();
    const auto& settings = TP::Settings();
    if (sel < 0 || sel >= static_cast<int>(settings.size())) return -1;
    return settings[sel].index;
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
// The preview calls this too — it shows the error instead of a frame.
// ---------------------------------------------------------------------------
bool MainFrame::BuildCommandPayload(std::vector<uint8_t>& payload, wxString& err) {
    int idx = m_cmdChoice->GetSelection();
    if (idx < 0 || idx >= kCmdCount) { err = "No command selected"; return false; }
    const CmdInfo& c = kCmds[idx];

    payload.clear();

    // Helper: parse input, fail with descriptive message
    auto getBytes = [&](wxTextCtrl* ctrl, const wxString& label,
                         std::vector<uint8_t>& out) -> bool {
        if (!ParseHexBytes(ctrl->GetValue().Trim().ToStdString(), out)) {
            err = wxString::Format("'%s' — invalid hex", label);
            return false;
        }
        return true;
    };

    // Helper: check exact byte count
    auto exact = [&](const std::vector<uint8_t>& v, size_t n,
                      const wxString& label) -> bool {
        if (v.size() != n) {
            err = wxString::Format("'%s' must be exactly %zu byte%s",
                                   label, n, n == 1 ? "" : "s");
            return false;
        }
        return true;
    };

    // Helper: radio payload must fit the RF profile's packet size
    auto fitsPacket = [&](size_t dataLen) -> bool {
        const size_t max = TP::MaxPacketSize(m_rfProfile);
        if (max && dataLen > max) {
            err = wxString::Format("Data is %zu bytes, RF profile %d allows %zu", dataLen, m_rfProfile, max);
            return false;
        }
        return true;
    };

    const wxString p1Label = m_p1Label->GetLabel();
    std::vector<uint8_t> p1, p2;

    switch (c.code) {
        // No payload
        case TP::CMD_RESET_REQ: case TP::CMD_RSSI_REQ: case TP::CMD_SHUTDOWN_REQ:
        case TP::CMD_STANDBY_REQ: case TP::CMD_FACTORY_RESET_REQ:
            return true;

        case TP::CMD_DATA_REQ:
            if (!getBytes(m_p2Input, c.p2Label, payload)) return false;
            return fitsPacket(payload.size());

        // Channel + address bytes for the address mode, then data (manual 7.2.2)
        case TP::CMD_DATAEX_REQ:
            if (m_addrMode < 0) {
                err = "Address mode unknown — use Module > Read Address Mode & RF Profile";
                return false;
            }
            if (!getBytes(m_p1Input, p1Label, p1)) return false;
            if (!exact(p1, 1 + TP::AddressBytes(m_addrMode), p1Label)) return false;
            if (p1[0] > 140) { err = "Channel must be 00-8C (0-140)"; return false; }
            if (!getBytes(m_p2Input, c.p2Label, p2)) return false;
            if (!fitsPacket(p2.size())) return false;
            payload = p1;
            payload.insert(payload.end(), p2.begin(), p2.end());
            return true;

        // Manual 7.4.5: the only valid parameter is 0x00 (transparent mode)
        case TP::CMD_SET_MODE_REQ:
            if (!getBytes(m_p1Input, p1Label, p1)) return false;
            if (!exact(p1, 1, p1Label))            return false;
            if (p1[0] != TP::OPMODE_TRANSPARENT) {
                err = "Only 00 (transparent) — return with Module > Return to Command Mode";
                return false;
            }
            payload = p1;
            return true;

        case TP::CMD_SET_CHANNEL_REQ:
            if (!getBytes(m_p1Input, p1Label, p1)) return false;
            if (!exact(p1, 1, p1Label))            return false;
            if (p1[0] > 140) { err = "Channel must be 00-8C (0-140)"; return false; }
            payload = p1;
            return true;

        case TP::CMD_SET_PAPOWER_REQ:
            if (!getBytes(m_p1Input, p1Label, p1)) return false;
            if (!exact(p1, 1, p1Label))            return false;
            if (p1[0] > 14) { err = "TX power must be 00-0E (0-14 dBm)"; return false; }
            payload = p1;
            return true;

        // Manual 7.4.3: only used in address modes 2 and 3
        case TP::CMD_SET_DESTNETID_REQ:
            if (m_addrMode == 0 || m_addrMode == 1) {
                err = wxString::Format("No net ID in address mode %d", m_addrMode);
                return false;
            }
            if (!getBytes(m_p1Input, p1Label, p1)) return false;
            if (!exact(p1, 1, p1Label))            return false;
            payload = p1;
            return true;

        // Manual 7.4.4: 1 byte in modes 1/2, 2 bytes (LSB first) in mode 3
        case TP::CMD_SET_DESTADDR_REQ:
            if (m_addrMode == 0) { err = "No destination address in address mode 0"; return false; }
            if (!getBytes(m_p1Input, p1Label, p1)) return false;
            if (m_addrMode == 1 || m_addrMode == 2) {
                if (!exact(p1, 1, p1Label)) return false;
            } else if (m_addrMode == 3) {
                if (!exact(p1, 2, p1Label)) return false;
            } else if (p1.empty() || p1.size() > 2) {
                err = "Dest Addr must be 1 or 2 bytes";
                return false;
            }
            payload = p1;
            return true;

        case TP::CMD_SET_REQ: {
            const int index = SelectedSettingIndex();
            if (index < 0) { err = "No setting selected"; return false; }
            if (!getBytes(m_p2Input, c.p2Label, p2)) return false;
            std::string why;
            if (!TP::ValidateSetting(static_cast<uint8_t>(index), p2, why)) {
                err = wxString::FromUTF8(why);
                return false;
            }
            payload.push_back(static_cast<uint8_t>(index));
            payload.insert(payload.end(), p2.begin(), p2.end());
            return true;
        }

        case TP::CMD_GET_REQ: {
            const int index = SelectedSettingIndex();
            if (index < 0) { err = "No setting selected"; return false; }
            payload.push_back(static_cast<uint8_t>(index));
            return true;
        }

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
        auto frame = TP::BuildFrame(kCmds[idx].code, payload);
        m_previewLabel->SetLabel(wxString::FromUTF8(TP::HexDump(frame)));
    } else {
        m_previewLabel->SetLabel(wxString::FromUTF8("\xe2\x80\x94  ") + err);
    }
}

// ---------------------------------------------------------------------------
// Confirm commands with lasting or disruptive effects
// ---------------------------------------------------------------------------
bool MainFrame::ConfirmCommand(uint8_t cmd, const std::vector<uint8_t>& payload) {
    wxString title, text;

    switch (cmd) {
        case TP::CMD_FACTORY_RESET_REQ:
            title = "Confirm Factory Reset";
            text  = "Send FACTORY_RESET_REQ?\n"
                    "All user settings will be restored to factory defaults.";
            break;

        case TP::CMD_SET_REQ: {
            const TP::Setting* s = TP::FindSetting(payload[0]);
            std::vector<uint8_t> value(payload.begin() + 1, payload.end());
            title = "Write User Setting";
            text  = wxString::Format(
                "Write %s = %s to the module's flash?\n\n"
                "- Each write uses one of about 100,000 flash cycles. Read the "
                "setting with GET_REQ first and only write if it differs.\n"
                "- The module does not check the value. A wrong value can make it "
                "unreachable.\n"
                "- Keep the supply stable: a reset during the write can corrupt the "
                "settings.\n"
                "- The new value takes effect after a reset. Until then only SET_REQ, "
                "GET_REQ and RESET_REQ are allowed.",
                s ? s->name : "?", wxString::FromUTF8(TP::DescribeSettingValue(payload[0], value)));
            if (payload[0] == 0)
                text += "\n\nThe module restarts at the new baud rate: reconnect at "
                        "that rate afterwards.";
            break;
        }

        case TP::CMD_SET_MODE_REQ:
            title = "Switch to Transparent Mode";
            text  = "Switch the module to transparent mode?\n\n"
                    "It will stop accepting command frames. Everything you send is "
                    "transmitted over the radio as raw data.\n"
                    "Use Module > Return to Command Mode (+++) to come back.";
            break;

        case TP::CMD_SHUTDOWN_REQ:
        case TP::CMD_STANDBY_REQ:
            title = cmd == TP::CMD_SHUTDOWN_REQ ? "Shut Down Module" : "Put Module in Standby";
            text  = "The module's UART switches off. It only wakes up on a falling "
                    "edge on its WAKE_UP pin (the WAKE UP button on the EV-kit).";
            if (cmd == TP::CMD_SHUTDOWN_REQ)
                text += "\nWaking from shutdown restarts the module, so runtime settings are lost.";
            break;

        default:
            return true;
    }

    return wxMessageBox(text, title, wxYES_NO | wxICON_WARNING, this) == wxYES;
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
// Request queue
// ---------------------------------------------------------------------------
wxString MainFrame::SubmitRequest(uint8_t cmd, const std::vector<uint8_t>& payload, Origin origin) {
    wxString err;
    if (!m_serial.IsOpen()) {
        err = "Not connected";
    } else if (m_transparent) {
        err = "Module is in transparent mode — use Module > Return to Command Mode first";
    } else if (m_restartPending && cmd != TP::CMD_SET_REQ && cmd != TP::CMD_GET_REQ &&
               cmd != TP::CMD_RESET_REQ) {
        err = "A setting was written: only SET_REQ, GET_REQ or RESET_REQ are allowed "
              "until the module restarts (send RESET_REQ)";
    }

    if (!err.IsEmpty()) {
        Log(wxString::FromUTF8(TP::DescribeFrame({ cmd, payload, true }, m_addrMode)) +
            " not sent: " + err, LogLevel::Error);
        return err;
    }

    m_queue.push_back({ cmd, payload, origin });
    PumpQueue();
    return {};
}

void MainFrame::PumpQueue() {
    while (!m_inflight && !m_queue.empty()) {
        Request req = std::move(m_queue.front());
        m_queue.pop_front();

        if (!SendFrame(TP::BuildFrame(req.cmd, req.payload))) {
            if (req.origin == Origin::Chat && m_chat) m_chat->OnTimeout(req.cmd);
            continue;
        }
        Log(wxString::FromUTF8("         " + TP::DescribeFrame({ req.cmd, req.payload, true },
                                                               m_addrMode)));
        m_inflight      = std::move(req);
        m_inflightSince = NowMs();
    }
}

void MainFrame::CheckTimeouts(long long nowMs) {
    if (m_escapeSince && nowMs - m_escapeSince > kEscapeTimeoutMs) {
        m_escapeSince = 0;
        Log("No SET_MODE_CNF after \"+++\" — module is probably still in transparent mode",
            LogLevel::Error);
    }

    if (!m_inflight) return;
    const bool data = m_inflight->cmd == TP::CMD_DATA_REQ || m_inflight->cmd == TP::CMD_DATAEX_REQ;
    const long long limit = data ? kDataConfirmTimeoutMs : kConfirmTimeoutMs;
    if (nowMs - m_inflightSince <= limit) return;

    Request req = std::move(*m_inflight);
    m_inflight.reset();
    Log(wxString::Format("No confirmation for %s within %lld ms",
                         wxString::FromUTF8(TP::DescribeFrame({ req.cmd, req.payload, true },
                                                              m_addrMode)), limit),
        LogLevel::Error);
    if (req.origin == Origin::Chat && m_chat) m_chat->OnTimeout(req.cmd);
    PumpQueue();
}

void MainFrame::ClearRequests() {
    m_queue.clear();
    m_inflight.reset();
    m_escapeSince = 0;
}

// ---------------------------------------------------------------------------
// Incoming frames
// ---------------------------------------------------------------------------
void MainFrame::HandleFrame(const TP::Frame& f) {
    if (m_inflight && f.cmd == TP::ConfirmationFor(m_inflight->cmd)) {
        Request req = std::move(*m_inflight);
        m_inflight.reset();
        HandleConfirmation(req, f);
        PumpQueue();
        return;
    }

    switch (f.cmd) {
        case TP::CMD_SET_MODE_CNF:
            // Answer to the "+++" escape sequence, which is not a queued request
            HandleModeConfirmation(f);
            break;

        case TP::CMD_RESET_IND:
            // Restart: runtime settings are back to the stored user settings
            m_restartPending = false;
            if (!f.payload.empty()) m_transparent = (f.payload[0] == TP::OPMODE_TRANSPARENT);
            UpdateModeUI();
            if (m_chat) m_chat->OnModuleRestarted();
            if (!m_transparent) QueryModuleInfo();
            UpdateModuleInfo();
            break;

        case TP::CMD_DATAEX_IND: {
            TP::RxData rx;
            if (m_chat && m_chat->IsShown() && TP::ParseDataExInd(f, std::max(m_addrMode, 0), rx))
                m_chat->ReceiveMessage(rx);
            break;
        }

        default:
            break;
    }
}

void MainFrame::HandleConfirmation(const Request& req, const TP::Frame& cnf) {
    const auto& p = cnf.payload;
    const bool ok = !p.empty() && p[0] == 0x00;

    switch (cnf.cmd) {
        case TP::CMD_GET_CNF:
            if (ok && p.size() > 1 && !req.payload.empty()) {
                const uint8_t index = req.payload[0];
                std::vector<uint8_t> value(p.begin() + 1, p.end());
                const TP::Setting* s = TP::FindSetting(index);
                Log(wxString::Format("         %s = %s", s ? s->name : "?",
                                     wxString::FromUTF8(TP::DescribeSettingValue(index, value))));
                if (index == 4) m_addrMode  = value[0];
                if (index == 1) m_rfProfile = value[0];
                if (index == 4 || index == 1) UpdateModuleInfo();
            }
            break;

        case TP::CMD_SET_CNF:
            if (ok) {
                m_restartPending = true;
                Log("         Stored. It takes effect after a reset; until then only "
                    "SET_REQ, GET_REQ and RESET_REQ are allowed.");
                if (req.payload.size() > 1 && req.payload[0] == 0)
                    Log("         The module restarts at the new baud rate: disconnect and "
                        "reconnect at that rate.");
                UpdateModuleInfo();
            }
            break;

        case TP::CMD_SET_MODE_CNF:
            HandleModeConfirmation(cnf);
            break;

        case TP::CMD_SHUTDOWN_CNF:
        case TP::CMD_STANDBY_CNF:
            if (ok)
                Log("         The module's UART is off until a falling edge on WAKE_UP "
                    "(EV-kit: WAKE UP button).");
            break;

        default:
            break;
    }

    if (req.origin == Origin::Chat && m_chat) m_chat->OnConfirmation(req.cmd, cnf);
}

void MainFrame::HandleModeConfirmation(const TP::Frame& cnf) {
    m_escapeSince = 0;
    if (cnf.payload.size() < 2 || cnf.payload[0] != 0x00) return;

    m_transparent = (cnf.payload[1] == TP::OPMODE_TRANSPARENT);
    m_proto.Reset();
    if (m_transparent)
        Log("         Transparent mode: received data is shown raw. Use Module > Return "
            "to Command Mode to send commands again.");
    UpdateModeUI();
    UpdateModuleInfo();
}

void MainFrame::HandleTransparentRx(const std::vector<uint8_t>& raw) {
    std::string ascii;
    for (uint8_t b : raw) ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
    Log(wxString::FromUTF8(TP::HexDump(raw) + "   \"" + ascii + "\""), LogLevel::RX);
}

// Manual 5.1.2: wait at least 4 byte durations, send "+++" as the only data,
// then wait for SET_MODE_CNF.
void MainFrame::OnEscapeTransparent(wxCommandEvent&) {
    if (!m_serial.IsOpen()) return;

    const long long guardMs = 40 * 1000 / m_baud + 1; // 4 bytes × 10 bits
    const long long idleMs  = NowMs() - m_lastTxAt;
    if (idleMs < guardMs) wxMilliSleep(static_cast<unsigned long>(guardMs - idleMs));

    m_proto.Reset();
    if (!SendFrame(TP::EscapeSequence())) return;
    Log("         Escape sequence \"+++\" — waiting for SET_MODE_CNF");
    m_escapeSince = NowMs();
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

bool MainFrame::SendFrame(const std::vector<uint8_t>& frame) {
    if (!m_serial.IsOpen()) return false;
    const bool ok = m_serial.Write(frame);
    if (ok) {
        Log(wxString::FromUTF8(TP::HexDump(frame)), LogLevel::TX);
        m_lastTxAt = NowMs();
    } else {
        Log(m_busyCheck->GetValue()
                ? "Write failed — module stayed BUSY (or BUSY is not wired to CTS)"
                : "Write failed",
            LogLevel::Error);
    }
    UpdateStatusBar();
    return ok;
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
        ClearRequests();
        m_transparent    = false;
        m_restartPending = false;
        m_addrMode       = -1;
        m_rfProfile      = -1;
        UpdateConnectedState(false);
        UpdateModuleInfo();
        Log("Disconnected");
        return;
    }

    wxString portStr = m_portChoice->GetStringSelection();
    if (portStr == "(no devices found)") {
        Log("No valid port — click Refresh", LogLevel::Error);
        return;
    }

    m_baud = kBaudRates[m_baudChoice->GetSelection()];
    const bool busy = m_busyCheck->GetValue();

    if (!m_serial.Open(portStr.ToStdString(), m_baud, busy)) {
        Log(wxString::Format("Failed to open %s at %d baud", portStr, m_baud),
            LogLevel::Error);
        return;
    }

    m_proto.Reset();
    UpdateConnectedState(true);
    Log(wxString::Format("Opened %s at %d baud%s", portStr, m_baud,
                         busy ? " with BUSY flow control" : ""));
    m_lastRxAt = NowMs();
    m_timer.Start(20);
    QueryModuleInfo();
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

void MainFrame::OnSettingSelected(wxCommandEvent&) {
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

    std::vector<uint8_t> payload;
    wxString err;
    if (!BuildCommandPayload(payload, err)) {
        Log("Cannot build frame: " + err, LogLevel::Error);
        return;
    }

    if (!ConfirmCommand(kCmds[idx].code, payload)) return;
    SubmitRequest(kCmds[idx].code, payload, Origin::Builder);
}

// ---------------------------------------------------------------------------
// Timer — poll serial RX, confirmation timeouts
// ---------------------------------------------------------------------------
void MainFrame::OnTimer(wxTimerEvent&) {
    if (!m_serial.IsOpen()) { m_timer.Stop(); return; }

    const long long now = NowMs();
    auto raw = m_serial.Read();

    if (!raw.empty()) {
        m_lastRxAt = now;
        if (m_transparent && !m_escapeSince) {
            HandleTransparentRx(raw);
        } else {
            for (const auto& f : m_proto.Feed(raw)) {
                auto wireBytes = TP::BuildFrame(f.cmd, f.payload);
                Log(wxString::FromUTF8(TP::HexDump(wireBytes)), LogLevel::RX);
                Log(wxString::FromUTF8("         " + TP::DescribeFrame(f, m_addrMode)));
                if (f.checksumOk) HandleFrame(f);
            }
        }
        UpdateStatusBar();
    } else if (m_proto.HasPartialFrame() && now - m_lastRxAt > kStaleFrameMs) {
        // The module pauses at most 4 byte durations inside a frame (manual 6.1)
        Log("Discarded an incomplete frame", LogLevel::Error);
        m_proto.Reset();
    }

    CheckTimeouts(now);
}
