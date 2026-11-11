// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/given_rounds.h"
#include "coop/smoothing.h"
#include "coop/ping.h"
#include "coop/chat_history.h"
#include "coop/chat_pad.h"
#include "coop/talk_log.h"
#include "net/protocol.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

struct NVSEInterface;
class TESForm;
class TESObjectREFR;

namespace coop { struct Balance; }

namespace game
{
    bool Init(const NVSEInterface* nvse);
    bool ListenForHits();
    bool ListenForEnables();
    bool CompileScripts();

    TESObjectREFR* Player();
    TESForm* LookupForm(uint32_t formId);
    TESObjectREFR* LookupRef(uint32_t refId);
    uint32_t RefId(const TESObjectREFR* ref);
    bool GetPose(const TESObjectREFR* ref, coop::Pose& out);

    uint8_t StanceFlags(TESObjectREFR* ref);
    void Stance(TESObjectREFR* ref, uint8_t& flags, int& sitting);
    std::vector<uint32_t> PlayerGear();
    std::vector<uint32_t> Gear(TESObjectREFR* ref, bool armourOnly);
    std::vector<net::WornItem> WornGear(TESObjectREFR* ref);
    bool Wear(TESObjectREFR* ref, const net::WornItem& item);
    bool IsPastIntro();
    bool InDocMitchellsHouse();
    std::vector<std::string> LoadOrder();
    std::vector<std::string> LoadedMods();

    TESObjectREFR* SpawnStandIn(const char* name, const net::PlayerLooks* looks = nullptr);

    void BringToPlayer(TESObjectREFR* ref);
    void MoveOnto(TESObjectREFR* ref, TESObjectREFR* to);
    std::vector<uint32_t> Followers();
    bool IsTeammate(TESObjectREFR* ref);
    std::vector<uint32_t> Teammates();
    // Also report hits to and from these (the player's and stand-ins' always are).
    void WatchHits(std::vector<uint32_t> refs);
    void SideWithPlayer(TESObjectREFR* ref);
    bool IsWaiting(TESObjectREFR* ref);
    void SetWaiting(TESObjectREFR* ref, bool waiting);
    TESForm* FollowPackage(TESObjectREFR* target);
    void GivePackage(TESObjectREFR* ref, TESForm* package);
    void DropPackage(TESObjectREFR* ref);
    uint32_t CurrentPackage(TESObjectREFR* ref);
    bool InCombat(TESObjectREFR* ref);
    bool CompanionWheelOpen();

