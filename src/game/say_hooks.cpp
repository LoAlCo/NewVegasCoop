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
        struct Armed
        {
            const TESForm* topic = nullptr;
            const TESForm* info = nullptr;
            const TESObjectREFR* speaker = nullptr;
            int response = 0;
            uint32_t until = 0;
        };
        Armed g_armed;
        constexpr uint32_t kArmedMs = 3000;
        struct Said { const TESObjectREFR* speaker; const TESForm* info; uint32_t until; };
        std::vector<Said> g_said;
        constexpr uint32_t kSaidMs = 120000;
        uintptr_t g_pickLine = 0;

        bool IsArmed(const void* topic, const void* speaker)
        {
            return g_armed.info && topic == g_armed.topic && speaker == g_armed.speaker &&
                static_cast<int32_t>(GetTickCount() - g_armed.until) < 0;
        }

        std::map<uint32_t, RadioLineStart> g_radioNow;
        std::vector<RadioLineStart> g_radioStarts;
        bool g_radioFromHost = false;
        struct HostRadio
        {
            uint32_t info = 0, part = 0, seq = 0;
            std::vector<net::RadioPick> run;
            uint32_t hostStart = 0;
            uint32_t since = 0;
            uint32_t lastPush = 0;
            size_t handing = 0;
            bool handed = false;
            bool runUsed = false;
            int rebuilds = 0;
            bool aligned = false;
        };
        std::map<uint32_t, HostRadio> g_hostRadio;

        struct RadioNode { void* item; RadioNode* next; };
        struct RadioReceiver { TESObjectREFR* radio; Sound sound; Sound staticSound; bool shouldPlay, justStarted,
            inRange, isActor; };
        struct RadioConversation { RadioNode items; RadioNode* current; };
        struct RadioItem { RadioNode responses; RadioNode* current; TESForm* info; TESForm* topic; };
        struct RadioStation
        {
            TESObjectREFR* station;
            RadioConversation* conversation;
            void* nextConversation;
            uint32_t itemStart;
            int32_t itemDuration;
            uint8_t power, targetPower;
            uint8_t pad16[2];
            uint32_t flags;
            RadioNode users;
        };
        static_assert(offsetof(RadioReceiver, staticSound) == 0x10 && offsetof(RadioReceiver, shouldPlay) == 0x1C);
        static_assert(offsetof(RadioItem, info) == 0x0C && offsetof(RadioItem, topic) == 0x10);
        static_assert(offsetof(RadioStation, itemDuration) == 0x10 && offsetof(RadioStation, users) == 0x1C);

        bool ReadStation(const RadioStation* st, RadioLineStart& out)
        {
            const RadioConversation* conv = st->conversation;
            if (!conv || !conv->current || !conv->current->item)
                return false;
            out.station = st->station->refID;
            out.run.clear();
            out.line = 0;
            for (const RadioNode* n = &conv->items; n && out.run.size() < net::kMaxRadioRun; n = n->next)
            {
                auto* item = static_cast<const RadioItem*>(n->item);
                if (!item || !item->info)
                    continue;
                if (n == conv->current)
                    out.line = out.run.size();
                out.run.push_back({item->topic ? item->topic->refID : 0, item->info->refID});
            }
            auto* item = static_cast<const RadioItem*>(conv->current->item);
            if (!item->info)
                return false;
            out.info = item->info->refID;
            out.part = 0;
            if (item->current)
                for (const RadioNode* n = &item->responses; n && n != item->current && out.part < 64; n = n->next)
                    ++out.part;
            return true;
        }

        size_t FindInRun(const std::vector<net::RadioPick>& run, uint32_t info, size_t from = 0)
        {
            for (size_t i = from; i < run.size(); ++i)
                if (run[i].info == info)
                    return i;
            return std::string::npos;
        }

        void* __fastcall PickLineHook(void* topic, void*, void* a, void* b, void* c, uint32_t d, uint32_t e, uint32_t f)
        {
            if (IsArmed(topic, b))
                return const_cast<TESForm*>(g_armed.info);
            using Fn = void*(__thiscall*)(void*, void*, void*, void*, uint32_t, uint32_t, uint32_t);
            if (IsStandIn(static_cast<TESObjectREFR*>(b)))
            {
                static std::atomic<uint32_t> s_dropped{0};
                uint32_t n = ++s_dropped;
                if (n <= 20 || n % 100 == 0)
                    LOG_INFO("Dialogue: no NPC line for another player's character %08X (topic %08X; %u so far)",
                        static_cast<TESObjectREFR*>(b)->refID, topic ? static_cast<TESForm*>(topic)->refID : 0, n);
                return nullptr;
            }
            auto* speaker = static_cast<TESObjectREFR*>(b);
            if (g_radioFromHost && topic && speaker && speaker->baseForm &&
                speaker->baseForm->typeID == kFormType_BGSTalkingActivator)
            {
                auto it = g_hostRadio.find(speaker->refID);
                uint32_t id = static_cast<TESForm*>(topic)->refID;
                if (it != g_hostRadio.end() && !it->second.runUsed)
                {
                    HostRadio& h = it->second;
                    if (h.handed && h.handing < h.run.size() && h.run[h.handing].topic != id)
                    {
                        ++h.handing;
                        h.handed = false;
                    }
                    for (size_t i = h.handing; i < h.run.size(); ++i)
                        if (h.run[i].topic == id)
                            if (void* info = TopicInfo(h.run[i].info))
                            {
                                h.handing = i;
                                h.handed = true;
                                return info;
                            }
                }
            }
            return reinterpret_cast<Fn>(g_pickLine)(topic, a, b, c, d, e, f);
        }

        uint8_t* g_firstResponse = nullptr;
        uint8_t* g_setEndCallback = nullptr;
        std::map<uint32_t, Sound> g_voices;
        std::mutex g_voicesLock;

        void __fastcall SetEndCallbackHook(Sound* sound, void*, uintptr_t function, uint32_t argument)
        {
            if ((function == addr::kSayEnded || function == addr::kSpeechEnded) && sound)
            {
                static std::atomic<int> s_saidThread{0};
                NoteThread("The voice hook", s_saidThread);
                std::lock_guard<std::mutex> lock(g_voicesLock);
                if (g_voices.size() >= 256 && !g_voices.count(argument))
                    g_voices.clear();
                g_voices[argument] = *sound;
            }
            reinterpret_cast<void(__thiscall*)(Sound*, uintptr_t, uint32_t)>(g_setEndCallback)(sound, function, argument);
        }

        bool __fastcall FirstResponseHook(void* item)
        {
            bool any = reinterpret_cast<bool(__thiscall*)(void*)>(g_firstResponse)(item);
            const TESForm* info = item ? At<const TESForm*>(item, layout::kDialogueItemInfo) : nullptr;
            if (!any || !g_armed.info || info != g_armed.info || !g_armed.response ||
                static_cast<int32_t>(GetTickCount() - g_armed.until) >= 0)
                return any;
            using Current = void*(__thiscall*)(void*);
            auto current = reinterpret_cast<Current>(addr::kDialogueItemCurrentResponse);
            auto next = reinterpret_cast<bool(__thiscall*)(void*)>(addr::kDialogueItemNextResponse);
            void* response = current(item);
            for (int i = 0; response && i < 32 &&
                 At<uint32_t>(response, layout::kResponseNumber) != static_cast<uint32_t>(g_armed.response); ++i)
            {
                if (!next(item))
                    return reinterpret_cast<bool(__thiscall*)(void*)>(g_firstResponse)(item);
                response = current(item);
            }
            LOG_DEBUG("Dialogue: %s starts at response %u (wanted %d)", g_armed.speaker ?
                Name(g_armed.speaker) : "?", response ? At<uint32_t>(response, layout::kResponseNumber) : 0,
                g_armed.response);
            return response != nullptr;
        }
    }

    bool InstallSayHooks()
    {
        if (g_pickLine)
            return true;
        uintptr_t pick = HookCall(addr::kPickLineCall, reinterpret_cast<const void*>(&PickLineHook));
        if (!pick)
            return false;
        g_pickLine = pick;
        if (pick != 0x0061A790)
            LOG_INFO("Dialogue: another plugin hooks the game's line picking too (%08X); ours passes on to it",
                static_cast<unsigned>(pick));
        static const uint8_t kStart[7] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC};
        g_firstResponse = HookStart(addr::kDialogueItemFirstResponse, kStart, sizeof(kStart),
            reinterpret_cast<const void*>(&FirstResponseHook));
        static const uint8_t kSoundStart[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C};
        if (!g_mainThread)
            g_mainThread = GetCurrentThreadId();
        g_setEndCallback = HookStart(addr::kSoundHandleSetEndCallback, kSoundStart, sizeof(kSoundStart),
            reinterpret_cast<const void*>(&SetEndCallbackHook));
        return g_firstResponse && g_setEndCallback;
    }

    std::vector<RadioLineStart> TakeRadioLineStarts() { return std::move(g_radioStarts); }

    bool TunePipboyRadio(TESObjectREFR* station)
    {
        if (!station || !station->baseForm || station->baseForm->typeID != kFormType_BGSTalkingActivator)
            return false;
        auto setOn = reinterpret_cast<void(__cdecl*)(bool)>(addr::kRadioSetOn);
        if (*reinterpret_cast<const uint8_t*>(addr::kRadioOn))
            setOn(false);
        setOn(true);
        reinterpret_cast<void(__cdecl*)(TESObjectREFR*, bool)>(addr::kRadioSetStation)(station, true);
        return true;
    }

    void RadioFromHost(bool on)
    {
        g_radioFromHost = on;
        if (!on)
            g_hostRadio.clear();
    }

    void HostRadioLine(const net::RadioLine& line)
    {
        if (g_hostRadio.size() >= 64 && !g_hostRadio.count(line.station))
            return;
        HostRadio& h = g_hostRadio[line.station];
        if (h.seq == line.seq && h.info == line.info)
            return;
        if (h.run.size() != line.run.size() ||
            !std::equal(h.run.begin(), h.run.end(), line.run.begin(),
                [](const net::RadioPick& x, const net::RadioPick& y) { return x.info == y.info && x.topic == y.topic; }))
        {
            h.run = line.run;
            h.handing = 0;
            h.handed = false;
            h.runUsed = false;
            h.rebuilds = 0;
        }
        h.info = line.info;
        h.part = line.part;
        h.seq = line.seq;
        h.hostStart = GetTickCount() - line.elapsed;
        h.aligned = false;
    }

    void UpdateRadios()
    {
        uint32_t now = GetTickCount();
        if (g_radioNow.size() > 128)
            g_radioNow.clear();
        auto seek = reinterpret_cast<void(__thiscall*)(Sound*, uint32_t)>(addr::kSoundHandleSeek);
        int stations = 0;
        for (auto* node = reinterpret_cast<RadioNode*>(addr::kActiveRadioStations); node && stations < 64;
             node = node->next, ++stations)
        {
            auto* st = static_cast<RadioStation*>(node->item);
            RadioLineStart read;
            if (!st || !st->station || !ReadStation(st, read))
                continue;
            RadioLineStart& was = g_radioNow[read.station];
            bool moved = read.info != was.info || read.part != was.part || read.line != was.line;
            if (moved)
            {
                was = read;
                if (g_radioStarts.size() < 64)
                    g_radioStarts.push_back(read);
            }
            if (!g_radioFromHost)
                continue;
            auto it = g_hostRadio.find(read.station);
            if (it == g_hostRadio.end())
                continue;
            HostRadio& h = it->second;
            if (moved)
                h.since = now;
            size_t hostLine = FindInRun(read.run, h.info);
            if (hostLine != std::string::npos && !h.runUsed)
            {
                h.runUsed = true;
                RadioLineStart got = read;
                got.fromHost = true;
                if (g_radioStarts.size() < 64)
                    g_radioStarts.push_back(got);
            }
            bool canPush = st->itemDuration > 0 && now - h.lastPush > 500;
            if (hostLine == std::string::npos)
            {
                if (canPush && h.rebuilds < 2)
                {
                    ++h.rebuilds;
                    h.lastPush = now;
                    h.handing = 0;
                    h.handed = false;
                    h.runUsed = false;
                    RadioNode* last = &st->conversation->items;
                    while (last->next && last->next->item)
                        last = last->next;
                    st->conversation->current = last;
                    st->itemDuration = 1;
                    st->itemStart = 0;
                    LOG_DEBUG("Radio: station %08X builds the host's run (%zu lines, %08X now)", read.station,
                        h.run.size(), h.info);
                }
                continue;
            }
            if (read.line < hostLine || (read.line == hostLine && read.part < h.part))
            {
                if (canPush)
                {
                    h.lastPush = now;
                    if (hostLine > read.line + 1)
                    {
                        size_t index = 0;
                        for (RadioNode* n = &st->conversation->items; n; n = n->next)
                            if (n->item && static_cast<const RadioItem*>(n->item)->info && index++ == hostLine - 1)
                            {
                                st->conversation->current = n;
                                break;
                            }
                    }
                    st->itemDuration = 1;
                    st->itemStart = 0;
                    LOG_DEBUG("Radio: station %08X goes on to the host's %08X part %u (from line %zu part %u)",
                        read.station, h.info, h.part, read.line, read.part);
                }
                continue;
            }
            if (read.line != hostLine || read.part != h.part || h.aligned || now - h.since < 200 ||
                st->itemDuration <= 1)
                continue;
            h.aligned = true;
            uint32_t target = now - h.hostStart;
            if (target < 1000 || target + 1000 >= static_cast<uint32_t>(st->itemDuration) || st->itemStart <= target)
                continue;
            st->itemStart -= target;
            int receivers = 0, sought = 0;
            bool pipboy = *reinterpret_cast<RadioStation**>(addr::kPipboyRadioStation) == st &&
                *reinterpret_cast<const uint8_t*>(addr::kRadioOn);
            if (pipboy)
                reinterpret_cast<void(__cdecl*)(TESObjectREFR*, bool)>(addr::kRadioSetStation)(st->station, true);
            for (RadioNode* u = &st->users; u && receivers < 32; u = u->next, ++receivers)
                if (auto* r = static_cast<RadioReceiver*>(u->item); r && IsPlaying(r->sound))
                {
                    seek(&r->sound, target);
                    ++sought;
                }
            LOG_DEBUG("Radio: station %08X caught up with the host's %08X part %u, %u ms in (Pip-Boy %d, %d radios)",
                read.station, h.info, h.part, target, pipboy, sought);
        }
    }

    bool SaidForOthers(const TESObjectREFR* speaker, const void* info)
    {
        uint32_t now = GetTickCount();
        for (const Said& s : g_said)
            if (s.speaker == speaker && s.info == info && static_cast<int32_t>(now - s.until) < 0)
                return true;
        return false;
    }

    bool SayLine(TESObjectREFR* speaker, uint32_t topicId, uint32_t infoId, int response, Sound& sound)
    {
        sound = Sound{};
        TESForm* topic = LookupForm(topicId);
        TESForm* info = static_cast<TESForm*>(TopicInfo(infoId));
        void* process = speaker ? At<void*>(speaker, layout::kActorProcess) : nullptr;
        if (!g_pickLine || !g_firstResponse || !process || !IsActor(speaker) ||
            !topic || topic->typeID != kFormType_TESTopic || !info || response < 0 || response > 255)
            return false;
        uint32_t now = GetTickCount();
        g_said.erase(std::remove_if(g_said.begin(), g_said.end(), [&](const Said& s) {
            return static_cast<int32_t>(now - s.until) >= 0 || s.speaker == speaker; }), g_said.end());
        g_said.push_back({speaker, info, now + kSaidMs});
        g_armed = Armed{topic, info, speaker, response, now + kArmedMs};
        using SayFn = void(__thiscall*)(void*, TESObjectREFR*, TESForm*, uint8_t, uint8_t, uint8_t, uint8_t);
        SayFn say = reinterpret_cast<SayFn>((*reinterpret_cast<void***>(process))[addr::kProcessSaySlot]);
        auto* general = reinterpret_cast<uint8_t*>(addr::kSettingGeneralSubtitles + 4);
        const auto* dialogue = reinterpret_cast<const uint8_t*>(addr::kSettingDialogueSubtitles + 4);
        uint8_t was = *general;
        *general = *dialogue;
        say(process, speaker, topic, 0, 0, 1, 1);
        *general = was;
        std::lock_guard<std::mutex> lock(g_voicesLock);
        auto voice = g_voices.find(speaker->refID);
        if (voice != g_voices.end())
            sound = voice->second;
        return true;
    }

    bool StopSpeaking(const TESObjectREFR* speaker)
    {
        Sound voice;
        {
            std::lock_guard<std::mutex> lock(g_voicesLock);
            auto it = speaker ? g_voices.find(speaker->refID) : g_voices.end();
            if (it == g_voices.end())
                return false;
            voice = it->second;
            g_voices.erase(it);
        }
        bool playing = IsPlaying(voice);
        StopSound(voice);
        return playing;
    }

    bool IsPlaying(Sound& sound)
    {
        return sound.id != 0xFFFFFFFF && reinterpret_cast<bool(__thiscall*)(Sound*)>(addr::kSoundHandleIsPlaying)(&sound);
    }

    void EndSubtitle(const std::string& text)
    {
        const char* hud = *reinterpret_cast<const char* const*>(addr::kHudMainMenu);
        if (!hud || text.empty())
            return;
        auto end = [&](char* subtitle) {
            if (subtitle && std::strncmp(subtitle, text.c_str(), 0x103) == 0)
                *reinterpret_cast<float*>(subtitle + layout::kSubtitleShowFor) = 0;
        };
        end(At<char*>(hud, layout::kHudSubtitleShown));
        char* const* data = At<char* const*>(hud, layout::kHudSubtitles + 4);
        uint32_t size = At<uint32_t>(hud, layout::kHudSubtitles + 8);
        for (uint32_t i = 0; data && i < size && i < 64; ++i)
            end(data[i]);
    }

    void StopSound(Sound& sound)
    {
        if (sound.id != 0xFFFFFFFF)
            reinterpret_cast<bool(__thiscall*)(Sound*)>(addr::kSoundHandleStop)(&sound);
        sound = Sound{};
    }

    void ShowSubtitle(TESObjectREFR* speaker, const char* text, bool always)
    {
        struct SoundHandle { uint32_t id = 0xFFFFFFFF; uint8_t assumeSuccess = 0; uint32_t state = 0; };
        struct Point { float x, y, z; };
        using Fn = bool(__cdecl*)(const char*, SoundHandle, Point, TESObjectREFR*, bool);
        if (!speaker || !text || !*text)
            return;
        auto* general = reinterpret_cast<uint8_t*>(addr::kSettingGeneralSubtitles + 4);
        const auto* dialogue = reinterpret_cast<const uint8_t*>(addr::kSettingDialogueSubtitles + 4);
        if (!*dialogue && !always)
            return;
        uint8_t was = *general;
        *general = 1;
        reinterpret_cast<Fn>(addr::kShowSubtitle)(text, SoundHandle{}, Point{speaker->posX, speaker->posY,
            speaker->posZ}, speaker, false);
        *general = was;
    }

    void* TopicInfo(uint32_t id)
    {
        TESForm* form = LookupForm(id);
        return form && form->typeID == kFormType_TESTopicInfo ? form : nullptr;
    }

    uint32_t TopicInfoId(const void* info)
    {
        return info ? static_cast<const TESForm*>(info)->refID : 0;
    }

    std::string TopicInfoPrompt(const void* info)
    {
        const char* text = info ? static_cast<const TESTopicInfo*>(info)->prompt.m_data : nullptr;
        return text ? text : "";
    }

    std::string PlayerReplyText(uint32_t topic, uint32_t info)
    {
        constexpr uint8_t kTopicTypeTopic = 0;
        const TESForm* form = LookupForm(topic);
        if (!form || form->typeID != kFormType_TESTopic ||
            static_cast<const TESTopic*>(form)->type != kTopicTypeTopic)
            return "";
        std::string prompt = TopicInfoPrompt(TopicInfo(info));
        if (!prompt.empty())
            return prompt;
        const char* name = static_cast<const TESTopic*>(form)->fullName.name.m_data;
        return name ? name : "";
    }

    bool HasResultScript(void* info, bool onEnd)
    {
        using Fn = void*(__thiscall*)(void*, uint32_t);
        return info && reinterpret_cast<Fn>(addr::kTopicInfoGetResultScript)(info, onEnd ? 1 : 0) != nullptr;
    }

    uint16_t ScreenCommandIn(void* info, bool onEnd, int* statements)
    {
        if (statements)
            *statements = 0;
        using Fn = Script*(__thiscall*)(void*, uint32_t);
        Script* script = info ? reinterpret_cast<Fn>(addr::kTopicInfoGetResultScript)(info, onEnd ? 1 : 0) : nullptr;
        if (!script || !script->data)
            return 0;
        const uint8_t* data = script->data;
        uint32_t size = script->info.dataLength;
        auto opensScreen = [](uint16_t op) {
            for (uint16_t o : opcodes::kOpensScreen)
                if (o == op)
                    return true;
            return false;
        };
        auto u16 = [&](uint32_t at) { return static_cast<uint16_t>(data[at] | (data[at + 1] << 8)); };
        std::string ops;
        int count = 0;
        uint32_t at = 0;
        while (at + 4 <= size)
        {
            uint16_t op = u16(at);
            if (op == opcodes::kOnReference)
            {
                at += 4;
                continue;
            }
            uint16_t length = u16(at + 2);
            char b[8];
            std::snprintf(b, sizeof(b), "%X ", op);
            ops += b;
            ++count;
            if (opensScreen(op))
                return op;
            at += 4 + length;
        }
        if (statements)
            *statements = at == size ? count : -1;
        if (at == size)
            return 0;
        LOG_DEBUG("Dialogue: line %08X's %s script: %u bytes, statements %s(didn't add up)",
            static_cast<TESForm*>(info)->refID, onEnd ? "end" : "start", size, ops.c_str());
        for (uint32_t i = 0; i + 1 < size; ++i)
            if (opensScreen(u16(i)))
                return u16(i);
        return 0;
    }
}
