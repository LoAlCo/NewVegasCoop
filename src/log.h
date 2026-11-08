// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

enum class LogLevel { Debug, Info, Warn, Error };

namespace Log
{
    void Open(const char* folder);
    void EchoToConsole(bool on);
    void SetLevel(LogLevel level);
    void Flush();
    void Write(LogLevel level, const char* fmt, ...);
}

#define LOG_DEBUG(...) Log::Write(LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  Log::Write(LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  Log::Write(LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) Log::Write(LogLevel::Error, __VA_ARGS__)