    bool WatchPlayerCommands();
    bool WatchObjectCommands();
    void WatchObjects(bool on);
    struct ObjectChange
    {
        uint32_t ref;
        bool played;
        uint16_t group;
        bool init;
        bool destroyed;
    };
    std::vector<ObjectChange> TakeObjectChanges();
    bool IsDestroyed(const TESObjectREFR* ref);
    void SetDestroyedFlag(TESObjectREFR* ref, bool destroyed);
    bool PlayObjectGroup(TESObjectREFR* ref, uint16_t group, bool init);
    const char* PlayableGroupName(uint16_t group);
    const void* Model(TESObjectREFR* ref);
    void DestructiblesNear(std::vector<TESObjectREFR*>& out, float range);
    float ObjectFullHealth(const TESObjectREFR* ref);
    float ObjectHealth(const TESObjectREFR* ref);
    void DamageObjectBy(TESObjectREFR* ref, float damage);
    float ObjectSelfDamage(const TESObjectREFR* ref);
    void HoldRewardsFrom(const TESObjectREFR* ref, uint32_t ms);
    bool WatchActivations();
    void KeepActivations(bool on);
    std::vector<uint32_t> TakeActivations();
    bool UsedLikeActivator(const TESObjectREFR* ref);
    void SendQuestCommandsOf(std::function<bool(uint32_t)> hostRuns);
    std::vector<net::QuestCommand> TakeQuestCommands();
    bool RunQuestCommand(const net::QuestCommand& command, const char** skipped);
    void HoldPlayerEffects(bool on);
    void UpdateOwnUseLock(uint32_t now, bool inConversation);
    bool OwnUseHoldsControls();
    void GiveBackOwnUseControls(const char* why);
    void ActivateAsPlayer(TESObjectREFR* ref);
    bool LabActivate(TESObjectREFR* ref);
    void MirrorPlayerCommands(bool on);
    void ScreenFromHost(bool on);
    void SkipIntroPopups(bool on, const std::vector<std::string>& loadOrder);
    bool TakeScriptKill();
    void DialogueLineRunning(bool running);
    std::vector<net::PlayerCommand> TakePlayerCommands();
    bool ApplyPlayerCommand(const net::PlayerCommand& c);
    int GiveBackAfterReplay();
    void Move(TESObjectREFR* ref, const coop::Pose& pose);
    bool TravelTo(const coop::Pose& pose);
    void Turn(TESObjectREFR* ref, float heading);
    void SetPitch(TESObjectREFR* ref, float pitch);
    void LogAimSlots(TESObjectREFR* ref);
    void SetWeaponOut(TESObjectREFR* ref, bool out);
    void SetDragging(bool on);
    bool* PlayerAlwaysRun();
    void SetAlert(TESObjectREFR* ref, bool alert);
    void SetRestrained(TESObjectREFR* ref, bool on);
    void SetCombatOff(TESObjectREFR* ref, bool off);
    void EvaluatePackage(TESObjectREFR* ref);
    bool Jump(TESObjectREFR* ref);
    uint16_t MovementFlags(TESObjectREFR* ref);
    void SetMovementFlags(TESObjectREFR* ref, uint16_t flags);
    void SetUpGearLockers(const std::vector<std::string>& loadOrder);
    bool IsGearLocker(uint32_t ref);
    void UpdateRewardBoxes(bool host);
    std::vector<net::PlayerCommand> GearForJoiner();
    void ReplayingOthersLine(bool on);
    void RemoveAllItems(TESObjectREFR* ref);
    bool AddAndEquip(TESObjectREFR* ref, uint32_t formId, coop::GivenRounds* given = nullptr);
    void RemoveItem(TESObjectREFR* ref, uint32_t formId);
    struct MyAttack
    {
        uint8_t shots = 0;
        bool release = false;
        bool reload = false;
        bool paths = false;
    };
    MyAttack TakeMyAttacks();
    void PlayAttack(TESObjectREFR* ref, const std::vector<uint32_t>& worn, uint8_t shots, bool fire = true);
    uint32_t ShotPathWaitMs(const std::vector<uint32_t>& worn);
    void FireShots(TESObjectREFR* ref, const std::vector<uint32_t>& worn, uint32_t shots);
    bool FireAlong(TESObjectREFR* ref, const std::vector<uint32_t>& worn, const net::ShotPath& path, bool withEffects);
    void ReleaseThrow(TESObjectREFR* ref, const std::vector<uint32_t>& worn);
    void Remove(TESObjectREFR* ref);
    void Notify(const char* text);
    bool AskYesNo(const std::string& text, int id);
    void ShowMessage(const std::string& text);
    void RetryMessageBoxes();
    bool AddCoopToMainMenu(void (*onHost)(), void (*onJoin)());
    enum class EntryState { None, Typing, Done, Cancelled };
    bool StartTextEntry();
    void StopTextEntry();
    EntryState TextEntryState();
    size_t TextEntryLength();
    std::string TextEntryText();
    std::string TakeTextEntry();
    void TypeIntoEntry(const std::string& text);
    bool MainMenuMade();
    bool InstallLoadCheck(bool (*mayLoad)(const std::string& save));
    int TakeAnswer(int id);
    constexpr int kNeverShown = -2;
    bool AskChoice(const std::string& text, const std::vector<std::string>& buttons, int id);
    // Changes an open question's text, on screen too, and remembers it so DropQuestion can still close the box.
    void SetQuestionText(int id, const std::string& text);
    int TakeChoice(int id);
    void DropQuestion(int id);
    bool QuestionOpen();
    // A box co-op showed (a question or a message) is the message box on screen.
    bool OurBoxShown();
    bool MessageShows(const std::string& text);
    std::string MessageText();
    bool SetMessageText(const std::string& text);
    bool SetMessageButton(int index, const std::string& label);
    void OpenLoadMenu();
    void QuitGame();
    bool RunInBackground(bool on);
    void Notify(const char* text, float seconds);

