// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/chat.h"
#include "coop/game_mode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace net
{
    constexpr uint32_t kProtocolVersion = 72;
    constexpr size_t kMaxNameLength = 32;
    constexpr size_t kMaxReasonLength = 200;
    constexpr size_t kMaxMessageSize = 64 * 1024;

    enum class MsgType : uint8_t
    {
        Hello = 1,
        Welcome = 2,
        Reject = 3,
        PlayerJoined = 4,
        PlayerLeft = 5,
        PlayerState = 6,
        PlayerGear = 7,
        Hit = 8,
        PlayerHealth = 9,
        Damage = 10,
        Revive = 11,
        GameOver = 12,
        Attack = 13,
        Travel = 14,
        Regroup = 15,
        NpcStates = 16,
        NpcAttacks = 17,
        NpcGear = 18,
        DoorStates = 19,
        DoorUse = 20,
        ContainerView = 21,
        ContainerChange = 22,
        ContainerContents = 23,
        ItemStates = 25,
        ItemTake = 26,
        ItemDrop = 27,
        QuestStates = 28,
        RefEnables = 29,
        Progress = 30,
        Reputations = 31,
        TalkState = 32,
        DialogueLine = 33,
        Subtitle = 34,
        ItemRefund = 24,
        WaitAsk = 35,
        WaitVote = 36,
        WaitAnswer = 37,
        WaitResult = 38,
        GameTime = 39,
        SaveOrder = 40,
        LoadRefused = 41,
        Arrived = 42,
        Freeze = 43,
        ModFiles = 44,
        ModsDiffer = 45,
        ModFileAsk = 46,
        ModPiece = 47,
        AskingHost = 49,
        ExplosionAt = 50,
        ItemMoves = 51,
        DesyncReport = 52,
        CompanionStates = 53,
        CompanionOrder = 54,
        PlayerCommand = 55,
        KilledByScript = 56,
        RadioLine = 57,
        ChatLine = 58,
        PlayerHit = 60,
        VoiceData = 59,
        PlayerLooks = 61,
        NpcPile = 62,
        Fighting = 64,
        ReputationGain = 65,
        TravelMarkers = 66,
        Teleport = 67,
        PlayerAnim = 68,
        PlayerModAnims = 69,
        Drag = 70,
        Mode = 71,
        BodyMove = 74,
        BodyLimbs = 75,
        BodyPose = 83,
        MineChange = 63,
        ModsMatch = 48,
    };

    using Bytes = std::vector<uint8_t>;

    class Writer
    {
    public:
        explicit Writer(MsgType type) { U8(static_cast<uint8_t>(type)); }
        void U8(uint8_t v) { m_data.push_back(v); }
        void U16(uint16_t v);
        void U32(uint32_t v);
        void F32(float v);
        void String(std::string_view s, size_t maxLength);
        void Block(const void* data, size_t size) { auto p = static_cast<const uint8_t*>(data); m_data.insert(m_data.end(), p, p + size); }
        Bytes Take() { return std::move(m_data); }

    private:
        Bytes m_data;
    };

    class Reader
    {
    public:
        Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
        explicit Reader(const Bytes& b) : Reader(b.data(), b.size()) {}

        bool Type(MsgType& out);
        bool U8(uint8_t& out);
        bool U16(uint16_t& out);
        bool U32(uint32_t& out);
        bool F32(float& out);
        bool String(std::string& out, size_t maxLength);
        bool Block(void* out, size_t size) { return Raw(out, size); }
        bool AtEnd() const { return m_pos == m_size; }

    private:
        bool Raw(void* out, size_t n);
        const uint8_t* m_data;
        size_t m_size;
        size_t m_pos = 0;
    };

    using Hash = std::array<uint8_t, 32>;
    struct Hello
    {
        uint32_t version = 0;
        std::string name;
        bool hasMods = false;
        Hash mods{};
        bool checkOnly = false;
    };

    enum class ModKind : uint8_t { Plugin = 0, Archive = 1, Dll = 2 };
    struct ModFile
    {
        std::string name;
        ModKind kind = ModKind::Plugin;
        uint64_t size = 0;
        bool hashed = false;
        Hash hash{};
    };
    constexpr size_t kMaxModFiles = 1024;
    constexpr size_t kModFilesPerMessage = 200;
    constexpr size_t kMaxModFileNameLength = 200;
    constexpr uint64_t kMaxModFileSize = 1ull << 40;
    bool IsPlainModFileName(const std::string& name, ModKind kind);
    struct ModFiles
    {
        bool last = false;
        std::vector<ModFile> files;
    };
    struct Welcome
    {
        uint8_t playerId = 0;
        std::string hostName;
        std::vector<std::string> questMods;
        coop::SaveMode mode;
        int32_t hostXp = 0;
    };
    constexpr int32_t kMaxXp = 10000000;
    constexpr size_t kMaxQuestMods = 64;
    struct Reject { std::string reason; };
    struct PlayerJoined { uint8_t playerId = 0; std::string name; };
    struct PlayerLeft { uint8_t playerId = 0; };
    struct PlayerState
    {
        uint8_t playerId = 0;
        uint32_t timeMs = 0;
        uint32_t space = 0;
        float x = 0, y = 0, z = 0;
        float heading = 0;
        uint8_t flags = 0;
        uint16_t moves = 0;
        float pitch = 0;
        uint16_t runSpeed = 0;
    };
    constexpr uint8_t kKnownPlayerStateFlags = 0x03;
    constexpr float kMaxPitch = 1.6f;
    constexpr size_t kMaxGearItems = 32;
    constexpr uint8_t kNoCondition = 255;
    struct PlayerGear
    {
        uint8_t playerId = 0;
        std::vector<uint32_t> items;
    };

    struct Hit
    {
        uint8_t playerId = 0;
        uint32_t targetRef = 0;
        float damage = 0;
    };

    constexpr float kMaxHealth = 100000;
    struct PlayerHealth
    {
        uint8_t playerId = 0;
        float health = 0;
        float maxHealth = 0;
    };
    struct Damage
    {
        uint8_t playerId = 0;
        float amount = 0;
    };
    struct Revive
    {
        uint8_t playerId = 0;
    };
    constexpr uint8_t kMaxShots = 20;
    struct Attack
    {
        uint8_t playerId = 0;
        uint8_t shots = 1;
        bool release = false;
    };
    struct Travel
    {
        uint8_t playerId = 0;
        uint32_t space = 0;
        float x = 0, y = 0, z = 0;
        float heading = 0;
    };
    constexpr size_t kMaxNpcsPerMessage = 40;
    struct NpcState
    {
        uint32_t ref = 0;
        float x = 0, y = 0, z = 0;
        float heading = 0;
        uint8_t flags = 0;
        uint16_t moves = 0;
        float health = 0;
        float maxHealth = 0;
        uint32_t idle = 0;
        uint32_t weapon = 0;
        uint32_t base = 0;
        uint16_t limbs = 0;
        uint8_t pile = 0;
    };
    constexpr uint8_t kPileNone = 0, kPileGoo = 1, kPileAsh = 2, kPileAlien = 3, kPileLast = 3;
    struct NpcPile
    {
        uint32_t ref = 0;
        uint8_t kind = 0;
    };
    Bytes Encode(const NpcPile& m);
    bool Decode(Reader& r, NpcPile& m);
    constexpr uint8_t kMineOff = 1, kMineGone = 2, kMineBlown = 3;
    struct MineChange
    {
        uint32_t ref = 0;
        uint8_t state = 0;
    };
    Bytes Encode(const MineChange& m);
    bool Decode(Reader& r, MineChange& m);
    struct Fighting
    {
        bool fighting = false;
    };
    Bytes Encode(const Fighting& m);
    bool Decode(Reader& r, Fighting& m);
    constexpr uint8_t kNpcDead = 0x80;
    constexpr uint8_t kNpcDown = 0x40;
    constexpr uint8_t kNpcSitting = 0x20;
    constexpr uint8_t kKnownNpcFlags = kKnownPlayerStateFlags | kNpcDead | kNpcDown | kNpcSitting;
    struct NpcStates
    {
        uint32_t timeMs = 0;
        uint32_t space = 0;
        std::vector<NpcState> npcs;
    };
    bool IsSharedRef(uint32_t ref);
    bool IsMadeWhilePlaying(uint32_t id);
    struct NpcAttack
    {
        uint32_t ref = 0;
        uint8_t shots = 1;
        bool reload = false;
        bool release = false;
    };
    struct NpcAttacks
    {
        std::vector<NpcAttack> attacks;
    };

    enum class GameOverReason : uint8_t { BledOut = 0, AllDown = 1, Killed = 2 };
    struct GameOver
    {
        GameOverReason reason = GameOverReason::AllDown;
        uint8_t playerId = 0;
    };

    Bytes Encode(const Hello& m);
    Bytes Encode(const Welcome& m);
    Bytes Encode(const Reject& m);
    Bytes Encode(const PlayerJoined& m);
    Bytes Encode(const PlayerLeft& m);
    Bytes Encode(const PlayerState& m);
    Bytes Encode(const PlayerGear& m);
    Bytes Encode(const Hit& m);
    Bytes Encode(const PlayerHealth& m);
    Bytes Encode(const Damage& m);
    Bytes Encode(const Revive& m);
    Bytes Encode(const GameOver& m);
    Bytes Encode(const Attack& m);
    Bytes Encode(const Travel& m);
    Bytes EncodeRegroup();
    Bytes Encode(const NpcStates& m);
    Bytes Encode(const NpcAttacks& m);

    bool Decode(Reader& r, Hello& m);
    bool Decode(Reader& r, Welcome& m);
    bool Decode(Reader& r, Reject& m);
    bool Decode(Reader& r, PlayerJoined& m);
    bool Decode(Reader& r, PlayerLeft& m);
    bool Decode(Reader& r, PlayerState& m);
    bool Decode(Reader& r, PlayerGear& m);
    bool Decode(Reader& r, Hit& m);
    bool Decode(Reader& r, PlayerHealth& m);
    bool Decode(Reader& r, Damage& m);
    bool Decode(Reader& r, Revive& m);
    bool Decode(Reader& r, GameOver& m);
    bool Decode(Reader& r, Attack& m);
    bool Decode(Reader& r, Travel& m);
    bool Decode(Reader& r, NpcStates& m);
    struct WornItem
    {
        uint32_t item = 0;
        uint8_t health = kNoCondition;
        bool operator==(const WornItem& o) const { return item == o.item && health == o.health; }
    };
    struct NpcGear
    {
        uint32_t ref = 0;
        std::vector<WornItem> items;
    };
    Bytes Encode(const NpcGear& m);
    bool Decode(Reader& r, NpcGear& m);
    bool Decode(Reader& r, NpcAttacks& m);

    constexpr size_t kMaxDoorsPerMessage = 64;
    constexpr uint8_t kDoorOpen = 0x01;
    constexpr uint8_t kDoorLocked = 0x02;
    struct DoorState
    {
        uint32_t ref = 0;
        uint8_t flags = 0;
        uint8_t lockLevel = 0;
        bool operator==(const DoorState& o) const
        {
            return ref == o.ref && flags == o.flags && lockLevel == o.lockLevel;
        }
        bool operator!=(const DoorState& o) const { return !(*this == o); }
    };
    struct DoorStates
    {
        std::vector<DoorState> doors;
    };
    bool IsDoorRef(uint32_t ref);
    Bytes Encode(const DoorStates& m);
    bool Decode(Reader& r, DoorStates& m);
    Bytes EncodeDoorUse(const DoorState& m);
    bool DecodeDoorUse(Reader& r, DoorState& m);

    constexpr size_t kMaxItemKinds = 500;
    constexpr int32_t kMaxItemCount = 1000000;
    struct ItemCount
    {
        uint32_t item = 0;
        int32_t count = 0;
        uint8_t health = kNoCondition;
        uint8_t mods = 0;
        bool SameKind(const ItemCount& o) const { return item == o.item && health == o.health && mods == o.mods; }
        bool operator==(const ItemCount& o) const { return SameKind(o) && count == o.count; }
    };
    struct ContainerChange
    {
        uint32_t ref = 0;
        uint32_t seq = 0;
        ItemCount change;
    };
    struct ContainerContents
    {
        uint32_t ref = 0;
        uint32_t seen = 0;
        std::vector<ItemCount> items;
    };
    bool IsContainerRef(uint32_t ref);
    Bytes EncodeContainerView(uint32_t ref);
    bool DecodeContainerView(Reader& r, uint32_t& ref);
    Bytes Encode(const ContainerChange& m);
    bool Decode(Reader& r, ContainerChange& m);
    Bytes Encode(const ContainerContents& m);
    bool Decode(Reader& r, ContainerContents& m);
    constexpr size_t kMaxItemsPerMessage = 64;
    struct ItemState
    {
        uint32_t ref = 0;
        uint32_t base = 0;
        int32_t count = 1;
        bool present = true;
        float x = 0, y = 0, z = 0;
        float rx = 0, ry = 0, rz = 0;
        bool held = false;
        uint32_t parent = 0;
        bool parentOn = false;
        uint8_t health = kNoCondition;
        uint8_t mods = 0;
    };
    constexpr float kMaxItemAngle = 7;
    struct ItemStates
    {
        std::vector<ItemState> items;
    };
    struct ItemTake
    {
        uint32_t ref = 0;
        uint32_t base = 0;
        int32_t count = 1;
        uint8_t health = kNoCondition;
        uint8_t mods = 0;
    };
    struct ItemDrop
    {
        uint32_t base = 0;
        int32_t count = 1;
        float x = 0, y = 0, z = 0;
        uint8_t health = kNoCondition;
        uint8_t mods = 0;
    };
    Bytes Encode(const ItemStates& m);
    bool Decode(Reader& r, ItemStates& m);
    Bytes Encode(const ItemTake& m);
    bool Decode(Reader& r, ItemTake& m);
    Bytes Encode(const ItemDrop& m);
    bool Decode(Reader& r, ItemDrop& m);
    constexpr size_t kMaxItemMovesPerMessage = 32;
    struct ItemMove
    {
        uint32_t ref = 0;
        float x = 0, y = 0, z = 0;
        float rx = 0, ry = 0, rz = 0;
        bool held = false;
    };
    // Where a visitor's copy of a body is while they move it, for the host to steer theirs there.
    struct BodyMove
    {
        uint32_t ref = 0;
        float x = 0, y = 0, z = 0;
    };
    Bytes Encode(const BodyMove& m);
    // Limbs a body lost in a visitor's game, for the host to take off theirs too.
    struct BodyLimbs
    {
        uint32_t ref = 0;
        uint16_t limbs = 0;
    };
    Bytes Encode(const BodyLimbs& m);
    bool Decode(Reader& r, BodyLimbs& m);
    // Optional limb syncing: where a body's limbs are, in game units from its torso. Only the bones in the mask are sent.
    constexpr size_t kBodyPoseBones = 12;
    struct BodyPose
    {
        uint32_t ref = 0;
        uint16_t bones = 0;
        int16_t offsets[kBodyPoseBones][3] = {};
    };
    Bytes Encode(const BodyPose& m);
    bool Decode(Reader& r, BodyPose& m);
    bool Decode(Reader& r, BodyMove& m);

    struct ItemMoves
    {
        std::vector<ItemMove> moves;
    };
    Bytes Encode(const ItemMoves& m);
    bool Decode(Reader& r, ItemMoves& m);
    constexpr size_t kMaxQuestsPerMessage = 32;
    constexpr size_t kMaxQuestStages = 255;
    constexpr size_t kMaxQuestObjectives = 255;
    struct QuestObjective
    {
        uint32_t id = 0;
        uint8_t status = 0;
        bool operator==(const QuestObjective& o) const { return id == o.id && status == o.status; }
    };
    struct QuestState
    {
        uint32_t quest = 0;
        uint8_t flags = 0;
        uint8_t stage = 0;
        std::vector<uint8_t> done;
        std::vector<QuestObjective> objectives;
        bool operator==(const QuestState& o) const
        {
            return quest == o.quest && flags == o.flags && stage == o.stage && done == o.done &&
                objectives == o.objectives;
        }
    };
    struct QuestStates
    {
        std::vector<QuestState> quests;
    };
    Bytes Encode(const QuestStates& m);
    bool Decode(Reader& r, QuestStates& m);
    constexpr size_t kMaxEnablesPerMessage = 64;
    struct RefEnable
    {
        uint32_t ref = 0;
        bool enabled = true;
        bool operator==(const RefEnable& o) const { return ref == o.ref && enabled == o.enabled; }
    };
    Bytes Encode(const std::vector<RefEnable>& m);
    bool Decode(Reader& r, std::vector<RefEnable>& m);
    constexpr int32_t kMaxXpGain = 100000;
    constexpr int32_t kMaxKarmaChange = 10000;
    struct Progress
    {
        uint8_t playerId = 0;
        int32_t xp = 0;
        int32_t karma = 0;
    };
    Bytes Encode(const Progress& m);
    bool Decode(Reader& r, Progress& m);
    constexpr size_t kMaxReputationsPerMessage = 64;
    constexpr float kMaxReputation = 100000;
    struct Reputation
    {
        uint32_t form = 0;
        float fame = 0, infamy = 0;
        bool operator==(const Reputation& o) const { return form == o.form && fame == o.fame && infamy == o.infamy; }
    };
    Bytes Encode(const std::vector<Reputation>& m);
    struct ReputationGain
    {
        uint32_t form = 0;
        float fame = 0, infamy = 0;
        bool operator==(const ReputationGain& o) const { return form == o.form && fame == o.fame && infamy == o.infamy; }
    };
    Bytes EncodeGains(const std::vector<ReputationGain>& m);
    bool DecodeGains(Reader& r, std::vector<ReputationGain>& m);
    bool Decode(Reader& r, std::vector<Reputation>& m);
    constexpr size_t kMaxAnimPath = 160;
    struct PlayerAnim
    {
        uint8_t player = 0;
        std::string path;
    };
    Bytes Encode(const PlayerAnim& m);
    bool Decode(Reader& r, PlayerAnim& m);
    constexpr size_t kMaxModAnims = 8;
    struct PlayerModAnims
    {
        uint8_t player = 0;
        std::vector<std::string> paths;
    };
    Bytes Encode(const PlayerModAnims& m);
    bool Decode(Reader& r, PlayerModAnims& m);
    bool IsAnimPath(const std::string& path);
    struct Teleport
    {
        uint8_t mover = 0, target = 0;
    };
    Bytes Encode(const Teleport& m);
    bool Decode(Reader& r, Teleport& m);
    struct Drag
    {
        uint8_t dragger = 0, target = 0;
        bool on = false;
    };
    Bytes Encode(const Drag& m);
    bool Decode(Reader& r, Drag& m);
    struct ModeChange
    {
        coop::SaveMode mode;
    };
    Bytes Encode(const ModeChange& m);
    bool Decode(Reader& r, ModeChange& m);
    constexpr size_t kMaxMarkersPerMessage = 200;
    Bytes EncodeTravelMarkers(const std::vector<uint32_t>& refs);
    bool DecodeTravelMarkers(Reader& r, std::vector<uint32_t>& refs);
    struct TalkState
    {
        uint8_t playerId = 0;
        bool talking = false;
        uint32_t npc = 0;
    };
    Bytes Encode(const TalkState& m);
    bool Decode(Reader& r, TalkState& m);
    struct DialogueLine
    {
        uint32_t info = 0;
        bool onEnd = false;
        uint32_t speaker = 0;
    };
    Bytes Encode(const DialogueLine& m);
    bool Decode(Reader& r, DialogueLine& m);
    constexpr size_t kMaxSubtitleLength = 300;
    constexpr uint8_t kMaxResponse = 255;
    struct Subtitle
    {
        uint8_t playerId = 0;
        uint32_t speaker = 0;
        bool stop = false;
        bool reply = false;
        uint32_t topic = 0;
        uint32_t info = 0;
        uint8_t response = 0;
        std::string text;
    };
    Bytes Encode(const Subtitle& m);
    bool Decode(Reader& r, Subtitle& m);
    constexpr uint8_t kMaxWaitHours = 24;
    constexpr uint8_t kNobody = 0xFF;
    struct WaitAsk { uint8_t hours = 0; bool sleep = false; };
    struct WaitVote { uint8_t requester = 0; uint8_t hours = 0; bool sleep = false; };
    struct WaitAnswer { bool yes = false; };
    struct WaitResult
    {
        uint8_t requester = 0;
        bool passed = false;
        uint8_t hours = 0;
        bool sleep = false;
        uint8_t refusedBy = kNobody;
    };
    struct GameTime
    {
        float daysPassed = 0;
        float hour = 0;
        uint8_t day = 1;
        uint8_t month = 0;
        uint16_t year = 2281;
    };
    Bytes Encode(const WaitAsk& m);
    bool Decode(Reader& r, WaitAsk& m);
    Bytes Encode(const WaitVote& m);
    bool Decode(Reader& r, WaitVote& m);
    Bytes Encode(const WaitAnswer& m);
    bool Decode(Reader& r, WaitAnswer& m);
    Bytes Encode(const WaitResult& m);
    bool Decode(Reader& r, WaitResult& m);
    Bytes Encode(const GameTime& m);
    bool Decode(Reader& r, GameTime& m);
    constexpr size_t kMaxSaveOrderName = 100;
    struct SaveOrder { bool load = false; std::string name; };
    struct ModsMatch { std::string hostName; std::string save; };
    struct DesyncReport { uint16_t npcs = 0, items = 0, doors = 0; };
    constexpr size_t kMaxCompanions = 16;
    constexpr uint8_t kCompanionWaiting = 0xFF;
    constexpr uint8_t kMaxPlayerId = 8;
    struct CompanionState
    {
        uint32_t ref = 0;
        uint8_t leader = 0;
        bool operator==(const CompanionState& o) const { return ref == o.ref && leader == o.leader; }
        bool operator!=(const CompanionState& o) const { return !(*this == o); }
    };
    struct CompanionStates { std::vector<CompanionState> companions; };
    enum class CompanionOrderKind : uint8_t { FollowMe = 1, Wait = 2 };
    struct CompanionOrder { uint32_t ref = 0; CompanionOrderKind order = CompanionOrderKind::FollowMe; };
    Bytes Encode(const CompanionStates& m);
    bool Decode(Reader& r, CompanionStates& m);
    Bytes Encode(const CompanionOrder& m);
    bool Decode(Reader& r, CompanionOrder& m);
    enum class PlayerCommandKind : uint8_t
    {
        AddItem = 1,
        RemoveItem,
        RemoveAllItems,
        RemoveAllTypedItems,
        GiveAllItems,
        EquipItem,
        UnequipItem,
        AddPerk,
        RemovePerk,
        AddNote,
        RemoveNote,
        AddImageSpaceModifier,
        RemoveImageSpaceModifier,
        DisableControls,
        EnableControls,
        CastOnSelf,
        AddPlayerPackage,
        RemovePlayerPackage,
        Last = RemovePlayerPackage
    };
    struct PlayerCommand
    {
        PlayerCommandKind kind = PlayerCommandKind::AddItem;
        uint32_t form = 0;
        uint32_t ref = 0;
        int32_t n[3] = {};
        uint8_t flags = 0;
    };
    constexpr int32_t kMaxPlayerCommandNumber = 100000;
    struct ChatLine
    {
        uint8_t player = 0;
        std::string text;
        coop::VoiceStyle style;
    };
    constexpr size_t kMaxChatText = 160;
    constexpr size_t kMaxVoiceBytes = 2048;
    struct VoiceData
    {
        uint8_t player = 0;
        Bytes data;
    };
    constexpr size_t kFaceValues = 130;
    constexpr size_t kMaxHeadParts = 8;
    constexpr float kMaxFaceValue = 50;
    struct PlayerLooks
    {
        uint8_t player = 0;
        bool female = false;
        uint32_t race = 0, hair = 0, eyes = 0;
        float hairLength = 0;
        uint32_t hairColor = 0;
        float face[kFaceValues] = {};
        std::vector<uint32_t> headParts;
        std::string name;
        bool operator==(const PlayerLooks& o) const;
    };
    Bytes Encode(const PlayerLooks& m);
    bool Decode(Reader& r, PlayerLooks& m);
    struct PlayerHit
    {
        uint8_t target = 0;
        float damage = 0;
    };
    Bytes Encode(const PlayerHit& m);
    bool Decode(Reader& r, PlayerHit& m);
    Bytes Encode(const VoiceData& m);
    bool Decode(Reader& r, VoiceData& m);
    Bytes Encode(const ChatLine& m);
    bool Decode(Reader& r, ChatLine& m);
    Bytes Encode(const PlayerCommand& m);
    bool Decode(Reader& r, PlayerCommand& m);
    struct RadioPick
    {
        uint32_t topic = 0;
        uint32_t info = 0;
    };
    constexpr uint8_t kMaxRadioRun = 16;
    struct RadioLine
    {
        uint32_t station = 0;
        uint32_t info = 0;
        uint32_t seq = 0;
        uint32_t elapsed = 0;
        uint8_t part = 0;
        std::vector<RadioPick> run;
    };
    Bytes Encode(const RadioLine& m);
    bool Decode(Reader& r, RadioLine& m);
    constexpr uint16_t kMaxDesyncCount = 1000;
    Bytes Encode(const DesyncReport& m);
    bool Decode(Reader& r, DesyncReport& m);
    struct ExplosionAt
    {
        uint32_t form = 0;
        uint32_t space = 0;
        float x = 0, y = 0, z = 0;
    };
    Bytes Encode(const ExplosionAt& m);
    bool Decode(Reader& r, ExplosionAt& m);
    Bytes Encode(const ModsMatch& m);
    bool Decode(Reader& r, ModsMatch& m);
    Bytes Encode(const SaveOrder& m);
    bool Decode(Reader& r, SaveOrder& m);
    struct Freeze { bool frozen = false; uint8_t waitingFor = 0; };
    Bytes Encode(const Freeze& m);
    bool Decode(Reader& r, Freeze& m);
    Bytes Encode(const ModFiles& m);
    bool Decode(Reader& r, ModFiles& m);
    constexpr uint32_t kModPieceSize = 32 * 1024;
    struct ModFileAsk
    {
        uint16_t index = 0;
        uint64_t offset = 0;
    };
    struct ModPiece
    {
        uint16_t index = 0;
        uint64_t offset = 0;
        bool ok = true;
        Bytes data;
    };
    Bytes Encode(const ModFileAsk& m);
    bool Decode(Reader& r, ModFileAsk& m);
    Bytes Encode(const ModPiece& m);
    bool Decode(Reader& r, ModPiece& m);
    Bytes EncodeItemRefund(const ItemCount& m);
    bool DecodeItemRefund(Reader& r, ItemCount& m);
}
