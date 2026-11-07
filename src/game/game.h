// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/smoothing.h"
#include "coop/talk_log.h"
#include "net/protocol.h"

#include <cstdint>
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
    bool IsWaiting(TESObjectREFR* ref);
    void SetWaiting(TESObjectREFR* ref, bool waiting);
    TESForm* FollowPackage(TESObjectREFR* target);
    void GivePackage(TESObjectREFR* ref, TESForm* package);
    void DropPackage(TESObjectREFR* ref);
    uint32_t CurrentPackage(TESObjectREFR* ref);
    bool InCombat(TESObjectREFR* ref);
    bool CompanionWheelOpen();

    bool WatchPlayerCommands();
    void MirrorPlayerCommands(bool on);
    void ScreenFromHost(bool on);
    void SkipIntroPopups(bool on, const std::vector<std::string>& loadOrder);
    bool TakeScriptKill();
    void DialogueLineRunning(bool running);
    std::vector<net::PlayerCommand> TakePlayerCommands();
    bool ApplyPlayerCommand(const net::PlayerCommand& c);
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
    void RemoveAllItems(TESObjectREFR* ref);
    bool AddAndEquip(TESObjectREFR* ref, uint32_t formId);
    void RemoveItem(TESObjectREFR* ref, uint32_t formId);
    struct MyAttack
    {
        uint8_t shots = 0;
        bool release = false;
    };
    MyAttack TakeMyAttacks();
    void PlayAttack(TESObjectREFR* ref, const std::vector<uint32_t>& worn, uint8_t shots);
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
    std::string TakeTextEntry();
    void TypeIntoEntry(const std::string& text);
    bool MainMenuMade();
    bool InstallLoadCheck(bool (*mayLoad)(const std::string& save));
    int TakeAnswer(int id);
    constexpr int kNeverShown = -2;
    bool AskChoice(const std::string& text, const std::vector<std::string>& buttons, int id);
    int TakeChoice(int id);
    void DropQuestion(int id);
    bool QuestionOpen();
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
    void MakeCompanion(TESObjectREFR* ref);
    void StopFighting(TESObjectREFR* ref);
    void SetPlayerEssential(bool on);
    void SetEssential(TESObjectREFR* ref, bool on);
    void KnockOut(TESObjectREFR* ref);
    void KillPlayer();
    void Kill(TESObjectREFR* ref, uint16_t limbs = 0);
    uint16_t LimbsGone(TESObjectREFR* ref);
    void SetMaxHealth(TESObjectREFR* ref, float health);
    std::vector<TESObjectREFR*> MapMarkers();
    bool CanTravelTo(const TESObjectREFR* marker);
    std::string MarkerName(const TESObjectREFR* marker);
    void UnlockTravel(TESObjectREFR* marker);
    TESForm* TeleporterItem();
    int ItemCount(TESObjectREFR* ref, TESForm* item);
    void SetRefMapMarker(TESObjectREFR* ref, bool on);
    void SetOnCompass(TESObjectREFR* actor, bool on);
    std::string FurnitureAnim(TESObjectREFR* actor);
    void PlayAnimFile(TESObjectREFR* actor, const std::string& path);
    void StopAnimFile(TESObjectREFR* actor, const std::string& path);
    bool IsPlayingAnimFile(TESObjectREFR* actor, const std::string& path, bool loop = true);
    void SetCollision(TESObjectREFR* actor, bool on);
    void SetActorAlpha(TESObjectREFR* actor, float alpha);
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
    void Resurrect(TESObjectREFR* ref);
    void NoteMadeCharacters(const std::vector<uint32_t>& refIds);
    bool RunConsoleLine(const char* line, TESObjectREFR* on = nullptr);
    std::string Evaluate(const char* expression);
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
    };
    bool WatchExplosions();
    std::vector<SeenExplosion> TakeExplosions();
    bool ShowExplosion(uint32_t form, uint32_t space, float x, float y, float z);
    bool IsAttackPhase(int action);
    void SetUnconscious(TESObjectREFR* ref, bool out);
    void WakeUp(TESObjectREFR* ref);
    void SetPlayerControls(bool on);
    bool IsControlPressed(int control);
    int LifeState(const TESObjectREFR* ref);
    bool IsKnockedOut(const TESObjectREFR* ref);
    bool IsDead(const TESObjectREFR* ref);
    bool InMenu();
    bool IsKeyPressed(int key);
    bool OpenChat();
    bool ChatTyping();
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
    struct TalkLogLook
    {
        float scale = 1;
        float gap = 6;
        bool dividers = true, hudColor = true;
        int style = 1;
        uint8_t red = 255, green = 255, blue = 255;
    };
    void ShowTalkLog(const std::vector<coop::TalkLog::Shown>& lines, const TalkLogLook& look);
    // One panel for the party's event lines and, while chat is open, the chat history. Empty text hides it.
    struct ChatPanelLook
    {
        int xPercent = 98, yPercent = 45;
        float width = 500, scale = 0.65f;
        bool alignRight = true;
    };
    void ShowChatPanel(const std::string& text, const ChatPanelLook& look, float alpha);
    // Lines to move back through the history (negative is forward) since last asked
    int TakeChatScroll();
    void SetSpeedMult(TESObjectREFR* actor, int mult);
    float CachedRunSpeed(TESObjectREFR* actor);
    std::vector<std::string> ModAnimFiles(TESObjectREFR* actor);
    void SetAnimOverride(TESObjectREFR* actor, const std::string& path, bool on);
    float DistanceToPlayer(const TESObjectREFR* ref);
    bool InLoadingScreen();

    float LoadedReach();
    int LoadedGrid();
    int ExteriorCellBuffer();
    bool InMenuMode();
    void OpenMenus(char* out, size_t size);
    bool QuantityPromptOpen();
    bool SetFieldOfView(int degrees);
    bool ReadMyLooks(net::PlayerLooks& out);
    bool ApplyLooks(TESObjectREFR* standIn, const net::PlayerLooks& looks);

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
    struct Item { uint32_t id; int32_t count; uint8_t health = net::kNoCondition; uint8_t mods = 0; };
    constexpr size_t kMaxItemKinds = 500;
    bool Contents(TESObjectREFR* ref, std::vector<Item>& out);
    void ChangeItem(TESObjectREFR* ref, uint32_t itemId, int32_t delta);
    void ChangeItem(TESObjectREFR* ref, const net::ItemCount& kind);
    void Activate(TESObjectREFR* ref);
    void RefreshItemList();
    bool IsItem(uint32_t formId);
    bool IsLootable(TESObjectREFR* ref);

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
    void SetWorldSpeed(float speed);
    bool WatchSpeedChanges();
    struct Sound { uint32_t id = 0xFFFFFFFF; uint8_t assumeSuccess = 0; uint32_t state = 0; };
    bool SayLine(TESObjectREFR* speaker, uint32_t topic, uint32_t info, int response, Sound& sound);
    void StopSound(Sound& sound);
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
    std::vector<std::pair<uint32_t, void*>> DetachQuestScripts(bool (*accept)(uint32_t id, const void* context),
        const void* context);
    void PutBackQuestScripts(const std::vector<std::pair<uint32_t, void*>>& scripts);
    std::vector<std::pair<uint32_t, double>> QuestVariables(uint32_t id);
    void* SwapQuestScript(uint32_t id, void* script);
    void* QuestScript(uint32_t id);

    void ItemsNear(std::vector<TESObjectREFR*>& out, float range);
    void MovablesNear(std::vector<TESObjectREFR*>& out, float range);
    bool Get3DPosition(TESObjectREFR* ref, float& x, float& y, float& z);
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
