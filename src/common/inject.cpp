#include "common/inject.h"

#include <windows.h>

#include "common/log.h"

namespace lk {
namespace {

void send_one(const INPUT& input) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (SendInput(1, const_cast<INPUT*>(&input), sizeof(INPUT)) == 1) {
            return;
        }
        Sleep(1);
    }
}

INPUT make_mouse(DWORD flags, LONG dx, LONG dy, DWORD data) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.mouseData = data;
    in.mi.dwFlags = flags;
    return in;
}

}  // namespace

void Injector::move_absolute(uint16_t nx, uint16_t ny) {
    INPUT in = make_mouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, nx, ny,
                          0);
    send_one(in);
}

void Injector::mouse_button(uint8_t button, bool down) {
    DWORD flag = 0;
    switch (static_cast<MouseButton>(button)) {
        case MouseButton::Left:
            flag = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case MouseButton::Right:
            flag = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButton::Middle:
            flag = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case MouseButton::X1:
            flag = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            break;
        case MouseButton::X2:
            flag = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            break;
        default:
            return;
    }
    DWORD data = 0;
    if (button == static_cast<uint8_t>(MouseButton::X1)) {
        data = XBUTTON1;
    } else if (button == static_cast<uint8_t>(MouseButton::X2)) {
        data = XBUTTON2;
    }
    INPUT in = make_mouse(flag, 0, 0, data);
    send_one(in);

    if (down) {
        pressed_buttons_.insert(button);
    } else {
        pressed_buttons_.erase(button);
    }
}

void Injector::wheel(int32_t delta) {
    INPUT in = make_mouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(delta));
    send_one(in);
}

void Injector::key(uint32_t packed_scan, bool down) {
    uint32_t scan = packed_scan & 0xFFFF;
    bool extended = (packed_scan & 0x10000) != 0;

    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = static_cast<WORD>(scan);
    in.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (extended) {
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (!down) {
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    send_one(in);

    if (down) {
        pressed_keys_.insert(packed_scan);
    } else {
        pressed_keys_.erase(packed_scan);
    }
}

void Injector::inject(const std::vector<InputEvent>& events) {
    for (const InputEvent& e : events) {
        switch (e.kind) {
            case InputKind::MouseMove:
                move_absolute(static_cast<uint16_t>((e.data >> 16) & 0xFFFF),
                              static_cast<uint16_t>(e.data & 0xFFFF));
                break;
            case InputKind::MouseButton:
                mouse_button(e.flags, e.data != 0);
                break;
            case InputKind::MouseWheel:
                wheel(static_cast<int32_t>(e.data));
                break;
            case InputKind::KeyDown:
                key(e.data, true);
                break;
            case InputKind::KeyUp:
                key(e.data, false);
                break;
        }
    }
}

void Injector::release_all() {
    for (uint32_t scan : pressed_keys_) {
        key(scan, false);
    }
    pressed_keys_.clear();
    for (uint8_t button : pressed_buttons_) {
        mouse_button(button, false);
    }
    pressed_buttons_.clear();
}

}  // namespace lk
