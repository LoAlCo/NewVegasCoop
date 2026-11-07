// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/containers.h"
#include "coop/dialogue.h"
#include "coop/loading.h"
#include "coop/mod_transfer.h"
#include "coop/mods.h"
#include "coop/chat.h"
#include "coop/voice.h"
#include "coop/desync.h"
#include "coop/game_mode.h"
#include "coop/doors.h"
#include "coop/saves.h"
#include "coop/smoothing.h"
#include "coop/waitvote.h"
#include "coop/world_items.h"
#include "net/protocol.h"
#include "net/session.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace coop
{
    constexpr size_t kMaxFriends = 4;
    constexpr uint32_t kHelloTimeoutMs = 5000;
    constexpr uint32_t kModIdleMs = 120000;
    constexpr uint32_t kApprovalTimeoutMs = 60000;
    constexpr uint32_t kMaxExplosionsPerSecond = 16;
    constexpr uint32_t kMaxHostExplosionsPerSecond = 32;
    constexpr size_t kMaxQueuedExplosions = 64;

    struct JoinAsk
    {
        uint32_t id = 0;
        std::string name;
        bool wantsFiles = false;
    };
    constexpr uint32_t kGameTimeEveryMs = 5000;
    constexpr uint8_t kHostPlayerId = 0;
    constexpr uint32_t kPoseIntervalMs = 50;
    constexpr uint32_t kIdlePoseIntervalMs = 1000;
    constexpr float kMaxHitDamage = 10000;
    constexpr size_t kMaxQueuedHits = 64;
    constexpr size_t kMaxQueuedAttacks = 32;
    constexpr uint32_t kHealthIntervalMs = 200;
    constexpr uint32_t kBleedOutMs = 30000;
    constexpr float kReviveDistance = 600;
    constexpr float kReviveHealth = 0.3f;
    constexpr float kTeleportDistance = 3000;
    constexpr float kSamePlaceDistance = 1500;
    constexpr uint32_t kTravelCooldownMs = 5000;
    constexpr size_t kMaxNpcs = net::kMaxNpcsPerMessage;
    constexpr size_t kMaxKnownNpcs = 2 * kMaxNpcs;
    constexpr uint32_t kNpcTimeoutMs = 3000;
    constexpr uint32_t kNpcGearResendMs = 5000;
    constexpr uint32_t kDoorResendMs = 2000;
    constexpr size_t kMaxQueuedDoorUses = 32;
    constexpr size_t kMaxQueuedItemChanges = 64;
    constexpr size_t kMaxQueuedQuests = 4096;
    constexpr size_t kMaxQueuedMarkers = 4096;
    constexpr uint32_t kItemResendMs = 3000;
    constexpr float kItemSendMoveDistance = 16;
    constexpr float kItemSendTurn = 0.1f;
    constexpr uint32_t kMaxItemMovesPerSecond = 64;
    constexpr float kMaxItemMoveReach = 4096;
    constexpr size_t kMaxQueuedItemMoves = 128;

    bool SamePlace(const Pose& a, const Pose& b);
    bool Within(const Pose& a, const Pose& b, float distance);

    inline bool IsDowned(float health, float maxHealth) { return maxHealth > 0 && health <= 0; }

    enum class Role { None, Host, Friend };
    enum class FriendState { Connecting, WaitingForWelcome, FixingMods, Joined, Rejected, Disconnected };

    struct Player
    {
        uint8_t id = 0;
        std::string name;
        bool joined = false;
        uint32_t connectedAt = 0;
        bool fixingMods = false;
        std::string from;
        bool awaitingApproval = false;
        bool wantsFiles = false;
        uint32_t askId = 0;
        uint32_t askedAt = 0;
        uint32_t explosionsSince = 0, explosions = 0;
        uint32_t itemMovesSince = 0, itemMoves = 0;
        net::DesyncReport desyncs;
        ChatLimiter chatLimit;
        VoiceLimiter voiceLimit;
        bool fighting = false;
        uint32_t playerHitsSince = 0, playerHits = 0;
        uint32_t lastAsk = 0;
    };

    struct RemotePlayer
    {
        std::string name;
        Smoother motion;
        std::vector<uint32_t> gear;
        uint32_t gearChanges = 0;
        float health = 0, maxHealth = 0;
        bool Downed() const { return IsDowned(health, maxHealth); }
    };

    struct RemoteNpc
    {
        Smoother motion;
        uint32_t heardAt = 0;
        uint32_t stateTime = 0;
        bool dead = false, down = false, sitting = false;
        uint16_t limbs = 0;
        uint8_t pile = 0;
        float health = 0, maxHealth = 0;
        uint32_t idle = 0;
        uint32_t weapon = 0;
        uint32_t base = 0;
        std::vector<net::WornItem> gear;
        uint32_t gearChanges = 0;
    };

    struct HitReport
    {
        uint8_t playerId = 0;
        uint32_t targetRef = 0;
        float damage = 0;
    };

    struct AttackReport
    {
        uint8_t playerId = 0;
        uint8_t shots = 0;
        bool release = false;
    };

    class Coop
    {
    public:
        void SetMods(ModList mods) { m_mods = std::move(mods); m_modsPrint = ModsFingerprint(m_mods); }
        void SetModFolders(ModFolders folders) { m_modFolders = std::move(folders); }
        bool Host(uint16_t port, const std::string& myName);
        bool Join(const std::string& address, uint16_t port, const std::string& myName, bool checkModsOnly = false);
        void HostOn(std::unique_ptr<net::Transport> started, const std::string& myName);
        bool HostBoth(uint16_t port, std::unique_ptr<net::Transport> started, const std::string& myName);
        void JoinOn(std::unique_ptr<net::Transport> started, const std::string& myName, bool checkModsOnly = false);
        bool ModsMatched() const { return m_modsMatched; }
        const net::ModsMatch& MatchedHost() const { return m_matchedHost; }
        void SetCurrentSave(const std::string& save) { m_currentSave = save; }
        void SetQuestMods(const std::vector<std::string>& mods);
        void SetAutoApprove(bool on) { m_autoApprove = on; }
        std::vector<JoinAsk> TakeJoinAsks() { return std::move(m_joinAsks); }
        void AnswerJoin(uint32_t askId, bool yes);
        bool StillAsking(uint32_t askId) const;
        bool TakeAskingHost() { bool b = m_askingHost; m_askingHost = false; return b; }
        const std::vector<std::string>& QuestMods() const { return m_questMods; }
        void Stop();
        void Update();

        Role GetRole() const { return m_role; }
        FriendState GetFriendState() const { return m_friendState; }
        uint8_t MyPlayerId() const { return m_myId; }
        bool InGame() const { return m_role == Role::Host || m_friendState == FriendState::Joined; }

        void SetMyPose(const Pose& pose) { m_myPose = pose; m_hasPose = true; }
        void SetMyGear(std::vector<uint32_t> items);
        void ReportHit(uint32_t targetRef, float damage);
        void ReportPile(uint32_t hostRef, uint8_t kind) const;
        std::vector<net::NpcPile> TakePiles() { return std::move(m_piles); }
        void ReportMine(uint32_t ref, uint8_t state);
        void SetFighting(uint8_t playerId, bool fighting);
        bool HostSaysFighting() const { return m_hostSaysFighting; }
        struct MineFrom { uint8_t playerId; net::MineChange change; };
        std::vector<MineFrom> TakeMines() { return std::move(m_mines); }
        std::vector<HitReport> TakeHits() { return std::move(m_hits); }
        void Attack(uint8_t shots, bool release = false);
        void ShareExplosion(const net::ExplosionAt& e);
        void SawDesync(const Disagreement& d);
        void SetCompanions(const std::vector<net::CompanionState>& companions);
        struct CompanionOrderFrom { uint8_t playerId; net::CompanionOrder order; };
        std::vector<CompanionOrderFrom> TakeCompanionOrders() { return std::move(m_companionOrders); }
        bool CompanionLeader(uint32_t hostRef, uint8_t& leader) const;
        void OrderCompanion(uint32_t hostRef, net::CompanionOrderKind order);
        void SharePlayerCommand(const net::PlayerCommand& c);
        void KilledByScript();
        void ShareRadioLine(net::RadioLine line, uint32_t startedAt);
        struct ChatHeard { uint8_t player = 0; std::string name, text; VoiceStyle style; };
        void SendChat(const std::string& text, VoiceStyle style = {});
        std::vector<ChatHeard> TakeChat() { return std::move(m_chat); }
        struct VoiceHeard { uint8_t player = 0; net::Bytes data; };
        void SendVoice(const net::Bytes& data);
        std::vector<VoiceHeard> TakeVoice() { return std::move(m_voice); }
        void SetMyLooks(const net::PlayerLooks& looks);
        const std::map<uint8_t, net::PlayerLooks>& Looks() const { return m_looks; }
        uint32_t LooksChanges() const { return m_looksChanges; }
        void SetMyAnim(const std::string& path);
        const std::map<uint8_t, std::string>& Anims() const { return m_anims; }
        uint32_t AnimChanges() const { return m_animChanges; }
        void SetMyModAnims(const std::vector<std::string>& paths);
        const std::map<uint8_t, std::vector<std::string>>& ModAnims() const { return m_modAnims; }
        std::map<uint32_t, net::RadioLine> TakeRadioLines() { return std::move(m_radioLines); }
        std::vector<net::PlayerCommand> TakePlayerCommands() { return std::move(m_playerCommands); }
        std::vector<std::string> TakeDesyncNotices() { return std::move(m_desyncNotices); }
        std::vector<net::ExplosionAt> TakeExplosions() { return std::move(m_explosions); }
        std::vector<AttackReport> TakeAttacks() { return std::move(m_attacks); }
        void SetMyHealth(float health, float maxHealth);
        void DamagePlayer(uint8_t playerId, float amount);
        void SetSessionMode(const SaveMode& mode);
        const SaveMode& SessionMode() const { return m_mode; }
        void ReportPlayerHit(uint8_t target, float damage);
        float TakeDamage() { float d = m_damage; m_damage = 0; return d; }
        void Revive(uint8_t playerId);
        bool TakeRevived() { bool r = m_revived; m_revived = false; return r; }
        void Drag(uint8_t target, bool on);
        int DraggedBy(uint8_t target) const;
        int Dragging(uint8_t dragger) const;
        bool TakeGameOver(net::GameOver& out);
        bool TakeTravel(Pose& out, uint8_t& leader);
        void ForgetMyPlace();
        void SetNpcs(uint32_t space, std::vector<net::NpcState> npcs);
        const std::map<uint32_t, RemoteNpc>& Npcs() const { return m_npcs; }
        void NpcAttacked(uint32_t ref, uint8_t shots, bool reloaded = false, bool released = false);
        void SetNpcGear(uint32_t ref, std::vector<net::WornItem> items);
        std::vector<net::NpcAttack> TakeNpcAttacks() { return std::move(m_npcAttacks); }
        void SetDoors(std::vector<net::DoorState> doors);
        std::vector<net::DoorState> TakeDoorUses() { return std::move(m_doorUses); }
        std::vector<net::DoorState> ObserveDoors(uint32_t now, const std::vector<LocalDoor>& local);
        std::vector<uint32_t> ViewedContainers() const;
        void SetContainerContents(uint32_t ref, const std::vector<net::ItemCount>& items);
        struct ItemChangeFrom { uint8_t playerId; net::ContainerChange change; };
        std::vector<ItemChangeFrom> TakeContainerChanges() { return std::move(m_itemChanges); }
        void ContainerChangeDone(uint8_t playerId, uint32_t seq, const net::ItemCount& refund);
        void ViewContainer(uint32_t hostRef, bool open = true);
        std::vector<net::ItemCount> LookAtContainer(const std::vector<net::ItemCount>& local);
        void ContainerSettled(const std::vector<net::ItemCount>& local) { m_friendContainer.Settle(local); }
        std::vector<net::ItemCount> TakeItemRefunds() { return std::move(m_refunds); }
        void SetWorldItems(const std::vector<net::ItemState>& items);
        struct ItemTakeFrom { uint8_t playerId; net::ItemTake take; };
        struct ItemDropFrom { uint8_t playerId; net::ItemDrop drop; };
        std::vector<ItemTakeFrom> TakeItemTakes() { return std::move(m_itemTakes); }
        std::vector<ItemDropFrom> TakeItemDrops() { return std::move(m_itemDrops); }
        struct ItemMoveFrom { uint8_t playerId; net::ItemMove move; };
        std::vector<ItemMoveFrom> TakeItemMoves() { return std::move(m_itemMoves); }
        void ReportBodyMove(const net::BodyMove& m);
        void ReportBodyLimbs(const net::BodyLimbs& m);
        void ShareBodyPose(const net::BodyPose& m, bool reliable);
        std::vector<net::BodyPose> TakeBodyPoses()
        {
            std::vector<net::BodyPose> out;
            out.swap(m_bodyPoses);
            return out;
        }
        std::vector<net::BodyLimbs> TakeBodyLimbs()
        {
            std::vector<net::BodyLimbs> out;
            out.swap(m_bodyLimbs);
            return out;
        }
        std::vector<net::BodyMove> TakeBodyMoves()
        {
            std::vector<net::BodyMove> out;
            out.swap(m_bodyMoves);
            return out;
        }
        void RefundItems(uint8_t playerId, const net::ItemCount& refund);
        ItemActions LookAtItems(const std::vector<LocalItem>& local, bool sameArea);
        void ItemMade(uint32_t hostRef, uint32_t ourRef) { m_friendItems.Made(hostRef, ourRef); }
        void SendQuestStates(const std::vector<net::QuestState>& quests);
        uint32_t Joins() const { return m_joins; }
        void SendRefEnables(const std::vector<net::RefEnable>& changes);
        std::vector<net::RefEnable> TakeRefEnables() { return std::move(m_refEnables); }
        void ShareProgress(int32_t xp, int32_t karma);
        void SetMyXp(int32_t xp) { m_myXp = xp < 0 ? 0 : xp > net::kMaxXp ? net::kMaxXp : xp; }
        bool TakeHostXp(int32_t& xp)
        {
            if (!m_hostXpWaiting)
                return false;
            m_hostXpWaiting = false;
            xp = m_hostXp;
            return true;
        }
        std::vector<net::Progress> TakeProgress() { return std::move(m_progress); }
        void SendReputations(const std::vector<net::Reputation>& reps);
        void ShareReputationGains(const std::vector<net::ReputationGain>& gains);
        std::vector<std::pair<uint8_t, net::ReputationGain>> TakeReputationGains() { return std::move(m_repGains); }
        std::vector<net::Reputation> TakeReputations() { return std::move(m_reputations); }
        void ShareTravelMarkers(const std::vector<uint32_t>& refs);
        void AskTeleport(uint8_t mover, uint8_t target);
        std::vector<uint8_t> TakeTeleports() { return std::move(m_teleports); }
        std::vector<uint32_t> TakeTravelMarkers() { return std::move(m_travelMarkers); }
        void ShareProgressFor(uint8_t playerId, int32_t xp, int32_t karma);
        void SetMyConversation(bool talking, uint32_t npc);
        const TalkLock& Talk() const { return m_talk; }
        bool MayTalk() const { return m_talk.MayTalk(m_myId); }
        void SendDialogueLine(const net::DialogueLine& line);
        void ShareSubtitle(net::Subtitle s);
        std::vector<net::Subtitle> TakeSubtitles() { return std::move(m_subtitles); }
        struct DialogueLineFrom { uint8_t playerId; net::DialogueLine line; };
        std::vector<DialogueLineFrom> TakeDialogueLines() { return std::move(m_dialogueLines); }
        void AskToWait(uint8_t hours, bool sleep);
        void AnswerWait(bool yes);
        struct WaitEvent
        {
            enum class Kind { Asked, Start, Refused } kind;
            uint8_t requester = 0;
            uint8_t hours = 0;
            bool sleep = false;
            uint8_t refusedBy = net::kNobody;
        };
        std::vector<WaitEvent> TakeWaitEvents() { return std::move(m_waitEvents); }
        void SetMyGameTime(const net::GameTime& t) { m_myGameTime = t; m_hasGameTime = true; }
        bool TakeGameTime(net::GameTime& out)
        {
            if (!m_hostTimeNew)
                return false;
            out = m_hostTime;
            m_hostTimeNew = false;
            return true;
        }
        bool Frozen() const { return m_frozen && !m_expecting && !m_hasTravel; }
        uint8_t FrozenFor() const { return m_frozenFor; }
        void LoadedHostSave() { SendArrived(); }
        void ShareSave(const std::string& file, bool load);
        std::vector<net::SaveOrder> TakeSaveOrders() { return std::move(m_saveOrders); }
        void AskHostToSave(const std::string& ourSave);
        void AskHostToLoad(const std::string& hostSave);
        bool TakeLoadRefused() { bool r = m_loadRefused; m_loadRefused = false; return r; }
        struct SaveAsk { uint8_t playerId; bool load; std::string name; };
        std::vector<SaveAsk> TakeSaveAsks() { return std::move(m_saveAsks); }
        void RefuseLoad(uint8_t playerId);
        std::vector<net::QuestState> TakeQuestStates() { return std::move(m_questStates); }
        std::string TakeRejectReason() { return std::move(m_rejectReason); }
        std::string TakeModReport() { return std::move(m_modReport); }
        std::string TakeModQuestion() { return std::move(m_modQuestion); }
        void AnswerModFix(bool yes);
        bool DownloadingMods() const { return m_downloading; }
        void CancelModFix() { if (m_downloading) m_download.Fail("you cancelled"); }
        const ModDownload& Download() const { return m_download; }
        std::string TakeModFixResult() { return std::move(m_modFixResult); }
        std::string NameOf(uint8_t playerId) const;
        void SetBleedOutMs(uint32_t ms) { m_bleedOutMs = ms; }
        void SetTravelCooldownMs(uint32_t ms) { m_travelCooldownMs = ms; }
        const std::map<uint8_t, RemotePlayer>& Remotes() const { return m_remotes; }
        const std::map<net::PeerId, Player>& Players() const { return m_players; }
        net::Session& GetSession() { return m_enet; }

    private:
        void HostReceived(net::PeerId peer, const net::Bytes& data);
        void FriendReceived(const net::Bytes& data);
        void Kick(net::PeerId peer, const std::string& reason);
        void SendModList(net::PeerId peer);
        void Admit(net::PeerId peer, Player& player);
        void WelcomePlayer(net::PeerId peer, Player& player);
        void CheckHostMods();
        void OfferModFix();
        void UpdateDownload();
        void RemovePlayer(net::PeerId peer);
        void SendToJoined(const net::Bytes& data, bool reliable, net::PeerId except = 0);
        void SendMyPose(uint32_t now);
        void SendMyGear();
        void SendMyHealth(uint32_t now);
        void SendNpcs(uint32_t now);
        void SendDoors(uint32_t now);
        void SendItems(uint32_t now);
        void ForgetOldNpcs(uint32_t now);
        void HeardNpcs(const net::NpcStates& m);
        void SetRemoteGear(uint8_t id, std::vector<uint32_t> items);
        void QueueAttack(uint8_t playerId, uint8_t shots, bool release);
        void CheckDowned(uint32_t now);
        void EndGame(net::GameOverReason reason, uint8_t playerId);
        void HostRevive(uint8_t reviver, uint8_t target);
        void HostDrag(uint8_t dragger, uint8_t target, bool on);
        void EndDrag(uint8_t target);
        bool HealthOf(uint8_t playerId, float& health, float& maxHealth) const;
        bool PoseOf(uint8_t playerId, Pose& out) const;
        void CheckMyTravel(uint32_t now);
        void GroupTravels(uint8_t leader, const Pose& to, uint32_t now);
        void SendTravel(uint8_t toPlayer, uint8_t leader, const Pose& to);
        void PullMe(uint8_t leader, const Pose& to);
        uint8_t FreePlayerId() const;

        net::Session m_enet;
        std::unique_ptr<net::Transport> m_other;
        net::Transport* m_net = &m_enet;
        Role m_role = Role::None;
        std::string m_myName;
        uint8_t m_myId = 0;
        FriendState m_friendState = FriendState::Disconnected;
        std::map<net::PeerId, Player> m_players;
        uint32_t m_lastPingLog = 0;
        std::map<uint8_t, RemotePlayer> m_remotes;
        Pose m_myPose, m_sentPose;
        bool m_hasPose = false;
        uint32_t m_lastPoseSent = 0;
        std::vector<uint32_t> m_myGear;
        bool m_gearToSend = false;
        std::vector<HitReport> m_hits;
        std::vector<AttackReport> m_attacks;
        float m_myHealth = 0, m_myMaxHealth = 0;
        bool m_healthToSend = false;
        uint32_t m_lastHealthSent = 0;
        float m_damage = 0;
        bool m_hostSaysFighting = false;
        SaveMode m_mode;
        bool m_revived = false;
        bool m_gameOver = false;
        net::GameOver m_gameOverInfo;
        std::map<uint8_t, uint32_t> m_downedSince;
        std::map<uint8_t, uint8_t> m_drags;
        std::map<uint8_t, uint32_t> m_dragSince;
        bool m_gameOverSent = false;
        uint32_t m_bleedOutMs = kBleedOutMs;
        Pose m_place;
        bool m_hasPlace = false;
        bool m_regroup = false;
        Pose m_expect;
        bool m_expecting = false;
        Pose m_travel;
        uint8_t m_travelLeader = 0;
        bool m_hasTravel = false;
        Pose m_groupPlace;
        uint8_t m_groupLeader = 0;
        uint32_t m_groupMovedAt = 0;
        bool m_hasGroupPlace = false;
        uint32_t m_travelCooldownMs = kTravelCooldownMs;
        struct SentNpc { net::NpcState state; uint32_t at = 0; };
        uint32_t m_npcSpace = 0;
        std::vector<net::NpcState> m_myNpcs;
        std::map<uint32_t, SentNpc> m_sentNpcs;
        uint32_t m_lastNpcsSent = 0;
        uint32_t m_npcUpdatesSent = 0, m_npcBytesSent = 0;
        uint32_t m_playerCommandsSent = 0, m_playerCommandBytesSent = 0;
        std::map<uint32_t, net::NpcAttack> m_npcShots;
        struct NpcGearSent { std::vector<net::WornItem> items; uint32_t sentAt = 0; bool changed = true; };
        std::map<uint32_t, NpcGearSent> m_npcGear;
        std::vector<net::NpcAttack> m_npcAttacks;
        std::map<uint32_t, RemoteNpc> m_npcs;
        bool m_warnedNpcs = false;
        std::vector<net::DoorState> m_myDoors;
        std::map<uint32_t, net::DoorState> m_sentDoors;
        uint32_t m_lastDoorsSent = 0;
        std::vector<net::DoorState> m_doorUses;
        FriendDoors m_friendDoors;
        struct HostView
        {
            uint32_t ref = 0;
            uint32_t seen = 0;
            bool sent = false;
            uint32_t sentSeen = 0;
            std::vector<net::ItemCount> sentItems;
        };
        std::map<uint8_t, HostView> m_views;
        std::vector<ItemChangeFrom> m_itemChanges;
        FriendContainer m_friendContainer;
        std::vector<net::ItemCount> m_refunds;
        std::vector<net::ItemState> m_myItems;
        std::map<uint32_t, net::ItemState> m_sentItems;
        uint32_t m_lastItemsSent = 0;
        bool m_itemsNew = false;
        std::vector<ItemTakeFrom> m_itemTakes;
        std::vector<ItemDropFrom> m_itemDrops;
        std::vector<ItemMoveFrom> m_itemMoves;
        std::vector<net::BodyMove> m_bodyMoves;
        std::vector<net::BodyLimbs> m_bodyLimbs;
        std::vector<net::BodyPose> m_bodyPoses;
        std::map<uint32_t, uint32_t> m_lastMoveSent;
        DesyncTracker m_desyncs;
        std::vector<net::CompanionState> m_myCompanions;
        bool m_companionsChanged = false;
        uint32_t m_lastCompanionsSent = 0;
        std::vector<CompanionOrderFrom> m_companionOrders;
        std::map<uint32_t, uint8_t> m_companions;
        std::vector<net::PlayerCommand> m_playerCommands;
        std::map<uint32_t, net::RadioLine> m_radioLines;
        uint32_t m_nextRadioResend = 0;
        std::map<uint32_t, uint32_t> m_radioStartedAt;
        std::vector<ChatHeard> m_chat;
        std::vector<VoiceHeard> m_voice;
        std::vector<net::NpcPile> m_piles;
        std::vector<MineFrom> m_mines;
        net::PlayerLooks m_myLooks;
        bool m_hasMyLooks = false, m_myLooksSent = false;
        std::map<uint8_t, net::PlayerLooks> m_looks;
        uint32_t m_looksChanges = 0;
        std::string m_myAnim;
        bool m_myAnimSent = true;
        std::map<uint8_t, std::string> m_anims;
        uint32_t m_animChanges = 0;
        void HeardAnim(uint8_t player, const std::string& path);
        std::vector<std::string> m_myModAnims;
        bool m_myModAnimsSent = true;
        std::map<uint8_t, std::vector<std::string>> m_modAnims;
        void HeardModAnims(uint8_t player, const std::vector<std::string>& paths);
        int32_t m_myXp = 0;
        int32_t m_hostXp = 0;
        bool m_hostXpWaiting = false;
        void SendCompanions(uint32_t now);
        DesyncCounts m_desyncsSent;
        uint32_t m_nextDesyncSweep = 0;
        std::vector<std::string> m_desyncNotices;
        void SweepDesyncs(uint32_t now);
        FriendItems m_friendItems;
        uint32_t m_joins = 0;
        ModList m_mods;
        net::Hash m_modsPrint{};
        ModList m_hostMods;
        bool m_hostModsDone = false;
        bool m_modsDiffer = false;
        bool m_checkOnly = false;
        bool m_modsMatched = false;
        net::ModsMatch m_matchedHost;
        std::string m_currentSave;
        std::vector<std::string> m_questMods;
        bool m_autoApprove = true;
        std::vector<JoinAsk> m_joinAsks;
        uint32_t m_nextAskId = 1;
        std::set<std::string> m_approved;
        bool m_askingHost = false;
        std::vector<net::ExplosionAt> m_explosions;
        uint32_t m_hostExplosionsSince = 0, m_hostExplosions = 0;
        static bool WithinLimit(uint32_t& since, uint32_t& count, uint32_t most);
        std::string m_modReport;
        ModFolders m_modFolders;
        ModServer m_modServer;
        std::vector<ModDownload::Wanted> m_modsWanted;
        std::vector<std::string> m_modsExtra;
        std::string m_modQuestion;
        ModDownload m_download;
        bool m_downloading = false;
        std::string m_modFixResult;
        std::string m_rejectReason;
        std::vector<net::QuestState> m_questStates;
        std::vector<net::RefEnable> m_refEnables;
        std::vector<net::Progress> m_progress;
        std::vector<net::Reputation> m_reputations;
        std::vector<uint32_t> m_travelMarkers;
        std::vector<uint8_t> m_teleports;
        bool IsPlayer(uint8_t id) const;
        std::vector<std::pair<uint8_t, net::ReputationGain>> m_repGains;
        TalkLock m_talk;
        void SendTalk(net::PeerId to);
        std::vector<DialogueLineFrom> m_dialogueLines;
        std::vector<net::Subtitle> m_subtitles;
        LoadGate m_loads;
        bool m_frozen = false;
        uint8_t m_frozenFor = 0;
        void ExpectLoads(uint8_t except, uint32_t now);
        void UpdateFreeze(uint32_t now);
        void SendArrived();
        std::vector<net::SaveOrder> m_saveOrders;
        bool m_loadRefused = false;
        std::vector<SaveAsk> m_saveAsks;
        uint32_t m_lastSaveAsk = 0;
        WaitVote m_wait;
        std::vector<WaitEvent> m_waitEvents;
        void StartWaitVote(uint8_t requester, uint8_t hours, bool sleep, net::PeerId from);
        void CheckWaitVote(uint32_t now);
        void SendWaitResult(const net::WaitResult& m);
        net::GameTime m_myGameTime;
        bool m_hasGameTime = false;
        uint32_t m_lastGameTimeSent = 0;
        net::GameTime m_hostTime;
        bool m_hostTimeNew = false;
    };
}
