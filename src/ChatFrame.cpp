#include "ChatFrame.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/datetime.h>

namespace ID {
    enum {
        SEND_BTN = wxID_HIGHEST + 100,
        INPUT_BOX,
        SET_ADDR_BTN,
        ADDR_INPUT,
        SET_CHANNEL_BTN,
        CHANNEL_INPUT,
    };
}

wxBEGIN_EVENT_TABLE(ChatFrame, wxFrame)
    EVT_BUTTON(ID::SEND_BTN,          ChatFrame::OnSendBtn)
    EVT_TEXT_ENTER(ID::INPUT_BOX,     ChatFrame::OnInputEnter)
    EVT_BUTTON(ID::SET_ADDR_BTN,      ChatFrame::OnSetAddr)
    EVT_TEXT_ENTER(ID::ADDR_INPUT,    ChatFrame::OnSetAddr)
    EVT_BUTTON(ID::SET_CHANNEL_BTN,   ChatFrame::OnSetChannel)
    EVT_TEXT_ENTER(ID::CHANNEL_INPUT, ChatFrame::OnSetChannel)
    EVT_CLOSE(ChatFrame::OnClose)
wxEND_EVENT_TABLE()

ChatFrame::ChatFrame(wxWindow* parent, SerialPort& serial)
    : wxFrame(parent, wxID_ANY, "Chat",
              wxDefaultPosition, wxSize(580, 520),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT)
    , m_serial(serial)
{
    SetMinSize(wxSize(500, 420));
    BuildUI();
    Centre();
}

