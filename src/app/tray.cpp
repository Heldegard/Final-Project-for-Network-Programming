#include "app/tray.h"

#include <shellapi.h>

#include <cwchar>

namespace lk {
namespace {

void copy_wide(wchar_t* dst, size_t count, const std::wstring& src) {
    if (count == 0) {
        return;
    }
    size_t n = src.size() < count - 1 ? src.size() : count - 1;
    std::wmemcpy(dst, src.c_str(), n);
    dst[n] = L'\0';
}

}  // namespace

Tray::~Tray() { destroy(); }

bool Tray::create(HWND owner, const std::wstring& tooltip) {
    owner_ = owner;
    data_.cbSize = sizeof(NOTIFYICONDATAW);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    copy_wide(data_.szTip, std::size(data_.szTip), tooltip);

    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    return added_;
}

void Tray::destroy() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

void Tray::update_tooltip(const std::wstring& tooltip) {
    if (!added_) {
        return;
    }
    copy_wide(data_.szTip, std::size(data_.szTip), tooltip);
    data_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void Tray::balloon(const std::wstring& title, const std::wstring& text) {
    if (!added_) {
        return;
    }
    data_.uFlags = NIF_INFO;
    copy_wide(data_.szInfoTitle, std::size(data_.szInfoTitle), title);
    copy_wide(data_.szInfo, std::size(data_.szInfo), text);
    data_.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void Tray::set_state(bool connected, bool remote) {
    connected_ = connected;
    remote_ = remote;
}

int Tray::show_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdConnect, L"连接 / 重连");
    AppendMenuW(menu, MF_STRING, kCmdDisconnect, L"断开连接");
    if (!connected_) {
        EnableMenuItem(menu, kCmdDisconnect, MF_GRAYED);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"设置...");
    AppendMenuW(menu, MF_STRING, kCmdOpenLog, L"打开日志");
    AppendMenuW(menu, MF_STRING, kCmdAbout, L"关于");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_);
    int command = static_cast<int>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, owner_,
                       nullptr));
    DestroyMenu(menu);
    return command;
}

}  // namespace lk
