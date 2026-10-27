#include "ChatFrame.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/datetime.h>

namespace ID {
    enum {
        SEND_BTN = wxID_HIGHEST + 100,
        INPUT_BOX,
        APPLY_BTN,
    };
}

wxBEGIN_EVENT_TABLE(ChatFrame, wxFrame)
    EVT_BUTTON(ID::SEND_BTN,      ChatFrame::OnSendBtn)
    EVT_TEXT_ENTER(ID::INPUT_BOX, ChatFrame::OnInputEnter)
    EVT_BUTTON(ID::APPLY_BTN,     ChatFrame::OnApply)
    EVT_CLOSE(ChatFrame::OnClose)
wxEND_EVENT_TABLE()

// Factory default RF channel (RADIO_DefaultRfChannel, manual 8.2.4).
static constexpr int kDefaultChannel = 106;

// Factory default RF profile 0 allows 128-byte packets (manual table 13).
static constexpr size_t kDefaultMaxPacket = 128;

static const wxColour kErrColour (255, 80, 80);
static const wxColour kInfoColour(200, 200, 100);

/// Parse a decimal number in [0, max]. Returns false on anything else.
static bool ParseDecimal(const wxString& text, unsigned long max, unsigned long& out) {
    wxString s = text;
    s.Trim(true).Trim(false);
    return !s.IsEmpty() && s.ToULong(&out, 10) && out <= max;
}

ChatFrame::ChatFrame(wxWindow* parent, SubmitFn submit)
    : wxFrame(parent, wxID_ANY, "Chat",
              wxDefaultPosition, wxSize(620, 540),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT)
    , m_submit(std::move(submit))
{
    SetMinSize(wxSize(540, 440));
    BuildUI();
    SetModuleInfo(-1, 0);
    Centre();
}

