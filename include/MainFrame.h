#pragma once

#include "SerialPort.h"
#include "TarvosProtocol.h"
#include "ChatFrame.h"

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/timer.h>

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    // -----------------------------------------------------------------------
    // UI construction
    // -----------------------------------------------------------------------
    void BuildMenu();
    void BuildConnectionBar(wxSizer* parent);
    void BuildLogPane(wxSizer* parent);
    void BuildCommandPanel(wxSizer* parent);
    void UpdateConnectedState(bool connected);
    void PopulatePorts();

    // -----------------------------------------------------------------------
    // Command panel logic
    // -----------------------------------------------------------------------
    void UpdateCommandUI();       ///< Refresh param row visibility + labels from selection
    void UpdateFramePreview();    ///< Parse inputs and display wire-frame hex bytes
    bool BuildCommandPayload(std::vector<uint8_t>& payload, wxString& err);

    // -----------------------------------------------------------------------
    // Logging
    // -----------------------------------------------------------------------
    enum class LogLevel { Info, TX, RX, Error };
    void Log(const wxString& msg, LogLevel level = LogLevel::Info);

    // -----------------------------------------------------------------------
    // Serial helpers
    // -----------------------------------------------------------------------
    void SendFrame(const std::vector<uint8_t>& frame);
    void UpdateStatusBar();

    // -----------------------------------------------------------------------
    // Event handlers
    // -----------------------------------------------------------------------
    void OnNewWindow(wxCommandEvent& evt);
    void OnOpenChat(wxCommandEvent& evt);
    void OnRefreshPorts(wxCommandEvent& evt);
    void OnConnect(wxCommandEvent& evt);
    void OnCommandSelected(wxCommandEvent& evt);
    void OnParamChanged(wxCommandEvent& evt);
    void OnSendCommand(wxCommandEvent& evt);
    void OnTimer(wxTimerEvent& evt);

    wxDECLARE_EVENT_TABLE();

    // -----------------------------------------------------------------------
    // Widgets — connection bar
    // -----------------------------------------------------------------------
    wxChoice* m_portChoice = nullptr;
    wxChoice* m_baudChoice = nullptr;
    wxButton* m_connectBtn = nullptr;

    // -----------------------------------------------------------------------
    // Widgets — log
    // -----------------------------------------------------------------------
    wxTextCtrl* m_log = nullptr;

    // -----------------------------------------------------------------------
    // Widgets — command panel
    // -----------------------------------------------------------------------
    wxChoice*         m_cmdChoice    = nullptr;
    wxStaticText*     m_cmdCodeLabel = nullptr;
    wxStaticBoxSizer* m_cmdBoxSizer  = nullptr; ///< Needed for Show(sizer, bool)
    wxBoxSizer*       m_p1RowSizer   = nullptr;
    wxBoxSizer*       m_p2RowSizer   = nullptr;
    wxStaticText*     m_p1Label      = nullptr;
    wxTextCtrl*       m_p1Input      = nullptr;
    wxStaticText*     m_p2Label      = nullptr;
    wxTextCtrl*       m_p2Input      = nullptr;
    wxStaticText*     m_previewLabel = nullptr;
    wxButton*         m_sendCmdBtn   = nullptr;

    // -----------------------------------------------------------------------
    // Chat
    // -----------------------------------------------------------------------

    /// Called in OnTimer for every fully-parsed, checksum-valid frame.
    /// Routes incoming DATAEX_IND payloads to the chat window.
    void OnFrameParsed(const TarvosProtocol::Frame& f);

    wxButton*  m_chatBtn = nullptr;
    ChatFrame* m_chat    = nullptr;

    // -----------------------------------------------------------------------
    // Backend
    // -----------------------------------------------------------------------
    SerialPort     m_serial;
    TarvosProtocol m_proto;
    wxTimer        m_timer;
};