    bool AiStateBits(TESObjectREFR* ref, uint32_t& out);
    int AnimAction(TESObjectREFR* ref);
    void ConsolePrint(const char* text);

    bool IsActor(const TESObjectREFR* ref);
    void NearbyActors(std::vector<TESObjectREFR*>& out);
    const char* Name(const TESObjectREFR* ref);
    struct BaseInfo
    {
        uint32_t base = 0, leveledBase = 0, leveledActor = 0;
        uint8_t baseType = 0, leveledBaseType = 0, leveledActorType = 0;
    };
    BaseInfo DescribeBase(TESObjectREFR* ref);
    TESObjectREFR* PlaceActor(uint32_t baseId);
    bool Is3DLoaded(TESObjectREFR* ref);
    float Health(TESObjectREFR* ref);
    int PlayerLevel();
    float MaxHealth(TESObjectREFR* ref);
    void ChangeHealth(TESObjectREFR* ref, float amount);
    float PlayerMedicine();
    bool CastOnSelf(TESObjectREFR* ref, uint32_t formId);
    void MakeCompanion(TESObjectREFR* ref);
    void StopFighting(TESObjectREFR* ref);
    void SetPlayerEssential(bool on);
    void SetEssential(TESObjectREFR* ref, bool on);
    void KnockOut(TESObjectREFR* ref);
    // Makes a standing NPC visibly fall and stay down until WakeUp: an essential one through the game's own essential
    // fall (health below 0), any other unconscious with a tiny push to ragdoll it.
    enum class KnockedHow { Not, Essential, Unconscious };
    KnockedHow KnockDown(TESObjectREFR* ref);
    bool IsEssential(const TESObjectREFR* ref);
    void KillPlayer();
    void Kill(TESObjectREFR* ref, uint16_t limbs = 0);
    void Unprotect(TESObjectREFR* ref);
    void KillPlain(TESObjectREFR* ref);
    bool AiThreadsRunning();
    uint16_t LimbsGone(TESObjectREFR* ref);
    void SetMaxHealth(TESObjectREFR* ref, float health);
    std::vector<TESObjectREFR*> MapMarkers();
    bool CanTravelTo(const TESObjectREFR* marker);
    std::string MarkerName(const TESObjectREFR* marker);
    void UnlockTravel(TESObjectREFR* marker);
    void ForgetMadeCopies();
    void ReportForgottenCopies();
    TESForm* TeleporterItem();
    int ItemCount(TESObjectREFR* ref, TESForm* item);
    void SetRefMapMarker(TESObjectREFR* ref, bool on);
    void SetOnCompass(TESObjectREFR* actor, bool on);
    std::string FurnitureAnim(TESObjectREFR* actor);
    void PlayAnimFile(TESObjectREFR* actor, const std::string& path);
    void StopAnimFile(TESObjectREFR* actor, const std::string& path);
    bool IsPlayingAnimFile(TESObjectREFR* actor, const std::string& path, bool loop = true);
    // Puts actor's playing animation file at the same point as other's (same file), if it's more than `slack` seconds
    // off. False if either isn't playing it (or hasn't started yet); *moved gets the seconds it was moved by.
    bool MatchAnimFileTime(TESObjectREFR* actor, TESObjectREFR* other, const std::string& path, float slack,
        float* moved = nullptr);
    void SetCollision(TESObjectREFR* actor, bool on);
    void SetActorAlpha(TESObjectREFR* actor, float alpha);
    bool PipBoyLightOn(TESObjectREFR* actor);
    void SetPipBoyLight(TESObjectREFR* actor, bool on);
    void AddPipBoyLight(TESObjectREFR* actor);
    uint8_t PlayerDisabledControls();
    TESObjectREFR* SitInNearestChair(TESObjectREFR* actor);
    uint32_t PlayedIdle(TESObjectREFR* ref);
    bool PlayIdle(TESObjectREFR* ref, uint32_t idleId);
    void PlayNpcAttack(TESObjectREFR* ref, uint8_t shots);
    void ReleaseNpcThrow(TESObjectREFR* ref);
    uint32_t HeldWeaponId(TESObjectREFR* ref);
    bool HoldsHarmless(TESObjectREFR* ref);
    struct GunState { uint32_t weapon = 0; int rounds = -1; };
    int CountShots(TESObjectREFR* ref, GunState& state);
    void StopIdle(TESObjectREFR* ref);
    bool Resurrect(TESObjectREFR* ref);
    void SwitchBody(TESObjectREFR* ref, bool on);
    void NoteMadeCharacters(const std::vector<uint32_t>& refIds);
    bool RunConsoleLine(const char* line, TESObjectREFR* on = nullptr);
    std::string Evaluate(const char* expression, TESObjectREFR* on = nullptr);
    std::vector<uint32_t> SavedMadeCharacters();
    void ResetAnimation(TESObjectREFR* ref, int how);
    bool InScriptAnimation(TESObjectREFR* ref);
    bool Reload(TESObjectREFR* ref, int animType);
    uintptr_t ReloadFunction(TESObjectREFR* ref);
    bool IsAttackAction(int action);
    bool IsReloadAction(int action);
    bool IsThrowRelease(int action);

