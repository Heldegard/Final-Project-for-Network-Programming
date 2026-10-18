#include "common/log.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace lk {
namespace {

std::mutex g_mutex;
std::ofstream g_file;
LogLevel g_min_level = LogLevel::Info;

std::string now_string() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

}  // namespace

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

bool log_parse_level(const std::string& s, LogLevel* out) {
    if (s == "debug") { *out = LogLevel::Debug; return true; }
    if (s == "info") { *out = LogLevel::Info; return true; }
    if (s == "warn" || s == "warning") { *out = LogLevel::Warn; return true; }
    if (s == "error") { *out = LogLevel::Error; return true; }
    return false;
}

bool log_open(const std::wstring& path, LogLevel level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_file.open(std::filesystem::path(path), std::ios::out | std::ios::app);
    g_min_level = level;
    if (!g_file.is_open()) {
        return false;
    }
    g_file << "\n===== LanKVM session started " << now_string() << " =====\n";
    g_file.flush();
    return true;
}

void log_close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
}

void log_write(LogLevel level, const char* fmt, ...) {
    if (level < g_min_level) {
        return;
    }
    char message[2048]{};
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    std::string line = now_string();
    line += " [";
    line += log_level_name(level);
    line += "] ";
    line += message;
    line += "\n";

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file << line;
        g_file.flush();
    }
    OutputDebugStringA(line.c_str());
}

}  // namespace lk
