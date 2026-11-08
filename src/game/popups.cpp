// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "coop/gear_lockers.h"
#include "coop/activator_uses.h"
#include "coop/chat.h"
#include "coop/quests.h"
#include "coop/talk_log.h"
#include "coop/vats_feel.h"
#include "coop/balance.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <utility>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <deque>
#include <functional>
#include <intrin.h>
#include "coop/attack_versions.h"
#include "coop/clock.h"
#include "coop/shot_counter.h"
#include "game/addresses.h"
#include "game/mover.h"
#include "log.h"
#include "nvse/PluginAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameProcess.h"
#include <map>
#include <set>
#include <string>
#include "game/internal.h"

namespace game
{
    namespace
    {
        constexpr int kQuestions = 16;
        struct Question
        {
            bool open = false;
            volatile int button = -1;
            uint32_t askedAt = 0;
            char text[1024];
            std::string buttons[10];
        };
        Question g_questions[kQuestions];

        template <int N>
        void QuestionClosed()
        {
            g_questions[N].button = reinterpret_cast<uint8_t(__cdecl*)()>(addr::kTakeMessageBoxButton)();
        }
        template <int... N>
        constexpr std::array<void (*)(), sizeof...(N)> Closers(std::integer_sequence<int, N...>)
        {
            return {&QuestionClosed<N>...};
        }
        const std::array<void (*)(), kQuestions> kClosers = Closers(std::make_integer_sequence<int, kQuestions>());

        char g_message[1024];
        void MessageClosed() { reinterpret_cast<uint8_t(__cdecl*)()>(addr::kTakeMessageBoxButton)(); }

        using ShowFn = bool(__cdecl*)(const char*, uint32_t, uint32_t, void (*)(), uint32_t, uint32_t, float, float,
            ...);

        bool Show(int id)
        {
            Question& q = g_questions[id];
            const char* b[11] = {};
            for (size_t i = 0; i < 10 && !q.buttons[i].empty(); ++i)
                b[i] = q.buttons[i].c_str();
            return reinterpret_cast<ShowFn>(addr::kShowMessageBox)(q.text, 0, 0, kClosers[id], 0, 0x17, 0.f, 0.f,
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], nullptr);
        }

        bool ShowPlain(const std::string& text)
        {
            std::snprintf(g_message, sizeof(g_message), "%s", text.c_str());
            return reinterpret_cast<ShowFn>(addr::kShowMessageBox)(g_message, 0, 0, &MessageClosed, 0, 0x17, 0.f, 0.f,
                "OK", nullptr);
        }

        struct Waiting
        {
            int id;
            std::string text;
            uint32_t refusedSince = 0;
        };
        std::deque<Waiting> g_waiting;
        constexpr uint32_t kGiveUpMs = 10000;

        bool BoxFree()
        {
            auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
            return manager && *manager && !*reinterpret_cast<void* const*>(addr::kMessageMenu) &&
                !*reinterpret_cast<const uint8_t*>(addr::kMessageBoxBusy);
        }

