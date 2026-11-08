// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "log.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace
{
    std::FILE* g_file = nullptr;
    std::mutex g_mutex, g_fileMutex;
    std::condition_variable g_wake;
    std::vector<std::string> g_pending;
    size_t g_dropped = 0;
    bool g_console = false;
    std::atomic<int> g_minLevel{static_cast<int>(LogLevel::Debug)};
    constexpr size_t kMaxPending = 20000;

    const char* LevelName(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        default:              return "ERROR";
        }
    }

    void WriteWaiting()
    {
        std::lock_guard file(g_fileMutex);
        std::vector<std::string> lines;
        size_t dropped = 0;
        {
            std::lock_guard lock(g_mutex);
            lines.swap(g_pending);
            dropped = g_dropped;
            g_dropped = 0;
        }
        if (!g_file || (lines.empty() && !dropped))
            return;
        for (const std::string& line : lines)
            std::fputs(line.c_str(), g_file);
        if (dropped)
            std::fprintf(g_file, "(%zu log lines dropped: the disk was too slow)\n", dropped);
        std::fflush(g_file);
    }

    void Writer()
    {
        for (;;)
        {
            {
                std::unique_lock lock(g_mutex);
                g_wake.wait_for(lock, std::chrono::milliseconds(200));
            }
            WriteWaiting();
        }
    }
}

void Log::Open(const char* folder)
{
    {
        std::lock_guard lock(g_mutex);
        for (int i = 1; !g_file && i <= 4; ++i)
        {
            char path[MAX_PATH], previous[MAX_PATH];
            const char* suffix = i == 1 ? "" : (i == 2 ? "-2" : (i == 3 ? "-3" : "-4"));
            std::snprintf(path, sizeof(path), "%sNVCoop%s.log", folder, suffix);
            std::snprintf(previous, sizeof(previous), "%sNVCoop%s.previous.log", folder, suffix);
            MoveFileExA(path, previous, MOVEFILE_REPLACE_EXISTING);
            g_file = _fsopen(path, "w", _SH_DENYWR);
        }
        if (!g_file)
            return;
    }
    try
    {
        std::thread(Writer).detach();
        std::atexit(Log::Flush);
    }
    catch (...)
    {
    }
}

void Log::EchoToConsole(bool on)
{
    g_console = on;
}

void Log::SetLevel(LogLevel level)
{
    g_minLevel = static_cast<int>(level);
}

void Log::Flush()
{
    WriteWaiting();
}

void Log::Write(LogLevel level, const char* fmt, ...)
{
    if (static_cast<int>(level) < g_minLevel)
        return;
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1100];
    std::snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d %s %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
        LevelName(level), message);

    {
        std::lock_guard lock(g_mutex);
        if (g_console)
        {
            std::printf("%02d:%02d:%02d %s %s\n", t.wHour, t.wMinute, t.wSecond, LevelName(level), message);
            std::fflush(stdout);
        }
        if (!g_file)
            return;
        if (g_pending.size() < kMaxPending)
            g_pending.emplace_back(line);
        else
            ++g_dropped;
    }
    if (level == LogLevel::Error)
        WriteWaiting();
}
