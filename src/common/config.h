#pragma once

#include <cstdint>
#include <string>

namespace lk {

struct Config {
    std::string role = "master";          // "master" or "agent"
    std::string peer_ip = "127.0.0.1";    // master only: where the agent lives
    uint16_t port = 24800;
    std::string psk_hex;                  // 32 bytes, hex encoded
    std::string edge = "right";           // master only: side the agent sits on
    bool clipboard = true;
    bool encrypt_clipboard = true;
    bool dropzone = true;
    bool auto_connect = false;
    std::string download_dir;             // agent only, empty = Downloads\LanKVM
    std::string log_level = "info";
    int heartbeat_ms = 1000;
};

// <exe dir>\lankvm.ini
std::wstring config_default_path();

bool config_load(const std::wstring& path, Config* cfg, std::string* err);
bool config_save(const std::wstring& path, const Config& cfg, std::string* err);

// Fills psk_hex with 32 random bytes (hex) when it is missing or malformed.
bool config_ensure_psk(Config* cfg);

// Resolved download directory for received files.
std::string config_download_dir(const Config& cfg);

}  // namespace lk