        bool Ask(int id, const std::string& text, const std::vector<std::string>& buttons)
        {
            if (id < 0 || id >= kQuestions)
                return false;
            Question& q = g_questions[id];
            std::snprintf(q.text, sizeof(q.text), "%s", text.c_str());
            for (size_t i = 0; i < 10; ++i)
                q.buttons[i] = i < buttons.size() ? buttons[i] : std::string();
            q.button = -1;
            q.askedAt = GetTickCount();
            q.open = true;
            bool queued = false;
            for (const Waiting& w : g_waiting)
                queued |= w.id == id;
            if (!queued)
                g_waiting.push_back({id, ""});
            RetryMessageBoxes();
            return true;
        }
    }

    void RetryMessageBoxes()
    {
        if (g_waiting.empty() || !BoxFree())
            return;
        Waiting& w = g_waiting.front();
        bool shown = w.id >= 0 ? Show(w.id) : ShowPlain(w.text);
        if (shown)
        {
            g_waiting.pop_front();
            return;
        }
        uint32_t now = GetTickCount();
        if (!w.refusedSince)
            w.refusedSince = now;
        else if (now - w.refusedSince > kGiveUpMs)
        {
            LOG_WARN("A box (question %d) the game wouldn't show for %u s: given up", w.id, kGiveUpMs / 1000);
            if (w.id >= 0)
                g_questions[w.id].button = kNeverShown;
            g_waiting.pop_front();
        }
    }

    bool AskYesNo(const std::string& text, int id) { return Ask(id, text, {"Yes", "No"}); }

    void ShowMessage(const std::string& text)
    {
        g_waiting.push_back({-1, text});
        RetryMessageBoxes();
    }

    bool AskChoice(const std::string& text, const std::vector<std::string>& buttons, int id)
    {
        return Ask(id, text, buttons);
    }

    bool QuestionOpen()
    {
        for (const Question& q : g_questions)
            if (q.open && GetTickCount() - q.askedAt < 120000)
                return true;
        return false;
    }

    namespace
    {
        void SetTileText(void* tile, const std::string& text)
        {
            using SetString = void(__thiscall*)(void*, uint32_t, const char*, bool);
            reinterpret_cast<SetString>(addr::kTileSetString)(tile, addr::kTileValueString, text.c_str(), true);
        }
    }

    std::string MessageText()
    {
        auto* menu = *reinterpret_cast<uint8_t* const*>(addr::kMessageMenu);
        void* tile = menu ? *reinterpret_cast<void* const*>(menu + addr::kMessageMenuText) : nullptr;
        if (!tile)
            return "";
        using GetValue = const uint8_t*(__thiscall*)(void*, uint32_t);
        const uint8_t* value = reinterpret_cast<GetValue>(addr::kTileGetValue)(tile, addr::kTileValueString);
        const char* now = value ? *reinterpret_cast<const char* const*>(value + 0xC) : nullptr;
        return now ? now : "";
    }

    bool MessageShows(const std::string& text) { return !text.empty() && MessageText() == text; }

    bool SetMessageText(const std::string& text)
    {
        auto* menu = *reinterpret_cast<uint8_t* const*>(addr::kMessageMenu);
        void* tile = menu ? *reinterpret_cast<void* const*>(menu + addr::kMessageMenuText) : nullptr;
        if (!tile)
            return false;
        SetTileText(tile, text);
        return true;
    }

    bool SetMessageButton(int index, const std::string& label)
    {
        auto* menu = *reinterpret_cast<uint8_t* const*>(addr::kMessageMenu);
        if (!menu)
            return false;
        auto* node = menu + addr::kMessageMenuButtons + 4;
        for (int i = 0; node && i < index; ++i)
            node = *reinterpret_cast<uint8_t* const*>(node + 4);
        auto* item = node ? *reinterpret_cast<uint8_t* const*>(node) : nullptr;
        void* tile = item ? *reinterpret_cast<void* const*>(item) : nullptr;
        if (!tile)
            return false;
        SetTileText(tile, label);
        return true;
    }

    int TakeChoice(int id)
    {
        if (id < 0 || id >= kQuestions || !g_questions[id].open || g_questions[id].button == -1)
            return -1;
        int choice = g_questions[id].button;
        g_questions[id].open = false;
        g_questions[id].button = -1;
        LOG_DEBUG("Question %d answered with button %d", id, choice);
        return choice;
    }

    void DropQuestion(int id)
    {
        if (id < 0 || id >= kQuestions || !g_questions[id].open)
            return;
        Question& q = g_questions[id];
        q.open = false;
        q.button = -1;
        for (auto it = g_waiting.begin(); it != g_waiting.end(); ++it)
            if (it->id == id)
            {
                g_waiting.erase(it);
                return;
            }
        if (MessageShows(q.text))
            CloseMenu();
    }

    void OpenLoadMenu()
    {
        reinterpret_cast<void(__cdecl*)()>(addr::kLoadOption)();
    }

    bool RunInBackground(bool on)
    {
        auto* value = reinterpret_cast<uint8_t*>(addr::kAlwaysActiveSetting + 4);
        bool was = *value != 0;
        *value = on ? 1 : 0;
        return was;
    }

    void QuitGame()
    {
        reinterpret_cast<void(__cdecl*)()>(addr::kExitGameOption)();
    }

    int TakeAnswer(int id)
    {
        int choice = TakeChoice(id);
        return choice == kNeverShown ? 0 : choice < 0 ? -1 : choice == 0 ? 1 : 0;
    }
}
