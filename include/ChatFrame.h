#pragma once

#include "SerialPort.h"
#include "TarvosProtocol.h"

#include <wx/wx.h>

class ChatFrame : public wxFrame {
public:
    ChatFrame(wxWindow* parent, SerialPort& serial);

    /// Called by MainFrame when a DATAEX_IND arrives while chat is open.
    void ReceiveText(const std::string& text);

private:
    void BuildUI();
    void Send();
    void SendCommand(uint8_t cmd, const std::vector<uint8_t>& payload);
    void AppendLine(const wxString& who, const wxString& text, const wxColour& colour);

    void OnSendBtn(wxCommandEvent& evt);
    void OnInputEnter(wxCommandEvent& evt);
    void OnSetAddr(wxCommandEvent& evt);
    void OnSetChannel(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    // Chat area
    wxTextCtrl* m_chatLog  = nullptr;
    wxTextCtrl* m_inputBox = nullptr;
    wxButton*   m_sendBtn  = nullptr;

    // Settings bar
    wxTextCtrl* m_addrInput    = nullptr;
    wxButton*   m_setAddrBtn   = nullptr;
    wxTextCtrl* m_channelInput = nullptr;
    wxButton*   m_setChannelBtn = nullptr;

    SerialPort& m_serial;

    wxDECLARE_EVENT_TABLE();
};
