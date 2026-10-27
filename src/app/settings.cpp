#include "app/settings.h"

#include <string>

#include "common/clipboard.h"
#include "common/crypto.h"
#include "common/log.h"
#include "common/net.h"

namespace lk {
namespace {

enum ControlId {
    kIdPeer = 2001,
    kIdPort = 2002,
    kIdPsk = 2003,
    kIdEdge = 2004,
    kIdClipboard = 2005,
    kIdDropzone = 2006,
    kIdEncrypt = 2007,
    kIdAutoConnect = 2008,
    kIdSave = 2009,
    kIdCancel = 2010,
    kIdLocalIp = 2011,
    kIdRoleMaster = 2012,
    kIdRoleAgent = 2013,
    kIdLocalHint = 2014,
};

constexpr int kWidth = 460;
constexpr int kHeight = 500;

class Dialog {
public:
    bool show(HWND parent, Config* cfg);
    Config* cfg_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    bool saved_ = false;
    HFONT font_ = nullptr;

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void create_controls();
    void load_values();
    bool save_values();
    void apply_role_state();
};

void set_text(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

std::wstring get_text(HWND control) {
    int len = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(control, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void to_utf8(const std::wstring& wide, std::string* out) {
    out->clear();
    wide_to_utf8(wide, out);
}

}  // namespace

void Dialog::create_controls() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    auto* self = this;

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
                    int h, int id) -> HWND {
        HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                       hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       instance, nullptr);
        if (control && font_) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        return control;
    };

    font_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    self->parent_ = GetParent(hwnd_);

    make(L"STATIC", L"角色", 0, 20, 20, 60, 22, -1);
    make(L"BUTTON", L"主控端（这台接键鼠）", BS_AUTORADIOBUTTON | WS_GROUP, 90, 18, 150, 24,
         kIdRoleMaster);
    make(L"BUTTON", L"受控端（这台被控制）", BS_AUTORADIOBUTTON, 250, 18, 150, 24, kIdRoleAgent);

    make(L"STATIC", L"对端 IP", 0, 20, 58, 80, 22, -1);
    make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 110, 56, 300, 24, kIdPeer);

    make(L"STATIC", L"端口", 0, 20, 94, 80, 22, -1);
    make(L"EDIT", L"", WS_BORDER | ES_NUMBER, 110, 92, 100, 24, kIdPort);

    make(L"STATIC", L"预共享密钥 (64 位十六进制)", 0, 20, 130, 260, 22, -1);
    make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 110, 152, 300, 24, kIdPsk);

    make(L"STATIC", L"对端在我的哪一侧", 0, 20, 190, 160, 22, -1);
    HWND combo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 190, 188, 140, 120,
                      kIdEdge);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"右侧"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"左侧"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"上方"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"下方"));

    make(L"BUTTON", L"同步剪贴板", BS_AUTOCHECKBOX, 20, 228, 150, 24, kIdClipboard);
    make(L"BUTTON", L"显示文件投递框", BS_AUTOCHECKBOX, 20, 256, 170, 24, kIdDropzone);
    make(L"BUTTON", L"加密剪贴板 (AES-256-GCM)", BS_AUTOCHECKBOX, 20, 284, 250, 24, kIdEncrypt);
    make(L"BUTTON", L"启动时自动连接", BS_AUTOCHECKBOX, 20, 312, 180, 24, kIdAutoConnect);

    make(L"STATIC", L"本机地址", 0, 20, 350, 80, 22, -1);
    make(L"STATIC", L"", 0, 110, 350, 320, 22, kIdLocalIp);
    make(L"STATIC", L"把这个地址填到主控端的「对端 IP」", 0, 110, 374, 320, 20, kIdLocalHint);

    make(L"BUTTON", L"保存", BS_DEFPUSHBUTTON, 230, 410, 100, 32, kIdSave);
    make(L"BUTTON", L"取消", BS_PUSHBUTTON, 340, 410, 100, 32, kIdCancel);

    set_text(GetDlgItem(hwnd_, kIdLocalIp), L"获取中...");
    std::string ip = cfg_->peer_ip.empty() ? local_ipv4() : local_ipv4_for(cfg_->peer_ip);
    std::wstring wide;
    utf8_to_wide(ip, &wide);
    set_text(GetDlgItem(hwnd_, kIdLocalIp), wide + L":" + std::to_wstring(cfg_->port));

    load_values();
}

