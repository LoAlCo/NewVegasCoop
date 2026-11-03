// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "crash.h"

#include "game/game.h"
#include "log.h"

#include <atomic>
#include <crtdbg.h>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <exception>
#include <windows.h>
#include <dbghelp.h>

namespace
{
    LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER g_first = nullptr;
    LONG g_dumped = 0;

    void* g_spare = nullptr;
    HANDLE g_dumpWanted = nullptr, g_dumpDone = nullptr;
    HANDLE g_dumpWriter = nullptr;
    EXCEPTION_POINTERS* g_dumpInfo = nullptr;
    DWORD g_dumpThread = 0;
    char g_dumpPath[MAX_PATH];

    void Dump(EXCEPTION_POINTERS* info, DWORD thread)
    {
        HANDLE file = CreateFileA(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return LOG_ERROR("Couldn't make the crash dump file %s (error %lu)", g_dumpPath, GetLastError());
        MINIDUMP_EXCEPTION_INFORMATION mei{thread, info, FALSE};
        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo),
            info ? &mei : nullptr, nullptr, nullptr);
        DWORD error = ok ? 0 : GetLastError();
        CloseHandle(file);
        if (ok)
            LOG_ERROR("Crash dump written: %s (send it with this log)", g_dumpPath);
        else
            LOG_ERROR("Couldn't write the crash dump %s (error %08lX); the stack above is in the log", g_dumpPath,
                error);
    }

    DWORD WINAPI DumpThread(void*)
    {
        WaitForSingleObject(g_dumpWanted, INFINITE);
        Dump(g_dumpInfo, g_dumpThread);
        SetEvent(g_dumpDone);
        return 0;
    }

    void WriteDump(EXCEPTION_POINTERS* info, bool fatal = true)
    {
        static LONG s_errorDumped = 0;
        if (InterlockedExchange(fatal ? &g_dumped : &s_errorDumped, 1) != 0)
            return;
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::snprintf(g_dumpPath, sizeof(g_dumpPath), "NVCoop-crash-%04d%02d%02d-%02d%02d%02d.dmp",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        if (!fatal)
            return Dump(info, GetCurrentThreadId());
        if (g_spare)
        {
            VirtualFree(g_spare, 0, MEM_RELEASE);
            g_spare = nullptr;
        }
        if (g_dumpWanted && g_dumpDone)
        {
            if (WaitForSingleObject(g_dumpWriter, 0) == WAIT_OBJECT_0)
                return LOG_ERROR("No crash dump: the game was closing (the stack above says where)");
            g_dumpInfo = info;
            g_dumpThread = GetCurrentThreadId();
            SetEvent(g_dumpWanted);
            if (WaitForSingleObject(g_dumpDone, 60000) == WAIT_OBJECT_0)
                return;
            LOG_ERROR("The crash dump took over a minute; going on without it");
            return;
        }
        Dump(info, GetCurrentThreadId());
    }

    void DumpHere(bool fatal)
    {
        __try
        {
            RaiseException(0xE0C00000, 0, 0, nullptr);
        }
        __except (WriteDump(GetExceptionInformation(), fatal), EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool IsCode(uintptr_t v)
    {
        MEMORY_BASIC_INFORMATION m;
        return v > 0x10000 && VirtualQuery(reinterpret_cast<void*>(v), &m, sizeof(m)) && m.State == MEM_COMMIT &&
            (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }

    int ScanStack(uintptr_t esp, uintptr_t* out, int max)
    {
        int n = 0;
        __try
        {
            for (uintptr_t p = esp; p < esp + 0x4000 && n < max; p += 4)
            {
                uintptr_t v = *reinterpret_cast<const uintptr_t*>(p);
                if (IsCode(v))
                    out[n++] = v;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return n;
    }

    void Describe(uintptr_t address, char* out, size_t size)
    {
        HMODULE module = nullptr;
        char path[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address), &module))
            GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = std::strrchr(path, '\\');
        std::snprintf(out, size, "%s+%lX", name ? name + 1 : path,
            static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(module)));
    }

    void LogStack(const CONTEXT* c)
    {
        LOG_ERROR("Registers: eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX", c->Eax,
            c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        uintptr_t found[32];
        int n = ScanStack(c->Esp, found, 32);
        std::string line;
        char one[96];
        for (int i = 0; i < n; ++i)
        {
            Describe(found[i], one, sizeof(one));
            line += (line.empty() ? "" : ", ") + std::string(one);
            if ((i + 1) % 8 == 0 || i + 1 == n)
            {
                LOG_ERROR("Stack: %s", line.c_str());
                line.clear();
            }
        }
    }

    LONG WINAPI OnCrash(EXCEPTION_POINTERS* info)
    {
        static LONG depth = 0;
        struct Leave { ~Leave() { InterlockedDecrement(&depth); } } leave;
        LONG in = InterlockedIncrement(&depth);
        if (in == 2)
        {
            LOG_ERROR("Crash: the crash reporter after ours passed it back to ours; it goes to the one before ours");
            return g_first && g_first != g_previous ? g_first(info) : EXCEPTION_CONTINUE_SEARCH;
        }
        if (in > 2)
            return EXCEPTION_CONTINUE_SEARCH;
        static LONG once = 0;
        if (InterlockedExchange(&once, 1) == 0)
        {
            void* address = info->ExceptionRecord->ExceptionAddress;
            HMODULE module = nullptr;
            char moduleName[MAX_PATH] = "unknown";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    static_cast<LPCSTR>(address), &module))
                GetModuleFileNameA(module, moduleName, MAX_PATH);
            auto offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module);
            LOG_ERROR("CRASH: code %08lX at %p (%s + %lX)", info->ExceptionRecord->ExceptionCode, address,
                moduleName, static_cast<unsigned long>(offset));
            const EXCEPTION_RECORD* e = info->ExceptionRecord;
            if (e->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && e->NumberParameters >= 2)
                LOG_ERROR("It %s memory at %08lX", e->ExceptionInformation[0] == 0 ? "read" :
                    e->ExceptionInformation[0] == 1 ? "wrote" : "ran", static_cast<unsigned long>(e->ExceptionInformation[1]));
            LogStack(info->ContextRecord);
            AddressSpace a = GetAddressSpace();
            LOG_ERROR("Address space: %u MB free of %u (4 GB patch: %s)", a.freeMb, a.totalMb, a.large ? "yes" : "NO");
            char menus[96];
            game::OpenMenus(menus, sizeof(menus));
            LOG_ERROR("Menus open: %s", menus[0] ? menus : "none");
            WriteDump(info);
        }
        return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
    }

#ifdef _DEBUG
    int __cdecl OnRuntimeReport(int type, char* message, int* returnValue)
    {
        if (type == _CRT_WARN)
            return FALSE;
        LOG_ERROR("C++ runtime %s: %s", type == _CRT_ASSERT ? "check failed" : "error", message ? message : "");
        static int dumps = 0;
        if (dumps++ < 3)
            DumpHere(false);
        if (returnValue)
            *returnValue = 0;
        return TRUE;
    }
#endif

    void __cdecl OnAbort(int)
    {
        LOG_ERROR("C++ runtime: abort() called");
        DumpHere(true);
    }

    void OnTerminate()
    {
        LOG_ERROR("C++ runtime: an error was never caught (std::terminate)");
        DumpHere(true);
        std::abort();
    }

    void __cdecl OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
        unsigned int line, uintptr_t)
    {
        LOG_ERROR("C++ runtime: invalid parameter (%ls in %ls, %ls:%u)", expression ? expression : L"?",
            function ? function : L"?", file ? file : L"?", line);
        DumpHere(false);
    }
}