void ChatFrame::BuildUI() {
    wxFont mono(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE));
    auto* root = new wxBoxSizer(wxVERTICAL);

    // ── Settings bar (runtime channel / destination) ──────────────────────────
    auto* settingsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Radio settings (runtime)");
    auto* fieldsRow   = new wxBoxSizer(wxHORIZONTAL);

    auto addField = [&](const char* label, const wxString& value, const char* hint) {
        fieldsRow->Add(new wxStaticText(this, wxID_ANY, label),
                       0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        auto* ctrl = new wxTextCtrl(this, wxID_ANY, value,
                                    wxDefaultPosition, wxSize(70, -1));
        ctrl->SetFont(mono);
        ctrl->SetHint(hint);
        fieldsRow->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
        return ctrl;
    };

    m_channelInput = addField("Channel:",   wxString::Format("%d", kDefaultChannel), "0-140");
    m_netIdInput   = addField("Dest Net ID:", wxEmptyString, "0-255");
    m_addrInput    = addField("Dest Addr:", wxEmptyString, "decimal");

    fieldsRow->AddStretchSpacer();
    m_applyBtn = new wxButton(this, ID::APPLY_BTN, "Apply");
    fieldsRow->Add(m_applyBtn, 0, wxALIGN_CENTER_VERTICAL);

    settingsBox->Add(fieldsRow, 0, wxEXPAND | wxALL, 4);

    m_modeLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
    settingsBox->Add(m_modeLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

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
// Module information from MainFrame
// ---------------------------------------------------------------------------

void ChatFrame::SetModuleInfo(int addrMode, size_t maxPacket) {
    m_addrMode  = addrMode;
    m_maxPacket = maxPacket;

    m_netIdInput->Enable(addrMode >= 2);
    m_addrInput->Enable(addrMode >= 1);
    m_addrInput->SetHint(addrMode == 3 ? "0-65535" : "0-255");

    wxString text;
    switch (addrMode) {
        case 0:
            text = "Address mode 0: every message is received by all modules on the same channel.";
            break;
        case 1:
            text = "Address mode 1: Dest Addr = the other module's MAC_SourceAddr (255 = broadcast).";
            break;
        case 2:
        case 3:
            text = wxString::Format("Address mode %d: Dest Net ID / Addr = the other module's "
                                    "MAC_SourceNetID / MAC_SourceAddr (255%s = broadcast).",
                                    addrMode, addrMode == 3 ? " / 65535" : "");
            break;
        default:
            text = "Address mode unknown (could not read it from the module).";
            break;
    }
    m_modeLabel->SetLabel(text + "\nThese settings are lost when the module resets.");
    Layout();
}

// ---------------------------------------------------------------------------
// Send text message
// ---------------------------------------------------------------------------

bool ChatFrame::Submit(uint8_t cmd, const std::vector<uint8_t>& payload) {
    wxString err = m_submit(cmd, payload);
    if (!err.IsEmpty()) {
        Error(err);
        return false;
    }
    return true;
}

void ChatFrame::Send() {
    wxString text = m_inputBox->GetValue().Trim(true).Trim(false);
    if (text.IsEmpty()) return;

    std::string utf8 = text.ToUTF8().data();
    const size_t limit = m_maxPacket ? m_maxPacket : kDefaultMaxPacket;
    if (utf8.size() > limit) {
        wxMessageBox(wxString::Format("Message too long: %zu bytes, the RF profile allows %zu.",
                                      utf8.size(), limit),
                     "Cannot Send", wxOK | wxICON_WARNING, this);
        return;
    }

    if (!Submit(TarvosProtocol::CMD_DATA_REQ, std::vector<uint8_t>(utf8.begin(), utf8.end())))
        return;
    AppendLine("You", text, wxColour(100, 180, 255));
    m_inputBox->Clear();
}

// ---------------------------------------------------------------------------
// Apply channel / destination (volatile runtime settings)
// ---------------------------------------------------------------------------

void ChatFrame::ApplySettings() {
    unsigned long channel = 0, netId = 0, addr = 0;

    if (!ParseDecimal(m_channelInput->GetValue(), 140, channel)) {
        Error("Channel must be a number from 0 to 140.");
        return;
    }
    if (m_addrMode >= 2 && !ParseDecimal(m_netIdInput->GetValue(), 255, netId)) {
        Error("Dest Net ID must be a number from 0 to 255.");
        return;
    }
    const unsigned long maxAddr = (m_addrMode == 3) ? 65535 : 255;
    if (m_addrMode >= 1 && !ParseDecimal(m_addrInput->GetValue(), maxAddr, addr)) {
        Error(wxString::Format("Dest Addr must be a number from 0 to %lu.", maxAddr));
        return;
    }

    if (!Submit(TarvosProtocol::CMD_SET_CHANNEL_REQ, { static_cast<uint8_t>(channel) }))
        return;
    if (m_addrMode >= 2 &&
        !Submit(TarvosProtocol::CMD_SET_DESTNETID_REQ, { static_cast<uint8_t>(netId) }))
        return;
    if (m_addrMode >= 1) {
        std::vector<uint8_t> payload{ static_cast<uint8_t>(addr & 0xFF) };
        if (m_addrMode == 3) payload.push_back(static_cast<uint8_t>(addr >> 8)); // LSB first
        Submit(TarvosProtocol::CMD_SET_DESTADDR_REQ, payload);
    }
}

// ---------------------------------------------------------------------------
// Results from MainFrame
// ---------------------------------------------------------------------------

void ChatFrame::OnConfirmation(uint8_t requestCmd, const TarvosProtocol::Frame& cnf) {
    if (cnf.payload.empty()) return;
    const uint8_t value = cnf.payload[0];

    switch (requestCmd) {
        case TarvosProtocol::CMD_DATA_REQ:
            if (value != 0x00)
                Error("Message not delivered: " + TarvosProtocol::StatusText(cnf.cmd, value));
            break;
        case TarvosProtocol::CMD_SET_CHANNEL_REQ:
            Info(wxString::Format("Channel set to %d (%.3f MHz)",
                                  value, TarvosProtocol::ChannelToMHz(value)));
            break;
        case TarvosProtocol::CMD_SET_DESTNETID_REQ:
        case TarvosProtocol::CMD_SET_DESTADDR_REQ: {
            const char* what = requestCmd == TarvosProtocol::CMD_SET_DESTNETID_REQ
                               ? "Destination net ID" : "Destination address";
            if (value == 0x00)
                Info(wxString::Format("%s set", what));
            else
                Error(wxString::Format("%s not set: %s", what,
                                       TarvosProtocol::StatusText(cnf.cmd, value)));
            break;
        }
        default:
            break;
    }
}

void ChatFrame::OnTimeout(uint8_t requestCmd) {
    Error(requestCmd == TarvosProtocol::CMD_DATA_REQ
          ? "Message not confirmed by the module (timeout)."
          : "Setting not confirmed by the module (timeout).");
}

void ChatFrame::OnModuleRestarted() {
    Info("Module restarted: channel and destination are back to their stored defaults. "
         "Press Apply to set them again.");
}

void ChatFrame::ReceiveMessage(const TarvosProtocol::RxData& rx) {
    wxString who = "Remote";
    if (rx.netId >= 0)
        who += wxString::Format(" %d.%d", rx.netId, rx.addr);
    else if (rx.addr >= 0)
        who += wxString::Format(" %d", rx.addr);

    std::string text(rx.data.begin(), rx.data.end());
    AppendLine(who, wxString::FromUTF8(text) + wxString::Format("   (%d dBm)", rx.rssiDbm),
               wxColour(120, 220, 120));
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

void ChatFrame::Info(const wxString& text)  { AppendLine("INFO", text, kInfoColour); }
void ChatFrame::Error(const wxString& text) { AppendLine("ERR",  text, kErrColour); }

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ChatFrame::OnSendBtn(wxCommandEvent&)    { Send(); }
void ChatFrame::OnInputEnter(wxCommandEvent&) { Send(); }
void ChatFrame::OnApply(wxCommandEvent&)      { ApplySettings(); }

void ChatFrame::OnClose(wxCloseEvent& evt) {
    Hide();
    evt.Veto();
}
