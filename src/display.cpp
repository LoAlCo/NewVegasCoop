// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "display.h"

#include "log.h"
#include "window_spot.h"

#include <cstring>
#include <vector>
#include <windows.h>

namespace display
{
    namespace
    {
        decltype(&GetPrivateProfileStringA) g_getString = nullptr;
        decltype(&GetPrivateProfileIntA) g_getInt = nullptr;
        decltype(&WritePrivateProfileStringA) g_writeString = nullptr;
        bool g_wantedFullScreen = false;
        bool g_done = false;

        bool IsFullScreenKey(LPCSTR section, LPCSTR key)
        {
            return section && key && _stricmp(section, "Display") == 0 && _stricmp(key, "bFull Screen") == 0;
        }

        bool InFile(LPCSTR section, LPCSTR key, LPCSTR file)
        {
            char probe[24];
            g_getString(section, key, "<nvcoop: not here>", probe, sizeof(probe), file);
            return std::strcmp(probe, "<nvcoop: not here>") != 0;
        }

        DWORD WINAPI GetString(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size, LPCSTR file)
        {
            DWORD n = g_getString(section, key, fallback, out, size, file);
            if (IsFullScreenKey(section, key) && out && size >= 2)
            {
                if (InFile(section, key, file))
                    g_wantedFullScreen = out[0] != '0';
                LOG_DEBUG("Display: %s says bFull Screen %s", file ? file : "?", n ? out : "(none)");
                out[0] = '0';
                out[1] = 0;
                return 1;
            }
            return n;
        }

        UINT WINAPI GetInt(LPCSTR section, LPCSTR key, INT fallback, LPCSTR file)
        {
            UINT value = g_getInt(section, key, fallback, file);
            if (!IsFullScreenKey(section, key))
                return value;
            if (InFile(section, key, file))
                g_wantedFullScreen = value != 0;
            LOG_DEBUG("Display: %s says bFull Screen %u", file ? file : "?", value);
            return 0;
        }

        BOOL WINAPI WriteString(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file)
        {
            if (IsFullScreenKey(section, key) && value)
                value = g_wantedFullScreen ? "1" : "0";
            return g_writeString(section, key, value, file);
        }

        void* PatchImport(const char* dll, const char* function, void* replacement)
        {
            auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA(dll), function));
            if (!dir.VirtualAddress || !target)
                return nullptr;
            for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp)
            {
                if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dll) != 0)
                    continue;
                for (auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk); thunk->u1.Function; ++thunk)
                {
                    if (reinterpret_cast<void*>(thunk->u1.Function) != target)
                        continue;
                    DWORD old;
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                    thunk->u1.Function = reinterpret_cast<uintptr_t>(replacement);
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                    return target;
                }
            }
            return nullptr;
        }

        BOOL CALLBACK FindWindowOfOurs(HWND window, LPARAM found)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
                return TRUE;
            char type[32] = {};
            GetClassNameA(window, type, sizeof(type));
            if (window == GetConsoleWindow() || _stricmp(type, "ConsoleWindowClass") == 0)
                return TRUE;
            *reinterpret_cast<HWND*>(found) = window;
            return FALSE;
        }

        Box ToBox(const RECT& r)
        {
            return Box{r.left, r.top, r.right, r.bottom};
        }

        BOOL CALLBACK AddScreen(HMONITOR monitor, HDC, LPRECT, LPARAM list)
        {
            MONITORINFO info{sizeof(info)};
            if (!GetMonitorInfoA(monitor, &info))
                return TRUE;
            auto& screens = *reinterpret_cast<std::vector<Box>*>(list);
            if (info.dwFlags & MONITORINFOF_PRIMARY)
                screens.insert(screens.begin(), ToBox(info.rcWork));
            else
                screens.push_back(ToBox(info.rcWork));
            return TRUE;
        }

        struct OtherCopies
        {
            char type[64];
            std::vector<Box> windows;
        };

        BOOL CALLBACK AddOtherCopy(HWND window, LPARAM list)
        {
            auto& copies = *reinterpret_cast<OtherCopies*>(list);
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process == GetCurrentProcessId() || !IsWindowVisible(window))
                return TRUE;
            char type[64] = {};
            GetClassNameA(window, type, sizeof(type));
            RECT r;
            if (std::strcmp(type, copies.type) == 0 && GetWindowRect(window, &r))
                copies.windows.push_back(ToBox(r));
            return TRUE;
        }

        void PlaceWindowed(HWND window)
        {
            RECT r;
            if (!GetWindowRect(window, &r))
                return LOG_WARN("Display: couldn't read the window's position; it stays as it is");
            std::vector<Box> screens;
            EnumDisplayMonitors(nullptr, nullptr, AddScreen, reinterpret_cast<LPARAM>(&screens));
            OtherCopies copies{};
            GetClassNameA(window, copies.type, sizeof(copies.type));
            EnumWindows(AddOtherCopy, reinterpret_cast<LPARAM>(&copies));
            const Box now = ToBox(r);
            const Box spot = PickWindowSpot(now, screens, copies.windows);
            if (spot.left == now.left && spot.top == now.top)
                return LOG_INFO("Display: windowed, as chosen, at %d, %d", now.left, now.top);
            SetWindowPos(window, nullptr, spot.left, spot.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            LOG_INFO("Display: windowed, as chosen, moved from %d, %d to %d, %d (%zu other copies running)", now.left,
                now.top, spot.left, spot.top, copies.windows.size());
        }
    }

    void Install()
    {
        g_getString = reinterpret_cast<decltype(g_getString)>(
            PatchImport("KERNEL32.dll", "GetPrivateProfileStringA", reinterpret_cast<void*>(GetString)));
        g_getInt = reinterpret_cast<decltype(g_getInt)>(
            PatchImport("KERNEL32.dll", "GetPrivateProfileIntA", reinterpret_cast<void*>(GetInt)));
        g_writeString = reinterpret_cast<decltype(g_writeString)>(
            PatchImport("KERNEL32.dll", "WritePrivateProfileStringA", reinterpret_cast<void*>(WriteString)));
        if (!g_getString || !g_getInt || !g_writeString)
        {
            if (g_getString)
                PatchImport("KERNEL32.dll", "GetPrivateProfileStringA", reinterpret_cast<void*>(g_getString));
            if (g_getInt)
                PatchImport("KERNEL32.dll", "GetPrivateProfileIntA", reinterpret_cast<void*>(g_getInt));
            if (g_writeString)
                PatchImport("KERNEL32.dll", "WritePrivateProfileStringA", reinterpret_cast<void*>(g_writeString));
            LOG_WARN("Display: couldn't take over the game's INI reading (another mod has?); full screen left as is");
            g_done = true;
            return;
        }
        LOG_INFO("Display: a borderless window instead of full screen (Borderless=0 in NVCoop.ini: as the game has it)");
    }

    void Update()
    {
        if (g_done)
            return;
        HWND window = nullptr;
        EnumWindows(FindWindowOfOurs, reinterpret_cast<LPARAM>(&window));
        if (!window)
            return;
        g_done = true;
        if (!g_wantedFullScreen)
            return PlaceWindowed(window);
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor))
            return LOG_WARN("Display: couldn't read the screen's size; the window stays as it is");
        const RECT& r = monitor.rcMonitor;
        SetWindowLongPtrA(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        LOG_INFO("Display: borderless over the whole screen (%ld x %ld): alt-tab is safe", r.right - r.left,
            r.bottom - r.top);
    }
}
