// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// Too Many Emotes (TooManyEmotes.esp): what the local player's emote is, read from the mod's own quest (TMEWheel), and its
// song played from a friend's character. Nothing here runs unless the mod is loaded: the scripts name its quest and
// global, so they're only compiled once the game's plugin list has it.

#include "game/internal.h"

#include <cctype>
#include <cstring>

namespace game
{
    namespace
    {
        bool g_emotesTried = false;
        Script* g_emoteNow = nullptr;
        Script* g_emoteSongPlay = nullptr;
        Script* g_emoteSongPlaying = nullptr;
        Script* g_emoteSongStop = nullptr;
        Script* g_emoteNoDance = nullptr;
        Script* g_emoteJoin = nullptr;

        bool ModLoaded(const char* file)
        {
            for (const std::string& mod : LoadedMods())
                if (_stricmp(mod.c_str(), file) == 0)
                    return true;
            return false;
        }

        TESForm* SongForm(uint32_t song)
        {
            TESForm* form = song ? LookupForm(song) : nullptr;
            return form && form->typeID == kFormType_TESSound ? form : nullptr;
        }
    }

    bool EmotesReady()
    {
        if (g_emotesTried)
            return g_emoteNow != nullptr;
        if (!g_compiled || !g_scripts || !g_arrays)
            return false;
        g_emotesTried = true;
        if (!ModLoaded("TooManyEmotes.esp"))
            return false;
        // The emote now: TMEWheel.wfIdle is the idle the mod made for this part (intro, then loop), and wfMusic says
        // which song it started (1 the intro's, 2 the loop's; 0 when its music is off).
        g_emoteNow = Compile("Too Many Emotes: the emote now",
            "int iStage\n"
            "int iMusic\n"
            "ref rIdle\n"
            "ref rSong\n"
            "array_var aNow\n"
            "Begin Function {}\n"
            "  let aNow := ar_Construct \"array\"\n"
            "  set iStage to TMEWheel.wfStage\n"
            "  if iStage\n"
            "    set rIdle to TMEWheel.wfIdle\n"
            "    set iMusic to TMEWheel.wfMusic\n"
            "    if iMusic == 1\n"
            "      set rSong to TMEWheel.wfSnd\n"
            "    elseif iMusic == 2\n"
            "      set rSong to TMEWheel.wfSndL\n"
            "    endif\n"
            "    ar_Append aNow rIdle\n"
            "    ar_Append aNow rSong\n"
            "  endif\n"
            "  SetFunctionValue aNow\n"
            "End\n");
        // Songs follow this game's own Too Many Emotes music switch too.
        g_emoteSongPlay = Compile("Too Many Emotes: play a song",
            "ref rSong\n"
            "Begin Function {rSong}\n"
            "  if TMEMusic\n"
            "    PlaySound3D rSong\n"
            "  endif\n"
            "End\n");
        g_emoteSongPlaying = Compile("Too Many Emotes: song playing",
            "ref rSong\n"
            "ref rFrom\n"
            "int iOn\n"
            "Begin Function {rSong, rFrom}\n"
            "  set iOn to IsSoundPlaying rSong rFrom\n"
            "  SetFunctionValue iOn\n"
            "End\n");
        g_emoteSongStop = Compile("Too Many Emotes: stop a song",
            "ref rSong\n"
            "ref rFrom\n"
            "Begin Function {rSong, rFrom}\n"
            "  StopSound rSong rFrom\n"
            "End\n");
        // The mod makes everyone near you dance along, friends' characters too, but they aren't dancing in their own
        // game. This takes one out of the mod's list of dancers (TMEWheel.wfNPC), so it stops making them dance.
        g_emoteNoDance = Compile("Too Many Emotes: not a dancer",
            "ref rWho\n"
            "ref rIn\n"
            "int i\n"
            "int n\n"
            "array_var aIn\n"
            "Begin Function {rWho}\n"
            "  let aIn := TMEWheel.wfNPC\n"
            "  let n := ar_Size aIn\n"
            "  while i < n\n"
            "    let rIn := aIn[i]\n"
            "    if rIn == rWho\n"
            "      ar_Erase aIn i\n"
            "      SetFunctionValue 1\n"
            "      return\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "End\n");
        // Join a friend's emote: find it in the mod's list of every emote (TMEVars.wfAll; owning it isn't needed) and hand
        // it to the mod's wheel as if it had just been picked, so the mod plays it, its song, and stops it as usual.
        // Joining one already in its loop part starts straight on the loop and its song (the intro is left out).
        // Not while the wheel is open (it's using its list).
        g_emoteJoin = Compile("Too Many Emotes: join an emote",
            "string_var sPath\n"
            "string_var sIn\n"
            "string_var sLoop\n"
            "array_var aAll\n"
            "array_var aE\n"
            "array_var aGo\n"
            "ref rNone\n"
            "int i\n"
            "int n\n"
            "int iHow\n"
            "Begin Function {sPath}\n"
            "  if TMEWheel.wfOpen\n"
            "    return\n"
            "  endif\n"
            "  let aAll := TMEVars.wfAll\n"
            "  let n := ar_Size aAll\n"
            "  while i < n\n"
            "    let aE := aAll[i]\n"
            "    let sIn := aE[1]\n"
            "    let sLoop := aE[2]\n"
            "    set iHow to 0\n"
            "    if eval (sPath == sIn)\n"
            "      set iHow to 1\n"
            "    elseif eval (sv_Length sLoop) > 0\n"
            "      if eval (sPath == sLoop)\n"
            "        set iHow to 2\n"
            "      endif\n"
            "    endif\n"
            "    if iHow\n"
            "      let aGo := ar_Copy aE\n"
            "      if iHow == 2\n"
            "        let aGo[1] := sLoop\n"
            "        let aGo[3] := 0\n"
            "        let aGo[4] := aE[5]\n"
            "        let aGo[5] := rNone\n"
            "      endif\n"
            "      let TMEWheel.wfData := ar_List aGo\n"
            "      set TMEWheel.wfIdx to 0\n"
            "      set TMEWheel.wfGo to 1\n"
            "      SetFunctionValue iHow\n"
            "      return\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "End\n");
        if (!g_emoteNow)
            LOG_ERROR("Emotes: Too Many Emotes is loaded but its emotes can't be read, so they won't show for friends");
        else
            LOG_INFO("Emotes: Too Many Emotes is loaded; your emotes and their songs show for friends%s",
                g_emoteSongPlay && g_emoteSongPlaying && g_emoteSongStop ? "" : " (songs can't be played here)");
        return g_emoteNow != nullptr;
    }

    MyEmote ReadMyEmote()
    {
        MyEmote out;
        TESObjectREFR* me = Player();
        NVSEArrayVarInterface::Element result;
        if (!EmotesReady() || !me || !g_scripts->CallFunction(g_emoteNow, me, nullptr, &result, 0))
            return out;
        auto* array = result.GetArray();
        uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
        if (size != 2)
            return out;
        NVSEArrayVarInterface::Element elements[2];
        if (!g_arrays->GetElements(array, elements, nullptr))
            return out;
        TESForm* idle = elements[0].GetTESForm();
        if (!idle || idle->typeID != kFormType_TESIdleForm)
            return out;
        const char* path = static_cast<TESIdleForm*>(idle)->anim.nifPath.m_data;
        if (!path || !*path)
            return out;
        out.path = path;
        TESForm* song = elements[1].GetTESForm();
        out.song = song && song->typeID == kFormType_TESSound ? song->refID : 0;
        return out;
    }

    void PlayEmoteSong(TESObjectREFR* from, uint32_t song)
    {
        TESForm* form = SongForm(song);
        if (EmotesReady() && g_emoteSongPlay && from && form)
            g_scripts->CallFunctionAlt(g_emoteSongPlay, from, 1, form);
    }

    bool EmoteSongPlaying(TESObjectREFR* from, uint32_t song)
    {
        TESForm* form = SongForm(song);
        NVSEArrayVarInterface::Element result;
        if (!EmotesReady() || !g_emoteSongPlaying || !from || !form ||
            !g_scripts->CallFunction(g_emoteSongPlaying, from, nullptr, &result, 2, form, from))
            return true;
        return result.GetNumber() != 0;
    }

    void StopEmoteSong(TESObjectREFR* from, uint32_t song)
    {
        TESForm* form = SongForm(song);
        if (EmotesReady() && g_emoteSongStop && from && form)
            g_scripts->CallFunctionAlt(g_emoteSongStop, from, 2, form, from);
    }

    int JoinEmote(const std::string& path)
    {
        std::string file = _strnicmp(path.c_str(), "meshes\\", 7) == 0 ? path.substr(7) : path;
        NVSEArrayVarInterface::Element result;
        TESObjectREFR* me = Player();
        if (!EmotesReady() || !g_emoteJoin || !me || file.empty() ||
            !g_scripts->CallFunction(g_emoteJoin, me, nullptr, &result, 1, file.c_str()))
            return 0;
        return static_cast<int>(result.GetNumber());
    }

    bool LeaveEmoteDance(TESObjectREFR* who)
    {
        NVSEArrayVarInterface::Element result;
        TESObjectREFR* me = Player();
        if (!EmotesReady() || !g_emoteNoDance || !me || !who ||
            !g_scripts->CallFunction(g_emoteNoDance, me, nullptr, &result, 1, who))
            return false;
        return result.GetNumber() != 0;
    }
}
