#include "common/config.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/crypto.h"
#include "common/log.h"

namespace lk {
namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool parse_bool(const std::string& v, bool* out) {
    std::string s = lower(trim(v));
    if (s == "1" || s == "true" || s == "yes" || s == "on") { *out = true; return true; }
    if (s == "0" || s == "false" || s == "no" || s == "off") { *out = false; return true; }
    return false;
}

std::wstring exe_dir() {
    wchar_t buf[MAX_PATH * 4]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return L".";
    }
    std::filesystem::path p(buf);
    return p.parent_path().wstring();
}

}  // namespace

std::wstring config_default_path() {
    std::filesystem::path p(exe_dir());
    p /= L"lankvm.ini";
    return p.wstring();
}

bool config_load(const std::wstring& path, Config* cfg, std::string* err) {
    std::ifstream in(std::filesystem::path(path), std::ios::in);
    if (!in.is_open()) {
        if (err) {
            *err = "cannot open config file";
        }
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';' || s[0] == '[') {
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = lower(trim(s.substr(0, eq)));
        std::string value = trim(s.substr(eq + 1));

        if (key == "role") {
            cfg->role = lower(value);
        } else if (key == "peer_ip" || key == "peer") {
            cfg->peer_ip = value;
        } else if (key == "port") {
            cfg->port = static_cast<uint16_t>(std::max(1, std::min(65535, std::atoi(value.c_str()))));
        } else if (key == "psk" || key == "psk_hex") {
            cfg->psk_hex = lower(value);
        } else if (key == "edge") {
            cfg->edge = lower(value);
        } else if (key == "clipboard") {
            parse_bool(value, &cfg->clipboard);
        } else if (key == "encrypt_clipboard") {
            parse_bool(value, &cfg->encrypt_clipboard);
        } else if (key == "dropzone") {
            parse_bool(value, &cfg->dropzone);
        } else if (key == "auto_connect") {
            parse_bool(value, &cfg->auto_connect);
        } else if (key == "download_dir") {
            cfg->download_dir = value;
        } else if (key == "log_level") {
            cfg->log_level = lower(value);
        } else if (key == "heartbeat_ms") {
            cfg->heartbeat_ms = std::max(200, std::min(10000, std::atoi(value.c_str())));
        }
    }
    return true;
}

bool config_save(const std::wstring& path, const Config& cfg, std::string* err) {
    std::ofstream out(std::filesystem::path(path), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        if (err) {
            *err = "cannot write config file";
        }
        return false;
    }
    out << "# LanKVM configuration\n";
    out << "role=" << cfg.role << "\n";
    out << "peer_ip=" << cfg.peer_ip << "\n";
    out << "port=" << cfg.port << "\n";
    out << "psk=" << cfg.psk_hex << "\n";
    out << "edge=" << cfg.edge << "\n";
    out << "clipboard=" << (cfg.clipboard ? 1 : 0) << "\n";
    out << "encrypt_clipboard=" << (cfg.encrypt_clipboard ? 1 : 0) << "\n";
    out << "dropzone=" << (cfg.dropzone ? 1 : 0) << "\n";
    out << "auto_connect=" << (cfg.auto_connect ? 1 : 0) << "\n";
    out << "download_dir=" << cfg.download_dir << "\n";
    out << "log_level=" << cfg.log_level << "\n";
    out << "heartbeat_ms=" << cfg.heartbeat_ms << "\n";
    out.flush();
    return true;
}

bool config_ensure_psk(Config* cfg) {
    std::vector<uint8_t> bytes;
    if (from_hex(cfg->psk_hex, &bytes) && bytes.size() == 32) {
        return true;
    }
    uint8_t key[32]{};
    random_bytes(key, sizeof(key));
    cfg->psk_hex = to_hex(key, sizeof(key));
    return true;
}

std::string config_download_dir(const Config& cfg) {
    if (!cfg.download_dir.empty()) {
        return cfg.download_dir;
    }
    PWSTR known = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE, nullptr, &known)) && known) {
        dir = known;
        CoTaskMemFree(known);
    } else {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
            dir = std::filesystem::path(profile) / L"Downloads";
        } else {
            dir = std::filesystem::temp_directory_path();
        }
    }
    dir /= L"LanKVM";
    return dir.string();
}

}  // namespace lk
