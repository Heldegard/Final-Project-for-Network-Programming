#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace lk {

class Tray {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 20;

    enum Command {
        kCmdConnect = 1001,
        kCmdDisconnect = 1002,
        kCmdSettings = 1003,
        kCmdOpenLog = 1004,
        kCmdAbout = 1005,
        kCmdExit = 1006,
    };

    ~Tray();

    bool create(HWND owner, const std::wstring& tooltip);
    void destroy();
    void update_tooltip(const std::wstring& tooltip);
    void balloon(const std::wstring& title, const std::wstring& text);
    void set_state(bool connected, bool remote);
    int show_menu();

private:
    NOTIFYICONDATAW data_{};
    HWND owner_ = nullptr;
    bool added_ = false;
    bool connected_ = false;
    bool remote_ = false;
};

}  // namespace lk
