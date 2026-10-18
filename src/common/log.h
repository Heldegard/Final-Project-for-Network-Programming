#pragma once

#include <cstdarg>
#include <string>

namespace lk {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

bool log_open(const std::wstring& path, LogLevel level);
void log_close();
void log_write(LogLevel level, const char* fmt, ...);
bool log_parse_level(const std::string& s, LogLevel* out);
const char* log_level_name(LogLevel level);

}  // namespace lk

#define LK_DEBUG(...) ::lk::log_write(::lk::LogLevel::Debug, __VA_ARGS__)
#define LK_INFO(...) ::lk::log_write(::lk::LogLevel::Info, __VA_ARGS__)
#define LK_WARN(...) ::lk::log_write(::lk::LogLevel::Warn, __VA_ARGS__)
#define LK_ERROR(...) ::lk::log_write(::lk::LogLevel::Error, __VA_ARGS__)
