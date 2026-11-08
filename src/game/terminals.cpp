// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/terminals.h"

#include "coop/clock.h"
#include "coop/quests.h"
#include "game/addresses.h"
#include "game/game.h"
#include "game/progress.h"
#include "log.h"

#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"

#include <cstring>
#include <map>
#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kHoldRewardsMs = 3000;

        Terminals* g_terminals = nullptr;
        bool g_replaying = false;
        using RunEntryFn = bool(__thiscall*)(void* entry, TESObjectREFR* ref);
        RunEntryFn g_original[2] = {};
        RunEntryFn g_run = nullptr;

        template <typename T> T At(const void* p, uint32_t offset)
        {
            return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(p) + offset);
        }

        struct ListNode { void* data; ListNode* next; };

        std::vector<void*> Entries(const TESForm* terminal)
        {
            std::vector<void*> out;
            if (!terminal || terminal->typeID != kFormType_BGSTerminal)
                return out;
            auto* node = reinterpret_cast<const ListNode*>(reinterpret_cast<const uint8_t*>(terminal) +
                layout::kTerminalEntries);
            for (; node && out.size() < net::kMaxTerminalEntries; node = node->next)
                if (node->data)
                    out.push_back(node->data);
            return out;
        }

        std::string EntryText(const void* entry)
        {
            const char* text = entry ? At<const char*>(entry, layout::kTerminalEntryText) : nullptr;
            uint16_t length = entry ? At<uint16_t>(entry, layout::kTerminalEntryText + 4) : 0;
            return text && length < 200 ? std::string(text, length) : std::string();
        }

        bool __fastcall RunEntryHook(void* entry, void*, TESObjectREFR* ref)
        {
            if (!g_terminals || g_replaying)
                return g_run(entry, ref);
            return g_terminals->Chosen(entry, ref);
        }

        bool Redirect(uintptr_t at, RunEntryFn& original)
        {
            auto* p = reinterpret_cast<uint8_t*>(at);
            if (p[0] != 0xE8)
            {
                LOG_ERROR("Terminals: the terminal screen's call at %08X isn't as expected (%02X); terminals won't be "
                    "shared", static_cast<uint32_t>(at), p[0]);
                return false;
            }
            int32_t rel;
            std::memcpy(&rel, p + 1, 4);
            original = reinterpret_cast<RunEntryFn>(at + 5 + rel);
            DWORD old;
            if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
                return false;
            int32_t ours = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&RunEntryHook) - (at + 5));
            std::memcpy(p + 1, &ours, 4);
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            if (reinterpret_cast<uintptr_t>(original) != addr::kTerminalRunEntry)
                LOG_INFO("Terminals: another plugin hooks terminal entries at %08X too; ours goes first and passes on",
                    static_cast<uint32_t>(at));
            return true;
        }
    }

    void Terminals::Install(coop::Coop& coop, Progress& progress)
    {
        m_coop = &coop;
        m_progress = &progress;
        if (g_terminals)
            return;
        if (!Redirect(addr::kTerminalRunEntryCalls[0], g_original[0]))
            return;
        if (!Redirect(addr::kTerminalRunEntryCalls[1], g_original[1]))
        {
            LOG_WARN("Terminals: only one of the terminal screen's two calls hooked (entries with submenus not shared)");
        }
        g_run = g_original[0];
        g_terminals = this;
        LOG_INFO("Terminals: watching terminal entries");
    }

    bool Terminals::Ours(uint32_t id)
    {
        if (m_loadOrder.empty())
            m_loadOrder = LoadOrder();
        return coop::HostRunsQuestsOf(id, m_loadOrder, m_coop->QuestMods());
    }

    bool Terminals::Chosen(void* entry, TESObjectREFR* ref)
    {
        if (!m_coop || !m_coop->InGame())
            return g_run(entry, ref);
        const void* menu = *reinterpret_cast<void* const*>(addr::kComputersMenu);
        auto* terminal = menu ? At<TESForm*>(menu, layout::kMenuTerminal) : nullptr;
        std::vector<void*> entries = Entries(terminal);
        int index = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i] == entry)
                index = static_cast<int>(i);
        DialogueLineRunning(true);
        bool result = g_run(entry, ref);
        DialogueLineRunning(false);
        if (index < 0 || !ref || !Ours(ref->refID) || !Ours(terminal->refID))
        {
            LOG_WARN("Terminals: an entry (\"%s\") of %08X not found or not the host's to share; ran here only",
                EntryText(entry).c_str(), terminal ? terminal->refID : 0);
            return result;
        }
        ++m_chosen;
        m_mine.push_back({0, ref->refID, terminal->refID, static_cast<uint8_t>(index)});
        LOG_INFO("Terminals: we chose \"%s\" on %08X (terminal %08X, entry %d)", EntryText(entry).c_str(), ref->refID,
            terminal->refID, index);
        return result;
    }

    std::string Terminals::LabPick(int index)
    {
        const void* menu = *reinterpret_cast<void* const*>(addr::kComputersMenu);
        if (!menu || !g_terminals)
            return "no terminal screen open";
        auto* terminal = At<TESForm*>(menu, layout::kMenuTerminal);
        auto* ref = At<TESObjectREFR*>(menu, layout::kMenuTerminalRef);
        std::vector<void*> entries = Entries(terminal);
        if (!ref || index < 0 || index >= static_cast<int>(entries.size()))
            return "no such entry (" + std::to_string(entries.size()) + " here)";
        RunEntryHook(entries[index], nullptr, ref);
        return "ran \"" + EntryText(entries[index]) + "\"";
    }

    void Terminals::Replay(const net::TerminalUse& use)
    {
        bool host = m_coop->GetRole() == coop::Role::Host;
        std::string who = m_coop->NameOf(use.player);
        TESObjectREFR* ref = Ours(use.ref) ? LookupRef(use.ref) : nullptr;
        TESForm* terminal = Ours(use.terminal) ? LookupForm(use.terminal) : nullptr;
        std::vector<void*> entries = Entries(terminal);
        bool ok = ref && ref->baseForm && ref->baseForm->typeID == kFormType_BGSTerminal && use.entry < entries.size();
        if (ok && host)
        {
            coop::Pose them, it;
            ok = m_coop->PlayerPose(use.player, them) && GetPose(ref, it) && coop::Within(them, it, LoadedReach());
        }
        if (!ok)
        {
            ++m_refused;
            LOG_WARN("Terminals: %s's entry %u of %08X on %08X: not a terminal entry %shere; not run", who.c_str(),
                use.entry, use.terminal, use.ref, host ? "near them " : "");
            return;
        }
        void* entry = entries[use.entry];
        std::vector<Item> before, after;
        bool counted = Contents(Player(), before);
        int32_t xp = PlayerXp(), karma = PlayerKarma();
        HoldRewardsFrom(ref, kHoldRewardsMs);
        g_replaying = true;
        DialogueLineRunning(true);
        HoldPlayerEffects(true);
        g_run(entry, ref);
        HoldPlayerEffects(false);
        DialogueLineRunning(false);
        g_replaying = false;
        ++m_replayed;
        int putBack = 0;
        if (counted && Contents(Player(), after))
        {
            std::map<uint32_t, int32_t> change;
            for (const Item& i : after)
                change[i.id] += i.count;
            for (const Item& i : before)
                change[i.id] -= i.count;
            for (auto& [id, delta] : change)
                if (delta)
                {
                    ChangeItem(Player(), id, -delta);
                    ++putBack;
                    LOG_INFO("Terminals: %s's entry changed our player's %08X by %d; put back", who.c_str(), id, delta);
                }
        }
        GiveBackAfterReplay();
        int32_t gainedXp = PlayerXp() - xp, gainedKarma = PlayerKarma() - karma;
        if ((gainedXp || gainedKarma) && m_progress)
        {
            m_progress->Expect(gainedXp, gainedKarma);
            LOG_WARN("Terminals: %s's entry still gave us %d XP, %d karma (kept, not shared)", who.c_str(), gainedXp,
                gainedKarma);
        }
        LOG_INFO("Terminals: ran %s's \"%s\" on %08X (terminal %08X, entry %u) for our player; %d item kinds put back",
            who.c_str(), EntryText(entry).c_str(), use.ref, use.terminal, use.entry, putBack);
        if (host)
            m_coop->SendTerminalUse(use);
    }

    void Terminals::Update(uint32_t now)
    {
        if (!m_coop || !g_terminals)
            return;
        if (!m_coop->InGame())
        {
            m_mine.clear();
            return;
        }
        for (const net::TerminalUse& use : m_mine)
            m_coop->SendTerminalUse(use);
        m_mine.clear();
        for (const net::TerminalUse& use : m_coop->TakeTerminalUses())
            Replay(use);
        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_chosen || m_replayed || m_refused))
                LOG_INFO("Terminals: %u entries chosen here, %u run for others, %u refused", m_chosen, m_replayed,
                    m_refused);
            m_nextLog = now + 60000;
            m_chosen = m_replayed = m_refused = 0;
        }
    }
}
