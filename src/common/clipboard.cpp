#include "common/clipboard.h"

#include <windows.h>
#include <shellapi.h>

#include <cstring>

#include "common/log.h"

namespace lk {
namespace {

bool open_with_retry(HWND owner) {
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(owner)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

}  // namespace

bool utf8_to_wide(const std::string& in, std::wstring* out) {
    if (in.empty()) {
        out->clear();
        return true;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), nullptr, 0);
    if (len <= 0) {
        return false;
    }
    out->resize(static_cast<size_t>(len));
    MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), out->data(), len);
    return true;
}

bool wide_to_utf8(const std::wstring& in, std::string* out) {
    if (in.empty()) {
        out->clear();
        return true;
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 0) {
        return false;
    }
    out->resize(static_cast<size_t>(len));
    WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), out->data(), len,
                        nullptr, nullptr);
    return true;
}

bool clipboard_read(ClipBlob* out, std::string* err) {
    if (!open_with_retry(nullptr)) {
        if (err) {
            *err = "clipboard is busy";
        }
        return false;
    }

    bool ok = false;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        if (handle) {
            auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
            if (text) {
                std::wstring wide(text);
                std::string utf8;
                if (wide_to_utf8(wide, &utf8)) {
                    out->format = 1;
                    out->data.assign(utf8.begin(), utf8.end());
                    ok = true;
                }
                GlobalUnlock(handle);
            }
        }
    } else if (IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE handle = GetClipboardData(CF_DIB);
        if (handle) {
            SIZE_T size = GlobalSize(handle);
            void* data = GlobalLock(handle);
            if (data && size > 0) {
                out->format = 2;
                out->data.resize(size);
                std::memcpy(out->data.data(), data, size);
                ok = true;
            }
            if (data) {
                GlobalUnlock(handle);
            }
        }
    } else {
        if (err) {
            *err = "unsupported clipboard format";
        }
    }

    CloseClipboard();
    return ok;
}

bool clipboard_write(const ClipBlob& blob, std::string* err) {
    if (!open_with_retry(nullptr)) {
        if (err) {
            *err = "clipboard is busy";
        }
        return false;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        if (err) {
            *err = "EmptyClipboard failed";
        }
        return false;
    }

    bool ok = false;
    if (blob.format == 1) {
        std::string utf8(blob.data.begin(), blob.data.end());
        std::wstring wide;
        if (utf8_to_wide(utf8, &wide)) {
            SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (mem) {
                void* dst = GlobalLock(mem);
                if (dst) {
                    std::memcpy(dst, wide.c_str(), bytes);
                    GlobalUnlock(mem);
                    if (SetClipboardData(CF_UNICODETEXT, mem)) {
                        ok = true;
                    } else {
                        GlobalFree(mem);
                    }
                } else {
                    GlobalFree(mem);
                }
            }
        }
    } else if (blob.format == 2) {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, blob.data.size());
        if (mem) {
            void* dst = GlobalLock(mem);
            if (dst) {
                std::memcpy(dst, blob.data.data(), blob.data.size());
                GlobalUnlock(mem);
                if (SetClipboardData(CF_DIB, mem)) {
                    ok = true;
                } else {
                    GlobalFree(mem);
                }
            } else {
                GlobalFree(mem);
            }
        }
    }

    CloseClipboard();
    if (!ok && err) {
        *err = "SetClipboardData failed";
    }
    return ok;
}

bool clipboard_read_files(std::vector<std::string>* paths) {
    if (!open_with_retry(nullptr)) {
        return false;
    }
    bool ok = false;
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE handle = GetClipboardData(CF_HDROP);
        if (handle) {
            auto drop = static_cast<HDROP>(handle);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(len + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), len + 1);
                path.resize(len);
                std::string utf8;
                if (wide_to_utf8(path, &utf8)) {
                    paths->push_back(utf8);
                }
            }
            ok = !paths->empty();
        }
    }
    CloseClipboard();
    return ok;
}

}  // namespace lk
