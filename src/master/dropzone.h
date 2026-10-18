#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace lk {

// A small always-on-top drop target. Dragging files onto it sends them to the
// peer machine. Uses the shell's DragAcceptFiles path (WM_DROPFILES), which is
// far simpler and more robust than implementing IDropTarget.
class DropZone {
public:
    using DropFn = std::function<void(const std::vector<std::string>&)>;

    DropZone() = default;
    ~DropZone();

    bool create(HINSTANCE instance, DropFn on_drop);
    void destroy();
    bool valid() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paint();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    DropFn on_drop_;
    HFONT font_ = nullptr;
};

}  // namespace lk