void Dialog::load_values() {
    std::wstring wide;
    utf8_to_wide(cfg_->peer_ip, &wide);
    set_text(GetDlgItem(hwnd_, kIdPeer), wide);
    set_text(GetDlgItem(hwnd_, kIdPort), std::to_wstring(cfg_->port));
    utf8_to_wide(cfg_->psk_hex, &wide);
    set_text(GetDlgItem(hwnd_, kIdPsk), wide);

    int edge_index = 0;
    if (cfg_->edge == "left") {
        edge_index = 1;
    } else if (cfg_->edge == "top") {
        edge_index = 2;
    } else if (cfg_->edge == "bottom") {
        edge_index = 3;
    }
    SendMessageW(GetDlgItem(hwnd_, kIdEdge), CB_SETCURSEL, edge_index, 0);

    CheckDlgButton(hwnd_, kIdClipboard, cfg_->clipboard ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, kIdDropzone, cfg_->dropzone ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, kIdEncrypt, cfg_->encrypt_clipboard ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, kIdAutoConnect, cfg_->auto_connect ? BST_CHECKED : BST_UNCHECKED);

    CheckRadioButton(hwnd_, kIdRoleMaster, kIdRoleAgent,
                     cfg_->role == "agent" ? kIdRoleAgent : kIdRoleMaster);
    apply_role_state();
}

void Dialog::apply_role_state() {
    const bool is_agent = IsDlgButtonChecked(hwnd_, kIdRoleAgent) == BST_CHECKED;
    EnableWindow(GetDlgItem(hwnd_, kIdPeer), !is_agent);
    EnableWindow(GetDlgItem(hwnd_, kIdEdge), !is_agent);
    EnableWindow(GetDlgItem(hwnd_, kIdAutoConnect), !is_agent);
}

bool Dialog::save_values() {
    const bool is_agent = IsDlgButtonChecked(hwnd_, kIdRoleAgent) == BST_CHECKED;
    std::wstring peer = get_text(GetDlgItem(hwnd_, kIdPeer));
    std::wstring port_text = get_text(GetDlgItem(hwnd_, kIdPort));
    std::wstring psk = get_text(GetDlgItem(hwnd_, kIdPsk));

    if (!is_agent && peer.empty()) {
        MessageBoxW(hwnd_, L"主控端必须填写对端 IP。", L"设置", MB_OK | MB_ICONWARNING);
        return false;
    }
    int port = _wtoi(port_text.c_str());
    if (port < 1 || port > 65535) {
        MessageBoxW(hwnd_, L"端口必须在 1-65535 之间。", L"设置", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::string psk_utf8;
    to_utf8(psk, &psk_utf8);
    std::vector<uint8_t> key;
    if (!from_hex(psk_utf8, &key) || key.size() != 32) {
        MessageBoxW(hwnd_, L"预共享密钥必须是 64 位十六进制字符（32 字节）。", L"设置",
                    MB_OK | MB_ICONWARNING);
        return false;
    }

    if (!peer.empty()) {
        to_utf8(peer, &cfg_->peer_ip);
    }
    cfg_->role = is_agent ? "agent" : "master";
    cfg_->port = static_cast<uint16_t>(port);
    cfg_->psk_hex = psk_utf8;

    int edge_index = static_cast<int>(SendMessageW(GetDlgItem(hwnd_, kIdEdge), CB_GETCURSEL, 0, 0));
    switch (edge_index) {
        case 1: cfg_->edge = "left"; break;
        case 2: cfg_->edge = "top"; break;
        case 3: cfg_->edge = "bottom"; break;
        default: cfg_->edge = "right"; break;
    }
    cfg_->clipboard = IsDlgButtonChecked(hwnd_, kIdClipboard) == BST_CHECKED;
    cfg_->dropzone = IsDlgButtonChecked(hwnd_, kIdDropzone) == BST_CHECKED;
    cfg_->encrypt_clipboard = IsDlgButtonChecked(hwnd_, kIdEncrypt) == BST_CHECKED;
    cfg_->auto_connect = IsDlgButtonChecked(hwnd_, kIdAutoConnect) == BST_CHECKED;
    return true;
}

LRESULT CALLBACK Dialog::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE:
            if (self) {
                self->hwnd_ = hwnd;
                self->create_controls();
            }
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wparam);
            if ((id == kIdRoleMaster || id == kIdRoleAgent) && self) {
                self->apply_role_state();
                return 0;
            }
            if (id == kIdSave && self) {
                if (self->save_values()) {
                    self->saved_ = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            if (id == kIdCancel) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (self && self->font_) {
                DeleteObject(self->font_);
                self->font_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool Dialog::show(HWND parent, Config* cfg) {
    cfg_ = cfg;
    parent_ = parent;
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Dialog::wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LanKVM.Settings";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    RECT parent_rect{};
    if (parent) {
        GetWindowRect(parent, &parent_rect);
    }
    int x = parent_rect.left + ((parent_rect.right - parent_rect.left) - kWidth) / 2;
    int y = parent_rect.top + ((parent_rect.bottom - parent_rect.top) - kHeight) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LanKVM.Settings", L"LanKVM 设置",
                                WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE, x, y, kWidth,
                                kHeight, parent, nullptr, instance, this);
    if (!hwnd) {
        return false;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (parent) {
        EnableWindow(parent, FALSE);
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    return saved_;
}

bool SettingsDialog::show(HWND parent, Config* cfg) {
    Dialog dialog;
    return dialog.show(parent, cfg);
}

}  // namespace lk
