#pragma once

#include "TarvosProtocol.h"

#include <wx/wx.h>

#include <functional>

class ChatFrame : public wxFrame {
public:
    /// Queues a request with the owning window; returns an error message, or
    /// an empty string if the request was accepted.
    using SubmitFn = std::function<wxString(uint8_t cmd, const std::vector<uint8_t>& payload)>;

    ChatFrame(wxWindow* parent, SubmitFn submit);

    /// Address mode (-1 = unknown) and maximum packet size (0 = unknown)
    /// read from the module. Adjusts which address fields apply.
    void SetModuleInfo(int addrMode, size_t maxPacket);

    /// Called by MainFrame when a DATAEX_IND arrives while chat is open.
    void ReceiveMessage(const TarvosProtocol::RxData& rx);

    /// Called by MainFrame with the confirmation of a request this window sent.
    void OnConfirmation(uint8_t requestCmd, const TarvosProtocol::Frame& cnf);

    /// Called by MainFrame when a request this window sent got no confirmation.
    void OnTimeout(uint8_t requestCmd);

    /// Called by MainFrame on RESET_IND: runtime settings are back to defaults.
    void OnModuleRestarted();

private:
    void BuildUI();
    void Send();
    void ApplySettings();
    bool Submit(uint8_t cmd, const std::vector<uint8_t>& payload);
    void AppendLine(const wxString& who, const wxString& text, const wxColour& colour);
    void Info(const wxString& text);
    void Error(const wxString& text);

    void OnSendBtn(wxCommandEvent& evt);
    void OnInputEnter(wxCommandEvent& evt);
    void OnApply(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    // Chat area
    wxTextCtrl* m_chatLog  = nullptr;
    wxTextCtrl* m_inputBox = nullptr;
    wxButton*   m_sendBtn  = nullptr;

    // Settings bar (runtime settings, decimal)
    wxTextCtrl*   m_channelInput = nullptr;
    wxTextCtrl*   m_netIdInput   = nullptr;
    wxTextCtrl*   m_addrInput    = nullptr;
    wxButton*     m_applyBtn     = nullptr;
    wxStaticText* m_modeLabel    = nullptr;

    SubmitFn m_submit;
    int      m_addrMode  = -1;
    size_t   m_maxPacket = 0;

    wxDECLARE_EVENT_TABLE();
};