    struct SeenExplosion
    {
        uint32_t form = 0;
        uint32_t space = 0;
        float x = 0, y = 0, z = 0;
        bool ours = false;
        bool objectsOwn = false;
    };
    bool WatchExplosions();
    std::vector<SeenExplosion> TakeExplosions();
    bool WatchMyShots();
    void SetShotPaths(bool on);
    std::vector<net::ShotPath> TakeMyShots();
    bool ShowExplosion(uint32_t form, uint32_t space, float x, float y, float z);
    bool IsAttackPhase(int action);
    void SetGhost(TESObjectREFR* ref, bool on);
    bool IsGhost(TESObjectREFR* ref);
    void SetUnconscious(TESObjectREFR* ref, bool out);
    void WakeUp(TESObjectREFR* ref);
    void SetPlayerControls(bool on);
    bool IsControlPressed(int control);
    int LifeState(const TESObjectREFR* ref);
    bool IsKnockedOut(const TESObjectREFR* ref);
    bool IsDead(const TESObjectREFR* ref);
    int PlayerPerkRank(uint32_t perk);
    TESObjectREFR* KillerOf(TESObjectREFR* actor);
    bool IsCreatureType(TESObjectREFR* ref, int type);
    bool GlobalNumber(uint32_t global, float& value);
    bool SetGlobalNumber(uint32_t global, float value);
    void LevelUpNote(int action);
    bool InMenu();
    bool IsKeyPressed(int key);
    bool OpenChat();
    bool ChatTyping();
    bool ChatDraftEmpty();
    // Shown in place of an empty "Say:" line; empty for the usual line
    void SetChatHint(const std::string& hint);
    // Down by any route: the keyboard, the game, or put in by a script or another mod, even while turned off for the game
    bool IsKeyDown(int key);
    void CancelChat();
    bool KeyDown(int key);
    bool ApplyBalance(const coop::Balance* balance);
    int QuestStage(uint32_t quest);
    bool LeaveTutorial();
    void RemoveTutorialFades();
    void HoldTutorial(bool hold);
    bool ActivateByPlayer(uint32_t ref, bool moveHereFirst);
    bool GiveStarterGear();
    bool SetMouth(TESObjectREFR* actor, float opening);
    void ShowTalking(bool talking);
    void TypeChat(unsigned char c);
    std::vector<std::string> UpdateChat();
    void ShowChatAbove(int slot, const TESObjectREFR* who, const std::string& text);
    void UpdateChatAbove();
    struct FriendMark
    {
        TESObjectREFR* who = nullptr;
        std::string text;
        bool downed = false;
    };
    void ShowFriendMarkers(const std::vector<FriendMark>& friends);
    void ShowReviveBar(const std::string& text);
    void HideTalkPromptOnFriends();
    bool IsStandIn(const TESObjectREFR* ref);
    bool InstallAllyDetectionSkip();
    struct TalkLogLook
    {
        float scale = 1;
        float gap = 6;
        int place = 0;
        float xPercent = 2, yPercent = 25;
        float width = 600;
        bool alignRight = false;
        bool dividers = true, hudColor = true;
        int style = 1;
        uint8_t red = 255, green = 255, blue = 255;
    };
    void ShowTalkLog(const std::vector<coop::TalkLog::Shown>& lines, const TalkLogLook& look);
    // One panel for the party's event lines and, while chat is open, the chat history. No rows hides it.
    struct ChatPanelLook
    {
        int xPercent = 98, yPercent = 45;
        float width = 500, scale = 0.65f, gap = 6;
        bool alignRight = true, dividers = true;
        int style = 1;
    };
    void ShowChatPanel(const std::vector<coop::ChatPanelRow>& rows, const ChatPanelLook& look);
    // Chat with a controller (chat_pad.cpp): the first connected controller, and keeping it from the game while chat has
    // it (the game sees nothing pressed)
    bool ReadChatPad(coop::ChatPadInput& out);
    void HidePadFromGame(bool hide);
    void SetSpeedMult(TESObjectREFR* actor, int mult);
    float CachedRunSpeed(TESObjectREFR* actor);
    std::vector<std::string> ModAnimFiles(TESObjectREFR* actor);
    void SetAnimOverride(TESObjectREFR* actor, const std::string& path, bool on);
    // Too Many Emotes (does nothing without it): the local player's emote, and friends' songs.
    struct MyEmote
    {
        std::string path;
        uint32_t song = 0;
    };
    bool EmotesReady();
    MyEmote ReadMyEmote();
    void PlayEmoteSong(TESObjectREFR* from, uint32_t song);
    bool EmoteSongPlaying(TESObjectREFR* from, uint32_t song);
    void StopEmoteSong(TESObjectREFR* from, uint32_t song);
    bool LeaveEmoteDance(TESObjectREFR* who);
    // A gamepad: in use (the game's own prompts follow this), the button bound to a control ("A"; empty if none
    // known), whether it's held, and whether the pad moves the player (left stick, triggers, or the buttons for
    // attack, aim, drawing a weapon, jumping).
    bool UsingGamepad();
    std::string PadButtonName(int control);
    bool PadButtonPressed(int control);
    bool PadMoving();
    // The friend's character the player last activated (any key or button), once; null if none since.
    TESObjectREFR* TakeActivatedStandIn();
    // Third person for dancing (the mod's camera option on, and in first person now): true if it switched.
    bool EmoteCameraOut();
    void EmoteCameraBack();
    float DistanceToPlayer(const TESObjectREFR* ref);
    bool InLoadingScreen();