void ChatFrame::BuildUI() {
    wxFont mono(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE));
    auto* root = new wxBoxSizer(wxVERTICAL);

    // ── Settings bar (dest addr + channel) ────────────────────────────────────
    auto* settingsBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Configuration");

    settingsBox->AddSpacer(4);
    settingsBox->Add(new wxStaticText(this, wxID_ANY, "Dest Addr:"),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    m_addrInput = new wxTextCtrl(this, ID::ADDR_INPUT, "0000",
                                 wxDefaultPosition, wxSize(70, -1),
                                 wxTE_PROCESS_ENTER);
    m_addrInput->SetFont(mono);
    m_addrInput->SetHint("2-byte hex");
    settingsBox->Add(m_addrInput, 0, wxALIGN_CENTER_VERTICAL);

    m_setAddrBtn = new wxButton(this, ID::SET_ADDR_BTN, "Set",
                                wxDefaultPosition, wxSize(50, -1));
    settingsBox->Add(m_setAddrBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    settingsBox->AddSpacer(16);

    settingsBox->Add(new wxStaticText(this, wxID_ANY, "Channel:"),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    m_channelInput = new wxTextCtrl(this, ID::CHANNEL_INPUT, "00",
                                    wxDefaultPosition, wxSize(50, -1),
                                    wxTE_PROCESS_ENTER);
    m_channelInput->SetFont(mono);
    m_channelInput->SetHint("hex");
    settingsBox->Add(m_channelInput, 0, wxALIGN_CENTER_VERTICAL);

    m_setChannelBtn = new wxButton(this, ID::SET_CHANNEL_BTN, "Set",
                                   wxDefaultPosition, wxSize(50, -1));
    settingsBox->Add(m_setChannelBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    settingsBox->AddStretchSpacer();
    settingsBox->AddSpacer(4);

    root->Add(settingsBox, 0, wxEXPAND | wxALL, 8);

    // ── Chat log ──────────────────────────────────────────────────────────────
    auto* logBox = new wxStaticBoxSizer(wxVERTICAL, this, "Messages");

    m_chatLog = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 |
                               wxTE_AUTO_URL | wxHSCROLL | wxTE_DONTWRAP);
    m_chatLog->SetFont(mono);
    m_chatLog->SetBackgroundColour(wxColour(30, 30, 30));
    logBox->Add(m_chatLog, 1, wxEXPAND | wxALL, 4);
    root->Add(logBox, 1, wxEXPAND | wxALL, 8);

    // ── Input row ─────────────────────────────────────────────────────────────
    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);

    m_inputBox = new wxTextCtrl(this, ID::INPUT_BOX, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize,
                                wxTE_PROCESS_ENTER);
    m_inputBox->SetFont(mono);
    m_inputBox->SetHint("Type a message and press Enter or Send...");
    inputRow->Add(m_inputBox, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_sendBtn = new wxButton(this, ID::SEND_BTN, "Send",
                             wxDefaultPosition, wxSize(80, -1));
    inputRow->Add(m_sendBtn, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(inputRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(root);
    m_inputBox->SetFocus();
}

// ---------------------------------------------------------------------------
// Send text message
// ---------------------------------------------------------------------------

void ChatFrame::Send() {
    wxString text = m_inputBox->GetValue().Trim(true).Trim(false);
    if (text.IsEmpty()) return;

    std::string utf8 = text.ToUTF8().data();
    if (utf8.size() > 255) {
        wxMessageBox("Message too long (max 255 bytes).", "Cannot Send",
                     wxOK | wxICON_WARNING, this);
        return;
    }

    std::vector<uint8_t> payload(utf8.begin(), utf8.end());
    SendCommand(TarvosProtocol::CMD_DATA_REQ, payload);
    AppendLine("You", text, wxColour(100, 180, 255));
    m_inputBox->Clear();
}

// ---------------------------------------------------------------------------
// Generic send command helper
// ---------------------------------------------------------------------------

void ChatFrame::SendCommand(uint8_t cmd, const std::vector<uint8_t>& payload) {
    auto frame = TarvosProtocol::BuildFrame(cmd, payload);
    if (!m_serial.Write(frame)) {
        AppendLine("ERR", "Send failed — is the device still connected?",
                   wxColour(255, 80, 80));
    }
}

// ---------------------------------------------------------------------------
// Set destination address (2 bytes, LSB first)
// ---------------------------------------------------------------------------

void ChatFrame::OnSetAddr(wxCommandEvent&) {
    wxString addrStr = m_addrInput->GetValue().Trim();
    if (addrStr.IsEmpty() || addrStr.Len() > 4) {
        AppendLine("ERR", "Address must be 1-4 hex chars (2 bytes, LSB first)",
                   wxColour(255, 80, 80));
        return;
    }

    try {
        uint16_t addr = std::stoul(addrStr.ToStdString(), nullptr, 16);
        uint8_t lsb = static_cast<uint8_t>(addr & 0xFF);
        uint8_t msb = static_cast<uint8_t>((addr >> 8) & 0xFF);

        SendCommand(TarvosProtocol::CMD_SET_DESTADDR_REQ, {lsb, msb});
        AppendLine("INFO", wxString::Format("Set dest address to 0x%04X", addr),
                   wxColour(200, 200, 100));
    } catch (...) {
        AppendLine("ERR", "Invalid hex value", wxColour(255, 80, 80));
    }
}

// ---------------------------------------------------------------------------
// Set channel
// ---------------------------------------------------------------------------

void ChatFrame::OnSetChannel(wxCommandEvent&) {
    wxString chanStr = m_channelInput->GetValue().Trim();
    if (chanStr.IsEmpty() || chanStr.Len() > 2) {
        AppendLine("ERR", "Channel must be 1-2 hex chars",
                   wxColour(255, 80, 80));
        return;
    }

    try {
        uint8_t chan = static_cast<uint8_t>(std::stoul(chanStr.ToStdString(), nullptr, 16));
        SendCommand(TarvosProtocol::CMD_SET_CHANNEL_REQ, {chan});
        AppendLine("INFO", wxString::Format("Set channel to 0x%02X", chan),
                   wxColour(200, 200, 100));
    } catch (...) {
        AppendLine("ERR", "Invalid hex value", wxColour(255, 80, 80));
    }
}

// ---------------------------------------------------------------------------
// Receive (called by MainFrame on DATAEX_IND)
// ---------------------------------------------------------------------------

void ChatFrame::ReceiveText(const std::string& text) {
    AppendLine("Remote", wxString::FromUTF8(text), wxColour(120, 220, 120));
}

// ---------------------------------------------------------------------------
// Append a timestamped line to the chat log
// ---------------------------------------------------------------------------

void ChatFrame::AppendLine(const wxString& who, const wxString& text,
                            const wxColour& colour) {
    wxDateTime now = wxDateTime::UNow();
    wxString ts = now.Format("[%H:%M:%S] ");

    wxTextAttr tsAttr;
    tsAttr.SetTextColour(wxColour(130, 130, 130));
    m_chatLog->SetDefaultStyle(tsAttr);
    m_chatLog->AppendText(ts);

    wxTextAttr whoAttr;
    whoAttr.SetTextColour(colour);
    whoAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
    m_chatLog->SetDefaultStyle(whoAttr);
    m_chatLog->AppendText(who + ": ");

    wxTextAttr msgAttr;
    msgAttr.SetTextColour(wxColour(220, 220, 220));
    m_chatLog->SetDefaultStyle(msgAttr);
    m_chatLog->AppendText(text + "\n");

    m_chatLog->ShowPosition(m_chatLog->GetLastPosition());
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ChatFrame::OnSendBtn(wxCommandEvent&)    { Send(); }
void ChatFrame::OnInputEnter(wxCommandEvent&) { Send(); }

void ChatFrame::OnClose(wxCloseEvent& evt) {
    Hide();
    evt.Veto();
}
