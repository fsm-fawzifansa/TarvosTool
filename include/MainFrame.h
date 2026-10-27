#pragma once

#include "SerialPort.h"
#include "TarvosProtocol.h"
#include "ChatFrame.h"

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/timer.h>

#include <deque>
#include <optional>

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
    void UpdateModeUI();          ///< Enable/disable controls for command vs transparent mode
    void PopulatePorts();

    // -----------------------------------------------------------------------
    // Command panel logic
    // -----------------------------------------------------------------------
    void UpdateCommandUI();       ///< Refresh param row visibility + labels from selection
    void UpdateFramePreview();    ///< Parse inputs and display wire-frame hex bytes
    bool BuildCommandPayload(std::vector<uint8_t>& payload, wxString& err);
    bool ConfirmCommand(uint8_t cmd, const std::vector<uint8_t>& payload);
    int  SelectedSettingIndex() const;

    // -----------------------------------------------------------------------
    // Logging
    // -----------------------------------------------------------------------
    enum class LogLevel { Info, TX, RX, Error };
    void Log(const wxString& msg, LogLevel level = LogLevel::Info);

    // -----------------------------------------------------------------------
    // Request queue — the module accepts one request at a time and answers
    // each with a confirmation (manual 7.1).
    // -----------------------------------------------------------------------
    enum class Origin { Builder, Chat, Auto };
    struct Request {
        uint8_t              cmd;
        std::vector<uint8_t> payload;
        Origin               origin;
    };

    /// Queue a request. Returns an error message, or an empty string if accepted.
    wxString SubmitRequest(uint8_t cmd, const std::vector<uint8_t>& payload, Origin origin);
    void PumpQueue();
    void CheckTimeouts(long long nowMs);
    void ClearRequests();

    void HandleFrame(const TarvosProtocol::Frame& f);
    void HandleConfirmation(const Request& req, const TarvosProtocol::Frame& cnf);
    void HandleModeConfirmation(const TarvosProtocol::Frame& cnf);
    void HandleTransparentRx(const std::vector<uint8_t>& raw);

    /// Read MAC_DefaultAddressMode and RADIO_DefaultRfProfile from the module.
    void QueryModuleInfo();
    void UpdateModuleInfo();

    // -----------------------------------------------------------------------
    // Serial helpers
    // -----------------------------------------------------------------------
    bool SendFrame(const std::vector<uint8_t>& frame);
    void UpdateStatusBar();

    // -----------------------------------------------------------------------
    // Event handlers
    // -----------------------------------------------------------------------
    void OnNewWindow(wxCommandEvent& evt);
    void OnOpenChat(wxCommandEvent& evt);
    void OnReadModuleInfo(wxCommandEvent& evt);
    void OnEscapeTransparent(wxCommandEvent& evt);
    void OnRefreshPorts(wxCommandEvent& evt);
    void OnConnect(wxCommandEvent& evt);
    void OnCommandSelected(wxCommandEvent& evt);
    void OnSettingSelected(wxCommandEvent& evt);
    void OnParamChanged(wxCommandEvent& evt);
    void OnSendCommand(wxCommandEvent& evt);
    void OnTimer(wxTimerEvent& evt);

    wxDECLARE_EVENT_TABLE();

    // -----------------------------------------------------------------------
    // Widgets — menu and connection bar
    // -----------------------------------------------------------------------
    wxMenu*     m_moduleMenu = nullptr;
    wxChoice*   m_portChoice = nullptr;
    wxChoice*   m_baudChoice = nullptr;
    wxCheckBox* m_busyCheck  = nullptr;
    wxButton*   m_connectBtn = nullptr;

    // -----------------------------------------------------------------------
    // Widgets — log
    // -----------------------------------------------------------------------
    wxTextCtrl* m_log = nullptr;

    // -----------------------------------------------------------------------
    // Widgets — command panel
    // -----------------------------------------------------------------------
    wxChoice*         m_cmdChoice     = nullptr;
    wxStaticText*     m_cmdCodeLabel  = nullptr;
    wxStaticBoxSizer* m_cmdBoxSizer   = nullptr; ///< Needed for Show(sizer, bool)
    wxBoxSizer*       m_p1RowSizer    = nullptr;
    wxBoxSizer*       m_p2RowSizer    = nullptr;
    wxStaticText*     m_p1Label       = nullptr;
    wxTextCtrl*       m_p1Input       = nullptr;
    wxChoice*         m_settingChoice = nullptr; ///< Replaces m_p1Input for SET_REQ / GET_REQ
    wxStaticText*     m_p2Label       = nullptr;
    wxTextCtrl*       m_p2Input       = nullptr;
    wxStaticText*     m_previewLabel  = nullptr;
    wxButton*         m_sendCmdBtn    = nullptr;

    // -----------------------------------------------------------------------
    // Chat
    // -----------------------------------------------------------------------
    wxButton*  m_chatBtn = nullptr;
    ChatFrame* m_chat    = nullptr;

    // -----------------------------------------------------------------------
    // Module state
    // -----------------------------------------------------------------------
    int  m_addrMode       = -1;    ///< MAC_DefaultAddressMode, -1 = unknown
    int  m_rfProfile      = -1;    ///< RADIO_DefaultRfProfile, -1 = unknown
    bool m_transparent    = false; ///< Module is in transparent mode
    bool m_restartPending = false; ///< SET_REQ done; only SET/GET/RESET until restart (manual 7.5.1)
    int  m_baud           = 115200;

    std::deque<Request>    m_queue;
    std::optional<Request> m_inflight;
    long long              m_inflightSince = 0;
    long long              m_lastTxAt      = 0;
    long long              m_lastRxAt      = 0;
    long long              m_escapeSince   = 0; ///< "+++" sent, waiting for SET_MODE_CNF (0 = no)

    // -----------------------------------------------------------------------
    // Backend
    // -----------------------------------------------------------------------
    SerialPort     m_serial;
    TarvosProtocol m_proto;
    wxTimer        m_timer;
};