    float LoadedReach();
    int LoadedGrid();
    int ExteriorCellBuffer();
    bool InMenuMode();
    void OpenMenus(char* out, size_t size);
    bool QuantityPromptOpen();
    bool TradeMenuOpen();
    // The Pip-Boy (its items, stats or map pages) is open.
    bool PipBoyOpen();
    bool SetFieldOfView(int degrees);
    bool ReadMyLooks(net::PlayerLooks& out);
    // The voice line wheel: shown with one phrase picked (0 = the middle), in the HUD colour or its own.
    struct VoiceWheelLook { bool hudColor = true; float red = 255, green = 255, blue = 255; bool female = false; };
    // chosen: the hovered slot (-1 for none); line: what that phrase will say, shown under the wheel.
    void ShowVoiceWheel(bool show, int page, int chosen, const char* line, const char* pageHint,
        const VoiceWheelLook& look);
    // While the wheel is open: how far the view turned since last time (radians, x right, y up), then turns it back.
    bool WheelPointer(bool open, float& dx, float& dy, int& scroll);
    // A controller (XInput): its buttons (XINPUT_GAMEPAD_* bits) and right stick (-1..1, up positive).
    struct PadState { bool connected = false; uint16_t buttons = 0; float rx = 0, ry = 0; };
    bool ReadPad(PadState& out);
    // Turns the player's looking, attacking and view switching off while the wheel is worked with a controller (and
    // back on, only the ones it turned off).
    void HoldControlsForWheel(bool hold);
    bool ApplyLooks(TESObjectREFR* standIn, const net::PlayerLooks& looks, bool* headRemade = nullptr);
    bool RemakeFace(TESObjectREFR* standIn);
    const void* FaceNode(TESObjectREFR* standIn);
    void FaceToneFrame();

