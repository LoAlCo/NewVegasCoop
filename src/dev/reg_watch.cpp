// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "dev/reg_watch.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace game
{
    uintptr_t HookCall(uintptr_t site, const void* to);
}

namespace dev
{
    namespace
    {
        constexpr size_t kThunkSize = 256;
        constexpr size_t kData = 192;
        enum Slot
        {
            kEsi, kEdi, kEbx, kEbp, kRet, kEspBefore, kCalls, kDelta, kBadEsi, kBadEdi, kBadEbx, kBadEbp, kBusy,
            kPassed, kLastBadEsi, kSlots
        };
        static_assert(kData + 4 * kSlots <= kThunkSize, "thunk numbers don't fit");

        struct Watched
        {
            uintptr_t site;
            uintptr_t callee;
            uint8_t* thunk;
        };
        std::mutex g_mutex;
        std::vector<Watched> g_watched;

        class Emitter
        {
        public:
            explicit Emitter(uint8_t* at) : m_start(at), m_at(at) {}
            void Bytes(std::initializer_list<uint8_t> bytes)
            {
                for (uint8_t b : bytes)
                    *m_at++ = b;
            }
            void Word(uint32_t v)
            {
                std::memcpy(m_at, &v, 4);
                m_at += 4;
            }
            void SlotOp(std::initializer_list<uint8_t> op, Slot slot)
            {
                Bytes(op);
                Word(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_start + kData + 4 * slot)));
            }
            void Rel(uint8_t op, uintptr_t to)
            {
                Bytes({op});
                Word(static_cast<uint32_t>(to - (reinterpret_cast<uintptr_t>(m_at) + 4)));
            }
            uint8_t* Here() const { return m_at; }

        private:
            uint8_t* m_start;
            uint8_t* m_at;
        };

        void Build(uint8_t* t, uintptr_t callee)
        {
            Emitter e(t);
            e.SlotOp({0xF0, 0x0F, 0xBA, 0x2D}, kBusy);
            e.Bytes({0x00});
            e.Bytes({0x72, 0x00});
            uint8_t* jc = e.Here() - 1;
            e.SlotOp({0x89, 0x35}, kEsi);
            e.SlotOp({0x89, 0x3D}, kEdi);
            e.SlotOp({0x89, 0x1D}, kEbx);
            e.SlotOp({0x89, 0x2D}, kEbp);
            e.SlotOp({0x8F, 0x05}, kRet);
            e.SlotOp({0x89, 0x25}, kEspBefore);
            e.Rel(0xE8, callee);
            e.SlotOp({0xFF, 0x05}, kCalls);
            e.Bytes({0x50, 0x8B, 0xC4, 0x83, 0xC0, 0x04});
            e.SlotOp({0x2B, 0x05}, kEspBefore);
            e.SlotOp({0xA3}, kDelta);
            e.SlotOp({0x3B, 0x35}, kEsi);
            e.Bytes({0x74, 0x0C});
            e.SlotOp({0xFF, 0x05}, kBadEsi);
            e.SlotOp({0x89, 0x35}, kLastBadEsi);
            const std::pair<uint8_t, Slot> others[] = {{0x3D, kEdi}, {0x1D, kEbx}, {0x2D, kEbp}};
            const Slot bad[] = {kBadEdi, kBadEbx, kBadEbp};
            for (int i = 0; i < 3; ++i)
            {
                e.SlotOp({0x3B, others[i].first}, others[i].second);
                e.Bytes({0x74, 0x06});
                e.SlotOp({0xFF, 0x05}, bad[i]);
            }
            e.Bytes({0x58});
            e.SlotOp({0xFF, 0x35}, kRet);
            e.SlotOp({0xC7, 0x05}, kBusy);
            e.Word(0);
            e.Bytes({0xC3});
            *jc = static_cast<uint8_t>(e.Here() - (jc + 1));
            e.SlotOp({0xF0, 0xFF, 0x05}, kPassed);
            e.Rel(0xE9, callee);
        }

        uint32_t Number(const uint8_t* thunk, Slot slot)
        {
            uint32_t v;
            std::memcpy(&v, thunk + kData + 4 * slot, 4);
            return v;
        }
    }

    bool WatchCallSite(uintptr_t site, std::string& why)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
        uintptr_t start = reinterpret_cast<uintptr_t>(image), end = start + nt->OptionalHeader.SizeOfImage;
        if (site < start || site + 5 > end)
        {
            why = "not in the game's code";
            return false;
        }
        for (const Watched& w : g_watched)
            if (w.site == site)
            {
                why = "already watched";
                return false;
            }
        auto* at = reinterpret_cast<const uint8_t*>(site);
        if (at[0] != 0xE8)
        {
            why = "not a call (E8)";
            return false;
        }
        int32_t rel;
        std::memcpy(&rel, at + 1, 4);
        uintptr_t callee = site + 5 + rel;
        auto* thunk = static_cast<uint8_t*>(VirtualAlloc(nullptr, kThunkSize, MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (!thunk)
        {
            why = "no memory";
            return false;
        }
        std::memset(thunk, 0xCC, kData);
        std::memset(thunk + kData, 0, kThunkSize - kData);
        Build(thunk, callee);
        FlushInstructionCache(GetCurrentProcess(), thunk, kThunkSize);
        if (game::HookCall(site, thunk) != callee)
        {
            VirtualFree(thunk, 0, MEM_RELEASE);
            why = "couldn't redirect the call";
            return false;
        }
        g_watched.push_back({site, callee, thunk});
        return true;
    }

    std::string RegisterWatchReport()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_watched.empty())
            return "no call sites watched";
        std::string out;
        for (const Watched& w : g_watched)
        {
            const uint8_t* t = w.thunk;
            char line[200];
            std::snprintf(line, sizeof(line), "%08X->%08X calls %u unchecked %u popped %d", static_cast<unsigned>(w.site),
                static_cast<unsigned>(w.callee), Number(t, kCalls), Number(t, kPassed),
                static_cast<int>(Number(t, kDelta)));
            out += line;
            if (Number(t, kBadEsi))
            {
                std::snprintf(line, sizeof(line), " ESI changed %u (last to %08X)", Number(t, kBadEsi),
                    Number(t, kLastBadEsi));
                out += line;
            }
            const std::pair<const char*, Slot> others[] = {{"EDI", kBadEdi}, {"EBX", kBadEbx}, {"EBP", kBadEbp}};
            for (const auto& [name, slot] : others)
                if (Number(t, slot))
                {
                    std::snprintf(line, sizeof(line), " %s changed %u", name, Number(t, slot));
                    out += line;
                }
            out += "; ";
        }
        return out;
    }
}
