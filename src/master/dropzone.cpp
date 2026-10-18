#include "master/dropzone.h"

#include <shellapi.h>

#include "common/clipboard.h"
#include "common/log.h"

namespace lk {
namespace {

constexpr wchar_t kClassName[] = L"LanKVM.DropZone";
constexpr int kWidth = 190;
constexpr int kHeight = 110;
constexpr int kMargin = 24;

}  // namespace

DropZone::~DropZone() { destroy(); }

bool DropZone::create(HINSTANCE instance, DropFn on_drop) {
    instance_ = instance;
    on_drop_ = std::move(on_drop);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &DropZone::wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.right - kWidth - kMargin;
    int y = work.bottom - kHeight - kMargin;

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                            kClassName, L"LanKVM", WS_POPUP, x, y, kWidth, kHeight, nullptr,
                            nullptr, instance, this);
    if (!hwnd_) {
        LK_ERROR("cannot create drop zone window");
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(hwnd_, 0, 216, LWA_ALPHA);
    DragAcceptFiles(hwnd_, TRUE);

    font_ = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return true;
}

void DropZone::destroy() {
    if (hwnd_) {
        DragAcceptFiles(hwnd_, FALSE);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void DropZone::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(32, 34, 40));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_DOT, 1, RGB(120, 200, 255));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 8, 8, rc.right - 8, rc.bottom - 8);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(230, 236, 245));
    HGDIOBJ old_font = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, L"拖文件到这里\n发送到对端", -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    SelectObject(dc, old_font);

    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK DropZone::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<DropZone*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_PAINT:
            if (self) {
                self->paint();
                return 0;
            }
            break;
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wparam);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::string> paths;
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(len + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), len + 1);
                path.resize(len);
                std::string utf8;
                if (wide_to_utf8(path, &utf8)) {
                    paths.push_back(utf8);
                }
            }
            DragFinish(drop);
            if (self && self->on_drop_ && !paths.empty()) {
                self->on_drop_(paths);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            MessageBoxW(hwnd, L"把文件拖到这个方框里，就会发送到对端机器的下载目录。",
                        L"LanKVM 文件投递", MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace lk