    void DoorsNear(std::vector<TESObjectREFR*>& out, float range);
    struct DoorInfo
    {
        int open = 0;
        bool locked = false;
        int lockLevel = 0;
        bool leadsAway = false;
    };
    DoorInfo ReadDoor(TESObjectREFR* ref);
    int GameSaysLocked(TESObjectREFR* ref);
    uint8_t PileKind(TESObjectREFR* actor);
    void MakePile(TESObjectREFR* actor, uint8_t kind);
    void SetDoorOpen(TESObjectREFR* ref, bool open);
    void SetDoorLocked(TESObjectREFR* ref, bool locked, int level);

    void ContainersNear(std::vector<TESObjectREFR*>& out, float range);
    void MinesNear(std::vector<TESObjectREFR*>& out, float range);
    int MineState(const TESObjectREFR* ref);
    bool TurnOffMine(TESObjectREFR* mine);
    bool MineExplosion(TESObjectREFR* mine);
    bool SetMineRules(TESObjectREFR* const* puppets, size_t count, bool friendGame);
    bool HoldLevelUp(bool fighting);
    TESObjectREFR* OpenContainer();
    TESObjectREFR* CrosshairRef();
    struct CrosshairPick
    {
        bool hit = false;
        float at[3] = {};
        float distance = 0;
        char what[64] = {};
        coop::PingThing thing = coop::PingThing::Nothing;
        uint32_t ref = 0;
        std::string name;
    };
    bool CrosshairPoint(CrosshairPick& out);
    bool SpotOnScreen(const float (&at)[3], float& x, float& y, bool& inView);
    bool OverHead(TESObjectREFR* ref, float (&at)[3], float fallbackHeight = 130.0f);
    bool WouldAttackPlayer(TESObjectREFR* actor);
    struct PingMark
    {
        bool active = false;
        std::string text;
        float at[3] = {};
        uint8_t kind = 0;
    };
    void ShowPings(const std::array<PingMark, coop::kPingSlots>& marks, coop::PingIcons icons);
    void PlayPingSound(int choice);
    uint16_t PadButtons();
    struct Item { uint32_t id; int32_t count; uint8_t health = net::kNoCondition; uint8_t mods = 0; };
    constexpr size_t kMaxItemKinds = 500;
    bool Contents(TESObjectREFR* ref, std::vector<Item>& out);
    void ChangeItem(TESObjectREFR* ref, uint32_t itemId, int32_t delta);
    void ChangeItem(TESObjectREFR* ref, const net::ItemCount& kind);
    void Activate(TESObjectREFR* ref);
    void RefreshItemList();
    void RefreshLootWindows();
    bool IsItem(uint32_t formId);
    int FormTypeOf(uint32_t formId);
    bool IsLootable(TESObjectREFR* ref);
    bool IsContainer(TESObjectREFR* ref);

