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

constexpr uint8_t kFormatPng = 4;

UINT png_format() {
    static const UINT id = RegisterClipboardFormatW(L"PNG");
    return id;
}

// CF_DIBV5 carries a BITMAPV5HEADER; CF_DIB expects a plain BITMAPINFOHEADER
// (plus colour masks when BI_BITFIELDS). Everything after the header is the
// same pixel data, so this is a header rewrite.
bool dibv5_to_dib(const std::vector<uint8_t>& src, std::vector<uint8_t>* out) {
    if (src.size() < sizeof(BITMAPV5HEADER)) {
        return false;
    }
    const auto* v5 = reinterpret_cast<const BITMAPV5HEADER*>(src.data());
    if (v5->bV5Size < sizeof(BITMAPINFOHEADER) || src.size() < v5->bV5Size) {
        return false;
    }

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = v5->bV5Width;
    bi.biHeight = v5->bV5Height;
    bi.biPlanes = v5->bV5Planes;
    bi.biBitCount = v5->bV5BitCount;
    bi.biCompression = v5->bV5Compression;
    bi.biSizeImage = v5->bV5SizeImage;
    bi.biXPelsPerMeter = v5->bV5XPelsPerMeter;
    bi.biYPelsPerMeter = v5->bV5YPelsPerMeter;
    bi.biClrUsed = v5->bV5ClrUsed;
    bi.biClrImportant = v5->bV5ClrImportant;

    out->clear();
    const auto* header_bytes = reinterpret_cast<const uint8_t*>(&bi);
    out->insert(out->end(), header_bytes, header_bytes + sizeof(bi));
    if (bi.biCompression == BI_BITFIELDS) {
        const auto* masks = reinterpret_cast<const uint8_t*>(&v5->bV5RedMask);
        out->insert(out->end(), masks, masks + sizeof(DWORD) * 3);
    }
    out->insert(out->end(), src.begin() + v5->bV5Size, src.end());
    return true;
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
    } else if (IsClipboardFormatAvailable(CF_DIBV5)) {
        // Screenshot tools often publish DIBV5 only. Convert it to a plain DIB
        // so the receiving side can put a widely supported format on the
        // clipboard.
        HANDLE handle = GetClipboardData(CF_DIBV5);
        if (handle) {
            SIZE_T size = GlobalSize(handle);
            void* data = GlobalLock(handle);
            if (data && size > 0) {
                std::vector<uint8_t> raw(static_cast<size_t>(size));
                std::memcpy(raw.data(), data, raw.size());
                std::vector<uint8_t> dib;
                if (dibv5_to_dib(raw, &dib)) {
                    out->format = 2;
                    out->data = std::move(dib);
                    ok = true;
                }
            }
            if (data) {
                GlobalUnlock(handle);
            }
        }
    } else {
        const UINT png = png_format();
        if (png != 0 && IsClipboardFormatAvailable(png)) {
            HANDLE handle = GetClipboardData(png);
            if (handle) {
                SIZE_T size = GlobalSize(handle);
                void* data = GlobalLock(handle);
                if (data && size > 0) {
                    out->format = kFormatPng;
                    out->data.resize(size);
                    std::memcpy(out->data.data(), data, size);
                    ok = true;
                }
                if (data) {
                    GlobalUnlock(handle);
                }
            }
        }
    }

    if (!ok && err) {
        *err = "unsupported clipboard format";
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
    } else if (blob.format == kFormatPng) {
        // Peer only had a PNG image; publish it under the registered PNG
        // format. Word/Chrome/Explorer accept it, Paint does not.
        const UINT png = png_format();
        if (png != 0) {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, blob.data.size());
            if (mem) {
                void* dst = GlobalLock(mem);
                if (dst) {
                    std::memcpy(dst, blob.data.data(), blob.data.size());
                    GlobalUnlock(mem);
                    if (SetClipboardData(png, mem)) {
                        ok = true;
                    } else {
                        GlobalFree(mem);
                    }
                } else {
                    GlobalFree(mem);
                }
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

std::string clipboard_formats_report() {
    std::string out;
    if (!OpenClipboard(nullptr)) {
        return "cannot open clipboard\n";
    }
    out += "formats on the clipboard:\n";
    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        wchar_t name[128]{};
        if (GetClipboardFormatNameW(format, name, static_cast<int>(std::size(name))) > 0) {
            std::string utf8;
            wide_to_utf8(std::wstring(name), &utf8);
            out += "  [" + std::to_string(format) + "] " + utf8 + "\n";
        } else {
            out += "  [" + std::to_string(format) + "] (standard id)\n";
        }
    }
    CloseClipboard();

    ClipBlob blob;
    std::string err;
    if (clipboard_read(&blob, &err)) {
        out += "readable by LanKVM: yes, format=" + std::to_string(blob.format) +
               " bytes=" + std::to_string(blob.data.size()) + "\n";
    } else {
        out += "readable by LanKVM: no (" + err + ")\n";
    }
    return out;
}

}  // namespace lk
