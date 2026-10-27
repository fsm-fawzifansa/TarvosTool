#include "MainFrame.h"
#include <wx/wx.h>

class TarvosApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("TarvosTool");
        SetAppDisplayName("TarvosTool");

        if (!wxApp::OnInit()) return false;

        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(TarvosApp);