    struct QuestInfo
    {
        uint32_t id = 0;
        std::string name;
        uint8_t flags = 0;
        uint8_t stage = 0;
        float delay = 0;
        std::vector<uint8_t> done;
        std::vector<std::pair<uint32_t, uint32_t>> objectives;
    };
    void ReadQuests(std::vector<QuestInfo>& out, bool all);
    bool ReadQuest(uint32_t id, QuestInfo& out);
    void SetQuestFlags(uint32_t id, uint8_t flags);
    void SetQuestStageDone(uint32_t id, uint8_t stage, bool done);
    void SetQuestObjective(uint32_t id, uint32_t objective, uint32_t status);
    uint32_t TrackedQuest();
    bool ShowObjective(uint32_t quest, uint32_t objective, bool on);
    bool CompleteObjective(uint32_t quest, uint32_t objective, bool on);
    bool CompleteQuest(uint32_t quest);
    struct EnableChange { uint32_t ref; bool enabled; };
    std::vector<EnableChange> TakeEnables();
    void SetEnabled(TESObjectREFR* ref, bool enabled);
    // Like SetEnabled, but also for a reference with an enable parent, which the Disable and Enable commands refuse.
    void ForceEnabled(TESObjectREFR* ref, bool enabled);
    bool InDialogue();
    int32_t PlayerXp();
    int32_t PlayerKarma();
    void GiveXp(int32_t xp);
    void GiveKarma(int32_t karma);
    struct ReputationInfo { uint32_t form; std::string name; float fame, infamy; };
    std::vector<ReputationInfo> ReadReputations();
    void WriteReputation(uint32_t form, float fame, float infamy);
    void CloseMenu();
    TESObjectREFR* DialoguePartner();
    struct Spoken
    {
        uintptr_t id = 0;
        const char* text = "";
        uint32_t topic = 0, info = 0;
        int response = 0;
    };
    Spoken SpokenNow();
    bool IsTalking(const TESObjectREFR* actor);
    bool InstallSayHooks();
    int KeepVatsAtFullSpeed();
    void KeepVatsFeel();
    bool PlayVatsInOwnView();
    void SetKillCamMode(uint32_t mode, bool hold);
    void SetVatsOwnViewPlayback(bool on);
    bool NoDeathPlaybackWhenDowned();
    void SetDownedDeathPlayback(bool on);
    uint32_t VatsMode();
    // Sends the game's call at site to another function, returning where it went (0 if site isn't a call).
    uintptr_t HookCallSite(uintptr_t site, const void* to);
    // Puts wrapper (a Cmd_Execute) in front of a script command, returning the original (null if there's no such command).
    void* WrapScriptCommand(const char* name, void* wrapper);
    void SetWorldSpeed(float speed);
    bool WatchSpeedChanges();
    struct Sound { uint32_t id = 0xFFFFFFFF; uint8_t assumeSuccess = 0; uint32_t state = 0; };
    bool SayLine(TESObjectREFR* speaker, uint32_t topic, uint32_t info, int response, Sound& sound);
    // Says one line (response 1) in the given voice type, then gives the speaker back its own voice.
    bool SayInVoice(TESObjectREFR* speaker, uint32_t voice, uint32_t topic, uint32_t info, Sound& sound);
    void StopSound(Sound& sound);
    // Plays a sound file (e.g. "Data\Sound\Voice\...ogg") that isn't placed in the world.
    bool PlayFlatSound(const std::string& path, Sound& sound);
    void SetSoundVolume(Sound& sound, float volume);
    // The voice a character's line plays with (it can turn up a moment after the line starts, the first time a file loads).
    bool SpokenSound(const TESObjectREFR* speaker, Sound& sound);
    bool StopSpeaking(const TESObjectREFR* speaker);
    bool IsPlaying(Sound& sound);
    void EndSubtitle(const std::string& text);
    bool SaidForOthers(const TESObjectREFR* speaker, const void* info);
    struct RadioLineStart
    {
        uint32_t station = 0, info = 0, part = 0;
        size_t line = 0;
        std::vector<net::RadioPick> run;
        bool fromHost = false;
    };
    std::vector<RadioLineStart> TakeRadioLineStarts();
    bool TunePipboyRadio(TESObjectREFR* station);
    void RadioFromHost(bool on);
    void HostRadioLine(const net::RadioLine& line);
    void UpdateRadios();
    void ShowSubtitle(TESObjectREFR* speaker, const char* text, bool always = false);
    void* TopicInfo(uint32_t id);
    uint32_t TopicInfoId(const void* info);
    std::string TopicInfoPrompt(const void* info);
    std::string PlayerReplyText(uint32_t topic, uint32_t info);
    bool HasResultScript(void* info, bool onEnd);
    uint16_t ScreenCommandIn(void* info, bool onEnd, int* statements = nullptr);
    void Greet(TESObjectREFR* actor);
    bool IsEnabled(const TESObjectREFR* ref);
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> PlayerObjectives(size_t& targets);
    void QuestFingerprints(std::vector<std::pair<uint32_t, uint32_t>>& out);
    void WriteQuest(const QuestInfo& q);
    std::string QuestName(uint32_t id);
    std::string ObjectiveText(uint32_t id, uint32_t objective);
    size_t DetachQuestScripts(bool (*accept)(uint32_t id, const void* context), const void* context);
    size_t PutBackQuestScripts();
    std::vector<std::pair<uint32_t, double>> QuestVariables(uint32_t id);
    void* SwapQuestScript(uint32_t id, void* script);
    void* QuestScript(uint32_t id);