AddressSpace GetAddressSpace()
{
    AddressSpace a;
    auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
    a.large = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
    MEMORYSTATUSEX m{sizeof(m)};
    if (GlobalMemoryStatusEx(&m))
    {
        a.freeMb = static_cast<unsigned>(m.ullAvailVirtual >> 20);
        a.totalMb = static_cast<unsigned>(m.ullTotalVirtual >> 20);
    }
    return a;
}

namespace
{
    std::atomic<DWORD> g_lastBeat{0};
    std::atomic<bool> g_watchFrames{false};
    HANDLE g_gameThread = nullptr;

    DWORD WINAPI FreezeWatch(void*)
    {
        int reports = 0;
        bool reported = false;
        for (;;)
        {
            Sleep(1000);
            DWORD since = GetTickCount() - g_lastBeat.load();
            if (since < 8000 || !g_watchFrames.load())
            {
                reported = false;
                continue;
            }
            if (reported || reports >= 5)
                continue;
            reported = true;
            ++reports;
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_FULL;
            uintptr_t found[32];
            int n = 0;
            if (SuspendThread(g_gameThread) == static_cast<DWORD>(-1))
                continue;
            bool got = GetThreadContext(g_gameThread, &c) != 0;
            if (got)
                n = ScanStack(c.Esp, found, 32);
            ResumeThread(g_gameThread);
            if (!got)
                continue;
            char at[96];
            Describe(c.Eip, at, sizeof(at));
            LOG_WARN("Freeze: the game has stopped for %lu s (a loading screen, or stuck); it's at %s",
                static_cast<unsigned long>(since / 1000), at);
            std::string line;
            char one[96];
            for (int i = 0; i < n; ++i)
            {
                Describe(found[i], one, sizeof(one));
                line += (line.empty() ? "" : ", ") + std::string(one);
                if ((i + 1) % 8 == 0 || i + 1 == n)
                {
                    LOG_WARN("Freeze stack: %s", line.c_str());
                    line.clear();
                }
            }
        }
    }
}

void FreezeBeat(bool watch)
{
    if (!g_gameThread)
    {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_gameThread,
            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        if (g_gameThread)
            if (HANDLE t = CreateThread(nullptr, 64 * 1024, FreezeWatch, nullptr, 0, nullptr))
                CloseHandle(t);
    }
    g_lastBeat.store(GetTickCount());
    g_watchFrames.store(watch);
}

void InstallCrashReporter()
{
    g_spare = VirtualAlloc(nullptr, 32 << 20, MEM_RESERVE, PAGE_NOACCESS);
    g_dumpWanted = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_dumpDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (g_dumpWanted && g_dumpDone)
    {
        if (HANDLE thread = CreateThread(nullptr, 256 * 1024, DumpThread, nullptr, 0, nullptr))
            g_dumpWriter = thread;
        else
            g_dumpWanted = g_dumpDone = nullptr;
    }
    g_previous = g_first = SetUnhandledExceptionFilter(OnCrash);
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, OnRuntimeReport);
#endif
    std::signal(SIGABRT, OnAbort);
    std::set_terminate(OnTerminate);
    _set_invalid_parameter_handler(OnInvalidParameter);
}

void CheckCrashReporter()
{
    LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(OnCrash);
    if (current == OnCrash)
        return;
    char whose[96] = "?";
    if (current)
        Describe(reinterpret_cast<uintptr_t>(current), whose, sizeof(whose));
    LOG_WARN("Something replaced our crash reporter (%s); put it back in front (theirs still runs after ours)",
        whose);
    g_previous = current;
}
