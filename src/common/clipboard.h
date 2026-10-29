#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lk {

struct ClipBlob {
    uint8_t format = 0;  // 1 = UTF-8 text, 2 = DIB image, 4 = PNG image
    std::vector<uint8_t> data;
};

bool clipboard_read(ClipBlob* out, std::string* err);
bool clipboard_write(const ClipBlob& blob, std::string* err);

// CF_HDROP helpers, used by the drop zone.
bool clipboard_read_files(std::vector<std::string>* paths);

// Human readable dump of the current clipboard formats plus whether LanKVM can
// read them. Used by --clipinfo.
std::string clipboard_formats_report();

bool utf8_to_wide(const std::string& in, std::wstring* out);
bool wide_to_utf8(const std::wstring& in, std::string* out);

}  // namespace lk