    void ItemsNear(std::vector<TESObjectREFR*>& out, float range);
    void MovablesNear(std::vector<TESObjectREFR*>& out, float range);
    bool Get3DPosition(TESObjectREFR* ref, float& x, float& y, float& z);
    bool BodyPosition(TESObjectREFR* actor, float& x, float& y, float& z);
    void MoveBody(TESObjectREFR* actor, float dx, float dy, float dz, bool reload3D);
    // On success, detail is the bone it pulled; otherwise, which step failed.
    bool PullBody(TESObjectREFR* actor, float vx, float vy, float vz, std::string& detail);
    struct WorldItem
    {
        uint32_t base = 0;
        int count = 1;
        uint32_t flags = 0;
        bool inCell = false;
        float x = 0, y = 0, z = 0;
        float rx = 0, ry = 0, rz = 0;
    };
    WorldItem ReadWorldItem(TESObjectREFR* ref);
    bool IsThere(const WorldItem& item);
    TESObjectREFR* TopEnableParent(TESObjectREFR* ref);
    struct Condition { uint8_t health = net::kNoCondition; uint8_t mods = 0; };
    Condition ItemCondition(TESObjectREFR* ref);
    int MoveDroppedIntoBody(TESObjectREFR* body);
    TESObjectREFR* PlaceItem(uint32_t base, int count, float x, float y, float z, uint8_t health = net::kNoCondition,
        uint8_t mods = 0);
    void MoveItem(TESObjectREFR* ref, float x, float y, float z, float rx, float ry, float rz);
    uint32_t GrabbedItem();
    void RemoveWorldItem(TESObjectREFR* ref);
    struct Hit { uint32_t targetRef; uint32_t attackerRef; float damage; };
    std::vector<Hit> TakeHits();
    void ApplyHit(TESObjectREFR* target, TESObjectREFR* by, float damage);
    uint32_t CombatTarget(TESObjectREFR* actor);
    void ForceTarget(TESObjectREFR* actor, TESObjectREFR* target);
    bool IsGeneric(TESObjectREFR* actor);
    TESObjectREFR* PlaceExtraEnemy(TESObjectREFR* like, TESObjectREFR* target);
    int ThinAmmo(TESObjectREFR* body, int keptPercent);
}
