// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "net/protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace net
{

    void Writer::U16(uint16_t v)
    {
        U8(static_cast<uint8_t>(v));
        U8(static_cast<uint8_t>(v >> 8));
    }

    void Writer::U32(uint32_t v)
    {
        U16(static_cast<uint16_t>(v));
        U16(static_cast<uint16_t>(v >> 16));
    }

    void Writer::F32(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        U32(bits);
    }

    void Writer::String(std::string_view s, size_t maxLength)
    {
        if (s.size() > maxLength)
            s = s.substr(0, maxLength);
        U16(static_cast<uint16_t>(s.size()));
        m_data.insert(m_data.end(), s.begin(), s.end());
    }

    bool Reader::Raw(void* out, size_t n)
    {
        if (n > m_size - m_pos)
            return false;
        std::memcpy(out, m_data + m_pos, n);
        m_pos += n;
        return true;
    }

    bool Reader::Type(MsgType& out)
    {
        uint8_t v;
        if (!U8(v))
            return false;
        out = static_cast<MsgType>(v);
        return true;
    }

    bool Reader::U8(uint8_t& out) { return Raw(&out, 1); }

    bool Reader::U16(uint16_t& out)
    {
        uint8_t b[2];
        if (!Raw(b, 2))
            return false;
        out = static_cast<uint16_t>(b[0] | (b[1] << 8));
        return true;
    }

    bool Reader::U32(uint32_t& out)
    {
        uint16_t lo, hi;
        if (!U16(lo) || !U16(hi))
            return false;
        out = lo | (static_cast<uint32_t>(hi) << 16);
        return true;
    }

    bool Reader::F32(float& out)
    {
        uint32_t bits;
        if (!U32(bits))
            return false;
        std::memcpy(&out, &bits, 4);
        return std::isfinite(out) && std::fabs(out) < 1e7f;
    }

    bool Reader::String(std::string& out, size_t maxLength)
    {
        uint16_t length;
        if (!U16(length) || length > maxLength || length > m_size - m_pos)
            return false;
        out.assign(reinterpret_cast<const char*>(m_data + m_pos), length);
        m_pos += length;
        for (unsigned char c : out)
            if (c < 0x20 || c == 0x7F)
                return false;
        return true;
    }

    Bytes Encode(const Hello& m)
    {
        Writer w(MsgType::Hello);
        w.U32(m.version);
        w.String(m.name, kMaxNameLength);
        w.U8((m.hasMods ? 1 : 0) | (m.checkOnly ? 2 : 0));
        if (m.hasMods)
            w.Block(m.mods.data(), m.mods.size());
        return w.Take();
    }

    Bytes Encode(const Welcome& m)
    {
        Writer w(MsgType::Welcome);
        w.U8(m.playerId);
        w.String(m.hostName, kMaxNameLength);
        size_t count = m.questMods.size() < kMaxQuestMods ? m.questMods.size() : kMaxQuestMods;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
            w.String(m.questMods[i], kMaxModFileNameLength);
        w.U8(static_cast<uint8_t>(m.mode.mode));
        w.U8(m.mode.friendlyFire ? 1 : 0);
        w.U32(static_cast<uint32_t>(m.hostXp));
        return w.Take();
    }

    Bytes Encode(const Reject& m)
    {
        Writer w(MsgType::Reject);
        w.String(m.reason, kMaxReasonLength);
        return w.Take();
    }

    Bytes Encode(const PlayerJoined& m)
    {
        Writer w(MsgType::PlayerJoined);
        w.U8(m.playerId);
        w.String(m.name, kMaxNameLength);
        return w.Take();
    }

    Bytes Encode(const PlayerLeft& m)
    {
        Writer w(MsgType::PlayerLeft);
        w.U8(m.playerId);
        return w.Take();
    }

    Bytes Encode(const PlayerState& m)
    {
        Writer w(MsgType::PlayerState);
        w.U8(m.playerId);
        w.U32(m.timeMs);
        w.U32(m.space);
        w.F32(m.x);
        w.F32(m.y);
        w.F32(m.z);
        w.F32(m.heading);
        w.U8(m.flags);
        w.U16(m.moves);
        w.F32(m.pitch);
        w.U16(m.runSpeed);
        return w.Take();
    }

    Bytes Encode(const PlayerGear& m)
    {
        Writer w(MsgType::PlayerGear);
        w.U8(m.playerId);
        size_t count = m.items.size() < kMaxGearItems ? m.items.size() : kMaxGearItems;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
            w.U32(m.items[i]);
        return w.Take();
    }

    Bytes Encode(const Hit& m)
    {
        Writer w(MsgType::Hit);
        w.U8(m.playerId);
        w.U32(m.targetRef);
        w.F32(m.damage);
        return w.Take();
    }

    Bytes Encode(const PlayerHealth& m)
    {
        Writer w(MsgType::PlayerHealth);
        w.U8(m.playerId);
        w.F32(m.health);
        w.F32(m.maxHealth);
        return w.Take();
    }

    Bytes Encode(const Damage& m)
    {
        Writer w(MsgType::Damage);
        w.U8(m.playerId);
        w.F32(m.amount);
        return w.Take();
    }

    Bytes Encode(const Revive& m)
    {
        Writer w(MsgType::Revive);
        w.U8(m.playerId);
        return w.Take();
    }

    Bytes Encode(const GameOver& m)
    {
        Writer w(MsgType::GameOver);
        w.U8(static_cast<uint8_t>(m.reason));
        w.U8(m.playerId);
        return w.Take();
    }

    bool Decode(Reader& r, Hello& m)
    {
        uint8_t flags;
        if (!r.U32(m.version) || !r.String(m.name, kMaxNameLength) || m.name.empty() || !r.U8(flags) || flags > 3)
            return false;
        m.hasMods = (flags & 1) != 0;
        m.checkOnly = (flags & 2) != 0;
        if (m.hasMods && !r.Block(m.mods.data(), m.mods.size()))
            return false;
        return r.AtEnd();
    }

    bool Decode(Reader& r, Welcome& m)
    {
        uint8_t count;
        if (!r.U8(m.playerId) || !r.String(m.hostName, kMaxNameLength) || !r.U8(count) || count > kMaxQuestMods)
            return false;
        m.questMods.resize(count);
        for (std::string& mod : m.questMods)
            if (!r.String(mod, kMaxModFileNameLength) || !IsPlainModFileName(mod, ModKind::Plugin))
                return false;
        uint8_t mode = 0, friendlyFire = 0;
        if (!r.U8(mode) || mode > static_cast<uint8_t>(coop::GameMode::Balanced) || !r.U8(friendlyFire) ||
            friendlyFire > 1)
            return false;
        m.mode = coop::SaveMode{static_cast<coop::GameMode>(mode), friendlyFire == 1};
        uint32_t xp = 0;
        if (!r.U32(xp) || xp > static_cast<uint32_t>(kMaxXp))
            return false;
        m.hostXp = static_cast<int32_t>(xp);
        return r.AtEnd();
    }

    bool Decode(Reader& r, Reject& m)
    {
        return r.String(m.reason, kMaxReasonLength) && r.AtEnd();
    }

    bool Decode(Reader& r, PlayerJoined& m)
    {
        return r.U8(m.playerId) && r.String(m.name, kMaxNameLength) && !m.name.empty() && r.AtEnd();
    }

    bool Decode(Reader& r, PlayerLeft& m)
    {
        return r.U8(m.playerId) && r.AtEnd();
    }

    bool Decode(Reader& r, PlayerState& m)
    {
        if (!r.U8(m.playerId) || !r.U32(m.timeMs) || !r.U32(m.space) || !r.F32(m.x) || !r.F32(m.y) || !r.F32(m.z))
            return false;
        if (!r.F32(m.heading) || !r.U8(m.flags) || (m.flags & ~kKnownPlayerStateFlags) != 0 || !r.U16(m.moves) ||
            !r.F32(m.pitch) || std::fabs(m.pitch) > kMaxPitch || !r.U16(m.runSpeed) || !r.AtEnd())
            return false;
        return true;
    }

    bool Decode(Reader& r, PlayerGear& m)
    {
        uint8_t count;
        if (!r.U8(m.playerId) || !r.U8(count) || count > kMaxGearItems)
            return false;
        m.items.resize(count);
        for (uint32_t& id : m.items)
            if (!r.U32(id))
                return false;
        return r.AtEnd();
    }

    bool Decode(Reader& r, Hit& m)
    {
        return r.U8(m.playerId) && r.U32(m.targetRef) && r.F32(m.damage) && r.AtEnd();
    }

    bool Decode(Reader& r, PlayerHealth& m)
    {
        return r.U8(m.playerId) && r.F32(m.health) && r.F32(m.maxHealth) && r.AtEnd() &&
            m.health >= -kMaxHealth && m.health <= kMaxHealth && m.maxHealth > 0 && m.maxHealth <= kMaxHealth;
    }

    bool Decode(Reader& r, Damage& m)
    {
        return r.U8(m.playerId) && r.F32(m.amount) && r.AtEnd();
    }

    bool Decode(Reader& r, Revive& m)
    {
        return r.U8(m.playerId) && r.AtEnd();
    }

    bool Decode(Reader& r, GameOver& m)
    {
        uint8_t reason = 0;
        if (!r.U8(reason) || reason > static_cast<uint8_t>(GameOverReason::Killed))
            return false;
        m.reason = static_cast<GameOverReason>(reason);
        return r.U8(m.playerId) && r.AtEnd();
    }

    Bytes Encode(const Attack& m)
    {
        Writer w(MsgType::Attack);
        w.U8(m.playerId);
        w.U8(m.release ? 0 : m.shots);
        w.U8(m.release ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, Attack& m)
    {
        uint8_t release;
        if (!r.U8(m.playerId) || !r.U8(m.shots) || !r.U8(release) || !r.AtEnd() || release > 1)
            return false;
        m.release = release == 1;
        return m.release ? m.shots == 0 : m.shots >= 1 && m.shots <= kMaxShots;
    }

    Bytes Encode(const Travel& m)
    {
        Writer w(MsgType::Travel);
        w.U8(m.playerId);
        w.U32(m.space);
        w.F32(m.x);
        w.F32(m.y);
        w.F32(m.z);
        w.F32(m.heading);
        return w.Take();
    }

    Bytes EncodeRegroup()
    {
        return Writer(MsgType::Regroup).Take();
    }

    bool Decode(Reader& r, Travel& m)
    {
        return r.U8(m.playerId) && r.U32(m.space) && r.F32(m.x) && r.F32(m.y) && r.F32(m.z) && r.F32(m.heading) &&
            r.AtEnd() && m.space != 0;
    }

    namespace
    {
        bool ReadAngles(Reader& r, float& x, float& y, float& z)
        {
            return r.F32(x) && r.F32(y) && r.F32(z) && std::fabs(x) <= kMaxItemAngle && std::fabs(y) <= kMaxItemAngle &&
                std::fabs(z) <= kMaxItemAngle;
        }
    }

    bool IsSharedRef(uint32_t ref)
    {
        constexpr uint32_t kPlayerRef = 0x14;
        return ref != 0 && ref != kPlayerRef;
    }

    bool IsMadeWhilePlaying(uint32_t id)
    {
        return (id >> 24) == 0xFF;
    }

    Bytes Encode(const NpcStates& m)
    {
        Writer w(MsgType::NpcStates);
        w.U32(m.timeMs);
        w.U32(m.space);
        size_t count = m.npcs.size() < kMaxNpcsPerMessage ? m.npcs.size() : kMaxNpcsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            const NpcState& n = m.npcs[i];
            w.U32(n.ref);
            w.F32(n.x);
            w.F32(n.y);
            w.F32(n.z);
            w.F32(n.heading);
            w.U8(n.flags);
            w.U16(n.moves);
            w.F32(n.health);
            w.F32(n.maxHealth);
            w.U32(n.idle);
            w.U32(n.weapon);
            w.U32(n.base);
            if (n.flags & kNpcDead)
            {
                w.U16(n.limbs);
                w.U8(n.pile);
            }
        }
        return w.Take();
    }

    bool Decode(Reader& r, NpcStates& m)
    {
        uint8_t count;
        if (!r.U32(m.timeMs) || !r.U32(m.space) || m.space == 0 || !r.U8(count) || count > kMaxNpcsPerMessage)
            return false;
        m.npcs.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            NpcState& n = m.npcs[i];
            if (!r.U32(n.ref) || !IsSharedRef(n.ref) || !r.F32(n.x) || !r.F32(n.y) || !r.F32(n.z) ||
                !r.F32(n.heading) || !r.U8(n.flags) || (n.flags & ~kKnownNpcFlags) != 0 || !r.U16(n.moves) ||
                !r.F32(n.health) || n.health < -kMaxHealth || n.health > kMaxHealth || !r.F32(n.maxHealth) ||
                !(n.maxHealth > 0) || n.maxHealth > kMaxHealth || !r.U32(n.idle) || (n.idle >> 24) == 0xFF ||
                !r.U32(n.weapon) || IsMadeWhilePlaying(n.weapon) || !r.U32(n.base) || IsMadeWhilePlaying(n.base) ||
                (IsMadeWhilePlaying(n.ref) && n.base == 0))
                return false;
            if ((n.flags & kNpcDead) && (!r.U16(n.limbs) || n.limbs >= 0x8000 || !r.U8(n.pile) || n.pile > kPileLast))
                return false;
            for (size_t j = 0; j < i; ++j)
                if (m.npcs[j].ref == n.ref)
                    return false;
        }
        return r.AtEnd();
    }

    Bytes Encode(const NpcAttacks& m)
    {
        Writer w(MsgType::NpcAttacks);
        size_t count = m.attacks.size() < kMaxNpcsPerMessage ? m.attacks.size() : kMaxNpcsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m.attacks[i].ref);
            w.U8(m.attacks[i].shots);
            w.U8((m.attacks[i].reload ? 1 : 0) | (m.attacks[i].release ? 2 : 0));
        }
        return w.Take();
    }

    Bytes Encode(const NpcGear& m)
    {
        Writer w(MsgType::NpcGear);
        w.U32(m.ref);
        size_t count = m.items.size() < kMaxGearItems ? m.items.size() : kMaxGearItems;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m.items[i].item);
            w.U8(m.items[i].health);
        }
        return w.Take();
    }

    bool Decode(Reader& r, NpcGear& m)
    {
        uint8_t count;
        if (!r.U32(m.ref) || !IsSharedRef(m.ref) || !r.U8(count) || count > kMaxGearItems)
            return false;
        m.items.resize(count);
        for (WornItem& i : m.items)
            if (!r.U32(i.item) || i.item == 0 || IsMadeWhilePlaying(i.item) || !r.U8(i.health) ||
                (i.health > 100 && i.health != kNoCondition))
                return false;
        return r.AtEnd();
    }

    bool Decode(Reader& r, NpcAttacks& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxNpcsPerMessage)
            return false;
        m.attacks.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            NpcAttack& a = m.attacks[i];
            uint8_t flags;
            if (!r.U32(a.ref) || !IsSharedRef(a.ref) || !r.U8(a.shots) || a.shots > kMaxShots || !r.U8(flags) ||
                flags > 3 || (a.shots == 0 && flags == 0))
                return false;
            a.reload = (flags & 1) != 0;
            a.release = (flags & 2) != 0;
            for (size_t j = 0; j < i; ++j)
                if (m.attacks[j].ref == a.ref)
                    return false;
        }
        return r.AtEnd();
    }

    bool IsDoorRef(uint32_t ref)
    {
        return ref != 0 && !IsMadeWhilePlaying(ref);
    }

    namespace
    {
        void WriteDoor(Writer& w, const DoorState& d)
        {
            w.U32(d.ref);
            w.U8(d.flags);
            w.U8(d.lockLevel);
        }

        bool ReadDoor(Reader& r, DoorState& d)
        {
            return r.U32(d.ref) && IsDoorRef(d.ref) && r.U8(d.flags) && (d.flags & ~(kDoorOpen | kDoorLocked)) == 0 &&
                r.U8(d.lockLevel);
        }
    }

    Bytes Encode(const DoorStates& m)
    {
        Writer w(MsgType::DoorStates);
        size_t count = m.doors.size() < kMaxDoorsPerMessage ? m.doors.size() : kMaxDoorsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
            WriteDoor(w, m.doors[i]);
        return w.Take();
    }

    bool Decode(Reader& r, DoorStates& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxDoorsPerMessage)
            return false;
        m.doors.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            if (!ReadDoor(r, m.doors[i]))
                return false;
            for (size_t j = 0; j < i; ++j)
                if (m.doors[j].ref == m.doors[i].ref)
                    return false;
        }
        return r.AtEnd();
    }

    Bytes EncodeDoorUse(const DoorState& m)
    {
        Writer w(MsgType::DoorUse);
        WriteDoor(w, m);
        return w.Take();
    }

    bool DecodeDoorUse(Reader& r, DoorState& m)
    {
        return ReadDoor(r, m) && r.AtEnd();
    }

    bool IsContainerRef(uint32_t ref)
    {
        return IsSharedRef(ref);
    }

    namespace
    {
        bool ReadItem(Reader& r, ItemCount& m, bool signedCount)
        {
            uint32_t count;
            if (!r.U32(m.item) || !IsSharedRef(m.item) || !r.U32(count) || !r.U8(m.health) ||
                (m.health > 100 && m.health != kNoCondition) || !r.U8(m.mods) || m.mods > 7)
                return false;
            m.count = static_cast<int32_t>(count);
            if (signedCount)
                return m.count != 0 && m.count >= -kMaxItemCount && m.count <= kMaxItemCount;
            return m.count >= 1 && m.count <= kMaxItemCount;
        }

        void WriteItem(Writer& w, const ItemCount& m)
        {
            w.U32(m.item);
            w.U32(static_cast<uint32_t>(m.count));
            w.U8(m.health);
            w.U8(m.mods);
        }
    }

    Bytes EncodeContainerView(uint32_t ref)
    {
        Writer w(MsgType::ContainerView);
        w.U32(ref);
        return w.Take();
    }

    bool DecodeContainerView(Reader& r, uint32_t& ref)
    {
        return r.U32(ref) && (ref == 0 || IsContainerRef(ref)) && r.AtEnd();
    }

    Bytes Encode(const ContainerChange& m)
    {
        Writer w(MsgType::ContainerChange);
        w.U32(m.ref);
        w.U32(m.seq);
        WriteItem(w, m.change);
        return w.Take();
    }

    bool Decode(Reader& r, ContainerChange& m)
    {
        return r.U32(m.ref) && IsContainerRef(m.ref) && r.U32(m.seq) && ReadItem(r, m.change, true) && r.AtEnd();
    }

    Bytes Encode(const ContainerContents& m)
    {
        Writer w(MsgType::ContainerContents);
        w.U32(m.ref);
        w.U32(m.seen);
        size_t count = m.items.size() < kMaxItemKinds ? m.items.size() : kMaxItemKinds;
        w.U16(static_cast<uint16_t>(count));
        for (size_t i = 0; i < count; ++i)
            WriteItem(w, m.items[i]);
        return w.Take();
    }

    bool Decode(Reader& r, ContainerContents& m)
    {
        uint16_t count;
        if (!r.U32(m.ref) || !IsContainerRef(m.ref) || !r.U32(m.seen) || !r.U16(count) || count > kMaxItemKinds)
            return false;
        m.items.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            if (!ReadItem(r, m.items[i], false))
                return false;
            for (size_t j = 0; j < i; ++j)
                if (m.items[j].SameKind(m.items[i]))
                    return false;
        }
        return r.AtEnd();
    }

    Bytes EncodeItemRefund(const ItemCount& m)
    {
        Writer w(MsgType::ItemRefund);
        WriteItem(w, m);
        return w.Take();
    }

    bool DecodeItemRefund(Reader& r, ItemCount& m)
    {
        return ReadItem(r, m, true) && r.AtEnd();
    }

    namespace
    {
        bool ReadStack(Reader& r, uint32_t& base, int32_t& count)
        {
            uint32_t n;
            if (!r.U32(base) || !IsSharedRef(base) || !r.U32(n))
                return false;
            count = static_cast<int32_t>(n);
            return count >= 1 && count <= kMaxItemCount;
        }

        void WriteStack(Writer& w, uint32_t base, int32_t count)
        {
            w.U32(base);
            w.U32(static_cast<uint32_t>(count));
        }

        bool ReadCondition(Reader& r, uint8_t& health, uint8_t& mods)
        {
            return r.U8(health) && (health <= 100 || health == kNoCondition) && r.U8(mods) && mods <= 7;
        }

        void WriteCondition(Writer& w, uint8_t health, uint8_t mods)
        {
            w.U8(health);
            w.U8(mods);
        }
    }

    Bytes Encode(const ItemStates& m)
    {
        Writer w(MsgType::ItemStates);
        size_t count = m.items.size() < kMaxItemsPerMessage ? m.items.size() : kMaxItemsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            const ItemState& it = m.items[i];
            w.U32(it.ref);
            WriteStack(w, it.base, it.count);
            w.U8((it.present ? 1 : 0) | (it.held ? 2 : 0) | (it.parent ? 4 : 0) | (it.parent && it.parentOn ? 8 : 0));
            if (it.parent)
                w.U32(it.parent);
            w.F32(it.x);
            w.F32(it.y);
            w.F32(it.z);
            w.F32(it.rx);
            w.F32(it.ry);
            w.F32(it.rz);
            WriteCondition(w, it.health, it.mods);
        }
        return w.Take();
    }

    bool Decode(Reader& r, ItemStates& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxItemsPerMessage)
            return false;
        m.items.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            ItemState& it = m.items[i];
            uint8_t flags;
            if (!r.U32(it.ref) || !IsSharedRef(it.ref) || !ReadStack(r, it.base, it.count) || !r.U8(flags) ||
                flags > 15 || ((flags & 8) && !(flags & 4)))
                return false;
            if ((flags & 4) && (!r.U32(it.parent) || !IsSharedRef(it.parent) || IsMadeWhilePlaying(it.parent)))
                return false;
            if (!r.F32(it.x) || !r.F32(it.y) || !r.F32(it.z) || !ReadAngles(r, it.rx, it.ry, it.rz) ||
                !ReadCondition(r, it.health, it.mods))
                return false;
            it.present = (flags & 1) != 0;
            it.held = (flags & 2) != 0;
            it.parentOn = (flags & 8) != 0;
            for (size_t j = 0; j < i; ++j)
                if (m.items[j].ref == it.ref)
                    return false;
        }
        return r.AtEnd();
    }

    Bytes Encode(const ItemMoves& m)
    {
        Writer w(MsgType::ItemMoves);
        size_t count = m.moves.size() < kMaxItemMovesPerMessage ? m.moves.size() : kMaxItemMovesPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            const ItemMove& it = m.moves[i];
            w.U32(it.ref);
            w.U8(it.held ? 1 : 0);
            w.F32(it.x);
            w.F32(it.y);
            w.F32(it.z);
            w.F32(it.rx);
            w.F32(it.ry);
            w.F32(it.rz);
        }
        return w.Take();
    }

    bool Decode(Reader& r, ItemMoves& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxItemMovesPerMessage)
            return false;
        m.moves.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            ItemMove& it = m.moves[i];
            uint8_t held;
            if (!r.U32(it.ref) || !IsSharedRef(it.ref) || !r.U8(held) || held > 1 || !r.F32(it.x) || !r.F32(it.y) ||
                !r.F32(it.z) || !ReadAngles(r, it.rx, it.ry, it.rz))
                return false;
            it.held = held == 1;
            for (size_t j = 0; j < i; ++j)
                if (m.moves[j].ref == it.ref)
                    return false;
        }
        return r.AtEnd();
    }

    Bytes Encode(const ItemTake& m)
    {
        Writer w(MsgType::ItemTake);
        w.U32(m.ref);
        WriteStack(w, m.base, m.count);
        WriteCondition(w, m.health, m.mods);
        return w.Take();
    }

    bool Decode(Reader& r, ItemTake& m)
    {
        return r.U32(m.ref) && IsSharedRef(m.ref) && ReadStack(r, m.base, m.count) &&
            ReadCondition(r, m.health, m.mods) && r.AtEnd();
    }

    Bytes Encode(const ItemDrop& m)
    {
        Writer w(MsgType::ItemDrop);
        WriteStack(w, m.base, m.count);
        w.F32(m.x);
        w.F32(m.y);
        w.F32(m.z);
        WriteCondition(w, m.health, m.mods);
        return w.Take();
    }

    bool Decode(Reader& r, ItemDrop& m)
    {
        return ReadStack(r, m.base, m.count) && r.F32(m.x) && r.F32(m.y) && r.F32(m.z) &&
            ReadCondition(r, m.health, m.mods) && r.AtEnd();
    }

    Bytes Encode(const QuestStates& m)
    {
        Writer w(MsgType::QuestStates);
        size_t count = m.quests.size() < kMaxQuestsPerMessage ? m.quests.size() : kMaxQuestsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            const QuestState& q = m.quests[i];
            w.U32(q.quest);
            w.U8(q.flags);
            w.U8(q.stage);
            size_t stages = q.done.size() < kMaxQuestStages ? q.done.size() : kMaxQuestStages;
            w.U8(static_cast<uint8_t>(stages));
            for (size_t j = 0; j < stages; ++j)
                w.U8(q.done[j]);
            size_t objectives = q.objectives.size() < kMaxQuestObjectives ? q.objectives.size() : kMaxQuestObjectives;
            w.U8(static_cast<uint8_t>(objectives));
            for (size_t j = 0; j < objectives; ++j)
            {
                w.U32(q.objectives[j].id);
                w.U8(q.objectives[j].status);
            }
        }
        return w.Take();
    }

    bool Decode(Reader& r, QuestStates& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxQuestsPerMessage)
            return false;
        m.quests.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            QuestState& q = m.quests[i];
            uint8_t stages, objectives;
            if (!r.U32(q.quest) || q.quest == 0 || !r.U8(q.flags) || !r.U8(q.stage) || !r.U8(stages))
                return false;
            q.done.resize(stages);
            for (uint8_t& st : q.done)
                if (!r.U8(st))
                    return false;
            if (!r.U8(objectives))
                return false;
            q.objectives.resize(objectives);
            for (QuestObjective& o : q.objectives)
                if (!r.U32(o.id) || !r.U8(o.status) || o.status > 3)
                    return false;
            for (size_t j = 0; j < i; ++j)
                if (m.quests[j].quest == q.quest)
                    return false;
        }
        return r.AtEnd();
    }

    Bytes Encode(const std::vector<RefEnable>& m)
    {
        Writer w(MsgType::RefEnables);
        size_t count = m.size() < kMaxEnablesPerMessage ? m.size() : kMaxEnablesPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m[i].ref);
            w.U8(m[i].enabled ? 1 : 0);
        }
        return w.Take();
    }

    bool Decode(Reader& r, std::vector<RefEnable>& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxEnablesPerMessage)
            return false;
        m.resize(count);
        for (RefEnable& e : m)
        {
            uint8_t on;
            if (!r.U32(e.ref) || !IsSharedRef(e.ref) || IsMadeWhilePlaying(e.ref) || !r.U8(on) || on > 1)
                return false;
            e.enabled = on == 1;
        }
        return r.AtEnd();
    }

    Bytes Encode(const Progress& m)
    {
        Writer w(MsgType::Progress);
        w.U8(m.playerId);
        w.U32(static_cast<uint32_t>(m.xp));
        w.U32(static_cast<uint32_t>(m.karma));
        return w.Take();
    }

    bool Decode(Reader& r, Progress& m)
    {
        uint32_t xp, karma;
        if (!r.U8(m.playerId) || !r.U32(xp) || !r.U32(karma))
            return false;
        m.xp = static_cast<int32_t>(xp);
        m.karma = static_cast<int32_t>(karma);
        return m.xp >= 0 && m.xp <= kMaxXpGain && m.karma >= -kMaxKarmaChange && m.karma <= kMaxKarmaChange &&
            (m.xp || m.karma) && r.AtEnd();
    }

    Bytes Encode(const std::vector<Reputation>& m)
    {
        Writer w(MsgType::Reputations);
        size_t count = m.size() < kMaxReputationsPerMessage ? m.size() : kMaxReputationsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m[i].form);
            w.F32(m[i].fame);
            w.F32(m[i].infamy);
        }
        return w.Take();
    }

    bool Decode(Reader& r, std::vector<Reputation>& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxReputationsPerMessage)
            return false;
        m.resize(count);
        for (Reputation& rep : m)
            if (!r.U32(rep.form) || !IsSharedRef(rep.form) || IsMadeWhilePlaying(rep.form) || !r.F32(rep.fame) ||
                !r.F32(rep.infamy) || rep.fame < 0 || rep.fame > kMaxReputation || rep.infamy < 0 ||
                rep.infamy > kMaxReputation)
                return false;
        return r.AtEnd();
    }

    bool IsAnimPath(const std::string& path)
    {
        if (path.empty() || path.size() > kMaxAnimPath || path.find("..") != std::string::npos)
            return false;
        std::string lower;
        for (char c : path)
        {
            if (c == 0 || (!std::isalnum(static_cast<unsigned char>(c)) && !std::strchr(" _-.()\\", c)))
                return false;
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        auto starts = [&](const char* prefix) { return lower.compare(0, std::strlen(prefix), prefix) == 0; };
        bool under = starts("characters\\") || starts("meshes\\characters\\") || starts("animgroupoverride\\") ||
            starts("meshes\\animgroupoverride\\");
        return under && lower.size() > 3 && lower.compare(lower.size() - 3, 3, ".kf") == 0;
    }

    Bytes Encode(const PlayerModAnims& m)
    {
        Writer w(MsgType::PlayerModAnims);
        w.U8(m.player);
        size_t count = m.paths.size() < kMaxModAnims ? m.paths.size() : kMaxModAnims;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
            w.String(m.paths[i], kMaxAnimPath);
        return w.Take();
    }

    bool Decode(Reader& r, PlayerModAnims& m)
    {
        uint8_t count;
        if (!r.U8(m.player) || !r.U8(count) || count > kMaxModAnims)
            return false;
        m.paths.resize(count);
        for (std::string& path : m.paths)
            if (!r.String(path, kMaxAnimPath) || !IsAnimPath(path))
                return false;
        return r.AtEnd();
    }

    Bytes Encode(const PlayerAnim& m)
    {
        Writer w(MsgType::PlayerAnim);
        w.U8(m.player);
        w.String(m.path, kMaxAnimPath);
        return w.Take();
    }

    bool Decode(Reader& r, PlayerAnim& m)
    {
        return r.U8(m.player) && r.String(m.path, kMaxAnimPath) && (m.path.empty() || IsAnimPath(m.path)) &&
            r.AtEnd();
    }

    Bytes Encode(const Teleport& m)
    {
        Writer w(MsgType::Teleport);
        w.U8(m.mover);
        w.U8(m.target);
        return w.Take();
    }

    bool Decode(Reader& r, Teleport& m)
    {
        return r.U8(m.mover) && r.U8(m.target) && m.mover != m.target && r.AtEnd();
    }

    Bytes Encode(const Give& m)
    {
        Writer w(MsgType::Give);
        w.U8(static_cast<uint8_t>(m.stage));
        w.U32(m.id);
        w.U8(m.other);
        if (m.stage == GiveStage::Ask || m.stage == GiveStage::Deliver)
        {
            WriteItem(w, m.item);
            w.U8(m.itemType);
        }
        else
            w.U8(static_cast<uint8_t>(m.reason));
        return w.Take();
    }

    bool Decode(Reader& r, Give& m)
    {
        uint8_t stage;
        if (!r.U8(stage) || stage > static_cast<uint8_t>(GiveStage::Result) || !r.U32(m.id) || m.id == 0 ||
            !r.U8(m.other))
            return false;
        m.stage = static_cast<GiveStage>(stage);
        m.item = ItemCount{};
        m.itemType = 0;
        m.reason = GiveReason::Ok;
        if (m.stage == GiveStage::Ask || m.stage == GiveStage::Deliver)
            return ReadItem(r, m.item, false) && !IsMadeWhilePlaying(m.item.item) && r.U8(m.itemType) && r.AtEnd();
        uint8_t reason;
        if (!r.U8(reason) || reason > static_cast<uint8_t>(GiveReason::TurnedOff))
            return false;
        m.reason = static_cast<GiveReason>(reason);
        return r.AtEnd();
    }

    Bytes Encode(const Drag& m)
    {
        Writer w(MsgType::Drag);
        w.U8(m.dragger);
        w.U8(m.target);
        w.U8(m.on ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, Drag& m)
    {
        uint8_t on = 0;
        if (!r.U8(m.dragger) || !r.U8(m.target) || !r.U8(on) || on > 1 || m.dragger == m.target || !r.AtEnd())
            return false;
        m.on = on != 0;
        return true;
    }

    Bytes Encode(const ModeChange& m)
    {
        Writer w(MsgType::Mode);
        w.U8(static_cast<uint8_t>(m.mode.mode));
        w.U8(m.mode.friendlyFire ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, ModeChange& m)
    {
        uint8_t mode = 0, friendlyFire = 0;
        if (!r.U8(mode) || mode < static_cast<uint8_t>(coop::GameMode::Normal) ||
            mode > static_cast<uint8_t>(coop::GameMode::Balanced) || !r.U8(friendlyFire) || friendlyFire > 1 ||
            !r.AtEnd())
            return false;
        m.mode = coop::SaveMode{static_cast<coop::GameMode>(mode), friendlyFire == 1};
        return true;
    }

    Bytes EncodeTravelMarkers(const std::vector<uint32_t>& refs)
    {
        Writer w(MsgType::TravelMarkers);
        size_t count = refs.size() < kMaxMarkersPerMessage ? refs.size() : kMaxMarkersPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
            w.U32(refs[i]);
        return w.Take();
    }

    bool DecodeTravelMarkers(Reader& r, std::vector<uint32_t>& refs)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxMarkersPerMessage)
            return false;
        refs.resize(count);
        for (uint32_t& ref : refs)
            if (!r.U32(ref) || !IsSharedRef(ref) || IsMadeWhilePlaying(ref))
                return false;
        return r.AtEnd();
    }

    Bytes EncodeGains(const std::vector<ReputationGain>& m)
    {
        Writer w(MsgType::ReputationGain);
        size_t count = m.size() < kMaxReputationsPerMessage ? m.size() : kMaxReputationsPerMessage;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m[i].form);
            w.F32(m[i].fame);
            w.F32(m[i].infamy);
        }
        return w.Take();
    }

    bool DecodeGains(Reader& r, std::vector<ReputationGain>& m)
    {
        uint8_t count;
        if (!r.U8(count) || count == 0 || count > kMaxReputationsPerMessage)
            return false;
        m.resize(count);
        for (ReputationGain& g : m)
            if (!r.U32(g.form) || !IsSharedRef(g.form) || IsMadeWhilePlaying(g.form) || !r.F32(g.fame) ||
                !r.F32(g.infamy) || !(g.fame >= -kMaxReputation && g.fame <= kMaxReputation) ||
                !(g.infamy >= -kMaxReputation && g.infamy <= kMaxReputation) || (g.fame == 0 && g.infamy == 0))
                return false;
        return r.AtEnd();
    }

    Bytes Encode(const TalkState& m)
    {
        Writer w(MsgType::TalkState);
        w.U8(m.playerId);
        w.U8(m.talking ? 1 : 0);
        w.U32(m.talking ? m.npc : 0);
        return w.Take();
    }

    bool Decode(Reader& r, TalkState& m)
    {
        uint8_t talking;
        if (!r.U8(m.playerId) || !r.U8(talking) || talking > 1 || !r.U32(m.npc))
            return false;
        m.talking = talking == 1;
        return (m.npc == 0 || (m.talking && IsSharedRef(m.npc))) && r.AtEnd();
    }

    Bytes Encode(const DialogueLine& m)
    {
        Writer w(MsgType::DialogueLine);
        w.U32(m.info);
        w.U8(m.onEnd ? 1 : 0);
        w.U32(m.speaker);
        return w.Take();
    }

    bool Decode(Reader& r, DialogueLine& m)
    {
        uint8_t onEnd;
        if (!r.U32(m.info) || !r.U8(onEnd) || onEnd > 1 || !r.U32(m.speaker))
            return false;
        m.onEnd = onEnd == 1;
        return IsSharedRef(m.info) && !IsMadeWhilePlaying(m.info) && IsSharedRef(m.speaker) && r.AtEnd();
    }

    Bytes Encode(const Subtitle& m)
    {
        Writer w(MsgType::Subtitle);
        w.U8(m.playerId);
        w.U32(m.speaker);
        w.U8(m.stop ? 1 : m.reply ? 2 : 0);
        if (m.stop)
            return w.Take();
        w.U32(m.topic);
        w.U32(m.info);
        w.U8(m.response);
        w.String(m.text, kMaxSubtitleLength);
        return w.Take();
    }

    bool Decode(Reader& r, Subtitle& m)
    {
        uint8_t stop;
        if (!r.U8(m.playerId) || !r.U32(m.speaker) || !IsSharedRef(m.speaker) || !r.U8(stop) || stop > 2)
            return false;
        m.stop = stop == 1;
        m.reply = stop == 2;
        m.topic = m.info = 0;
        m.response = 0;
        m.text.clear();
        if (m.stop)
            return r.AtEnd();
        auto line = [](uint32_t id) { return IsSharedRef(id) && !IsMadeWhilePlaying(id); };
        return r.U32(m.topic) && r.U32(m.info) && ((m.topic == 0 && m.info == 0) || (line(m.topic) && line(m.info))) &&
            r.U8(m.response) && r.String(m.text, kMaxSubtitleLength) &&
            !m.text.empty() && r.AtEnd();
    }

    namespace
    {
        bool Flag(Reader& r, bool& out)
        {
            uint8_t v;
            if (!r.U8(v) || v > 1)
                return false;
            out = v == 1;
            return true;
        }
        bool Hours(Reader& r, uint8_t& out, bool zeroOk)
        {
            return r.U8(out) && out <= kMaxWaitHours && (zeroOk || out >= 1);
        }
    }

    Bytes Encode(const WaitAsk& m)
    {
        Writer w(MsgType::WaitAsk);
        w.U8(m.hours);
        w.U8(m.sleep ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, WaitAsk& m)
    {
        return Hours(r, m.hours, true) && Flag(r, m.sleep) && r.AtEnd();
    }

    Bytes Encode(const WaitVote& m)
    {
        Writer w(MsgType::WaitVote);
        w.U8(m.requester);
        w.U8(m.hours);
        w.U8(m.sleep ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, WaitVote& m)
    {
        return r.U8(m.requester) && Hours(r, m.hours, false) && Flag(r, m.sleep) && r.AtEnd();
    }

    Bytes Encode(const WaitAnswer& m)
    {
        Writer w(MsgType::WaitAnswer);
        w.U8(m.yes ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, WaitAnswer& m)
    {
        return Flag(r, m.yes) && r.AtEnd();
    }

    Bytes Encode(const WaitResult& m)
    {
        Writer w(MsgType::WaitResult);
        w.U8(m.requester);
        w.U8(m.passed ? 1 : 0);
        w.U8(m.hours);
        w.U8(m.sleep ? 1 : 0);
        w.U8(m.refusedBy);
        return w.Take();
    }

    bool Decode(Reader& r, WaitResult& m)
    {
        return r.U8(m.requester) && Flag(r, m.passed) && Hours(r, m.hours, !m.passed) && Flag(r, m.sleep) &&
            r.U8(m.refusedBy) && r.AtEnd();
    }

    Bytes Encode(const GameTime& m)
    {
        Writer w(MsgType::GameTime);
        w.F32(m.daysPassed);
        w.F32(m.hour);
        w.U8(m.day);
        w.U8(m.month);
        w.U16(m.year);
        return w.Take();
    }

    bool Decode(Reader& r, GameTime& m)
    {
        return r.F32(m.daysPassed) && m.daysPassed >= 0 && m.daysPassed <= 100000 && r.F32(m.hour) && m.hour >= 0 &&
            m.hour <= 24 && r.U8(m.day) && m.day >= 1 && m.day <= 31 && r.U8(m.month) && m.month <= 11 &&
            r.U16(m.year) && r.AtEnd();
    }

    Bytes Encode(const SaveOrder& m)
    {
        Writer w(MsgType::SaveOrder);
        w.U8(m.load ? 1 : 0);
        w.String(m.name, kMaxSaveOrderName);
        return w.Take();
    }

    bool Decode(Reader& r, SaveOrder& m)
    {
        return Flag(r, m.load) && r.String(m.name, kMaxSaveOrderName) && !m.name.empty() && r.AtEnd();
    }

    Bytes Encode(const CompanionStates& m)
    {
        Writer w(MsgType::CompanionStates);
        size_t count = m.companions.size() < kMaxCompanions ? m.companions.size() : kMaxCompanions;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m.companions[i].ref);
            w.U8(m.companions[i].leader);
        }
        return w.Take();
    }

    bool Decode(Reader& r, CompanionStates& m)
    {
        uint8_t count;
        if (!r.U8(count) || count > kMaxCompanions)
            return false;
        m.companions.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            CompanionState& c = m.companions[i];
            if (!r.U32(c.ref) || !IsSharedRef(c.ref) || !r.U8(c.leader) ||
                (c.leader > kMaxPlayerId && c.leader != kCompanionWaiting))
                return false;
            for (size_t j = 0; j < i; ++j)
                if (m.companions[j].ref == c.ref)
                    return false;
        }
        return r.AtEnd();
    }

    Bytes Encode(const CompanionOrder& m)
    {
        Writer w(MsgType::CompanionOrder);
        w.U32(m.ref);
        w.U8(static_cast<uint8_t>(m.order));
        return w.Take();
    }

    bool Decode(Reader& r, CompanionOrder& m)
    {
        uint8_t order;
        if (!r.U32(m.ref) || !IsSharedRef(m.ref) || !r.U8(order) || order < 1 || order > 2 || !r.AtEnd())
            return false;
        m.order = static_cast<CompanionOrderKind>(order);
        return true;
    }

    Bytes Encode(const RadioLine& m)
    {
        Writer w(MsgType::RadioLine);
        w.U32(m.station);
        w.U32(m.info);
        w.U32(m.seq);
        w.U32(m.elapsed);
        w.U8(m.part);
        size_t count = m.run.size() < kMaxRadioRun ? m.run.size() : kMaxRadioRun;
        w.U8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            w.U32(m.run[i].topic);
            w.U32(m.run[i].info);
        }
        return w.Take();
    }

    bool Decode(Reader& r, RadioLine& m)
    {
        uint8_t count = 0;
        if (!(r.U32(m.station) && IsSharedRef(m.station) && !IsMadeWhilePlaying(m.station) && r.U32(m.info) &&
                m.info != 0 && !IsMadeWhilePlaying(m.info) && r.U32(m.seq) && r.U32(m.elapsed) &&
                m.elapsed < 3600000 && r.U8(m.part) && m.part < 64 && r.U8(count) && count <= kMaxRadioRun))
            return false;
        m.run.clear();
        for (uint8_t i = 0; i < count; ++i)
        {
            RadioPick pick;
            if (!r.U32(pick.topic) || IsMadeWhilePlaying(pick.topic) || !r.U32(pick.info) || pick.info == 0 ||
                IsMadeWhilePlaying(pick.info))
                return false;
            m.run.push_back(pick);
        }
        return r.AtEnd();
    }

    Bytes Encode(const PlayerHit& m)
    {
        Writer w(MsgType::PlayerHit);
        w.U8(m.target);
        w.F32(m.damage);
        return w.Take();
    }

    bool Decode(Reader& r, PlayerHit& m)
    {
        return r.U8(m.target) && r.F32(m.damage) && m.damage > 0 && r.AtEnd();
    }

    Bytes Encode(const VoiceData& m)
    {
        Writer w(MsgType::VoiceData);
        w.U8(m.player);
        size_t size = m.data.size() < kMaxVoiceBytes ? m.data.size() : kMaxVoiceBytes;
        w.U16(static_cast<uint16_t>(size));
        w.Block(m.data.data(), size);
        return w.Take();
    }

    bool Decode(Reader& r, VoiceData& m)
    {
        uint16_t size = 0;
        if (!r.U8(m.player) || !r.U16(size) || size == 0 || size > kMaxVoiceBytes)
            return false;
        m.data.resize(size);
        return r.Block(m.data.data(), size) && r.AtEnd();
    }

    bool PlayerLooks::operator==(const PlayerLooks& o) const
    {
        return female == o.female && race == o.race && hair == o.hair && eyes == o.eyes && hairLength == o.hairLength &&
            hairColor == o.hairColor && std::equal(face, face + kFaceValues, o.face) && headParts == o.headParts &&
            name == o.name;
    }

    Bytes Encode(const PlayerLooks& m)
    {
        Writer w(MsgType::PlayerLooks);
        w.U8(m.player);
        w.U8(m.female ? 1 : 0);
        w.U32(m.race);
        w.U32(m.hair);
        w.U32(m.eyes);
        w.F32(m.hairLength);
        w.U32(m.hairColor);
        for (float v : m.face)
            w.F32(v);
        size_t parts = m.headParts.size() < kMaxHeadParts ? m.headParts.size() : kMaxHeadParts;
        w.U8(static_cast<uint8_t>(parts));
        for (size_t i = 0; i < parts; ++i)
            w.U32(m.headParts[i]);
        w.String(m.name, kMaxNameLength);
        return w.Take();
    }

    bool Decode(Reader& r, PlayerLooks& m)
    {
        uint8_t female = 0, parts = 0;
        if (!r.U8(m.player) || !r.U8(female) || female > 1 || !r.U32(m.race) || !r.U32(m.hair) || !r.U32(m.eyes) ||
            !r.F32(m.hairLength) || !std::isfinite(m.hairLength) || m.hairLength < 0 || m.hairLength > 10 ||
            !r.U32(m.hairColor))
            return false;
        m.female = female == 1;
        for (float& v : m.face)
            if (!r.F32(v) || !std::isfinite(v) || v < -kMaxFaceValue || v > kMaxFaceValue)
                return false;
        if (!r.U8(parts) || parts > kMaxHeadParts)
            return false;
        m.headParts.resize(parts);
        for (uint32_t& p : m.headParts)
            if (!r.U32(p) || !p || IsMadeWhilePlaying(p))
                return false;
        if (!r.String(m.name, kMaxNameLength))
            return false;
        for (char c : m.name)
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                return false;
        return !IsMadeWhilePlaying(m.race) && !IsMadeWhilePlaying(m.hair) && !IsMadeWhilePlaying(m.eyes) && r.AtEnd();
    }

    Bytes Encode(const NpcPile& m)
    {
        Writer w(MsgType::NpcPile);
        w.U32(m.ref);
        w.U8(m.kind);
        return w.Take();
    }

    bool Decode(Reader& r, NpcPile& m)
    {
        return r.U32(m.ref) && IsSharedRef(m.ref) && r.U8(m.kind) && m.kind >= kPileGoo && m.kind <= kPileLast &&
            r.AtEnd();
    }

    Bytes Encode(const Fighting& m)
    {
        Writer w(MsgType::Fighting);
        w.U8(m.fighting ? 1 : 0);
        return w.Take();
    }

    bool Decode(Reader& r, Fighting& m)
    {
        uint8_t on;
        if (!r.U8(on) || on > 1 || !r.AtEnd())
            return false;
        m.fighting = on != 0;
        return true;
    }

    Bytes Encode(const MineChange& m)
    {
        Writer w(MsgType::MineChange);
        w.U32(m.ref);
        w.U8(m.state);
        return w.Take();
    }

    bool Decode(Reader& r, MineChange& m)
    {
        return r.U32(m.ref) && IsSharedRef(m.ref) && !IsMadeWhilePlaying(m.ref) && r.U8(m.state) &&
            m.state >= kMineOff && m.state <= kMineBlown && r.AtEnd();
    }

    Bytes Encode(const ChatLine& m)
    {
        Writer w(MsgType::ChatLine);
        w.U8(m.player);
        w.String(m.text, kMaxChatText);
        w.U8(m.style.voice);
        w.U8(m.style.variant);
        w.U8(static_cast<uint8_t>(m.style.pitch));
        w.U8(static_cast<uint8_t>(m.style.speed));
        w.U8(m.style.effect);
        return w.Take();
    }

    bool Decode(Reader& r, ChatLine& m)
    {
        uint8_t pitch = 0, speed = 0;
        if (!(r.U8(m.player) && r.String(m.text, kMaxChatText) && !m.text.empty() && r.U8(m.style.voice) &&
                r.U8(m.style.variant) && r.U8(pitch) && r.U8(speed) && r.U8(m.style.effect) && r.AtEnd()))
            return false;
        m.style.pitch = static_cast<int8_t>(pitch);
        m.style.speed = static_cast<int8_t>(speed);
        return m.style.Valid();
    }

    Bytes Encode(const PlayerCommand& m)
    {
        Writer w(MsgType::PlayerCommand);
        w.U8(static_cast<uint8_t>(m.kind));
        w.U32(m.form);
        w.U32(m.ref);
        for (int32_t v : m.n)
            w.U32(static_cast<uint32_t>(v));
        w.U8(m.flags);
        return w.Take();
    }

    bool Decode(Reader& r, PlayerCommand& m)
    {
        uint8_t kind;
        if (!r.U8(kind) || kind < 1 || kind > static_cast<uint8_t>(PlayerCommandKind::Last) || !r.U32(m.form) ||
            IsMadeWhilePlaying(m.form) || !r.U32(m.ref) || IsMadeWhilePlaying(m.ref) || m.ref == 0x14)
            return false;
        for (int32_t& v : m.n)
        {
            uint32_t raw;
            if (!r.U32(raw))
                return false;
            v = static_cast<int32_t>(raw);
            if (v > kMaxPlayerCommandNumber || v < -kMaxPlayerCommandNumber)
                return false;
        }
        m.kind = static_cast<PlayerCommandKind>(kind);
        return r.U8(m.flags) && r.AtEnd();
    }

    Bytes Encode(const DesyncReport& m)
    {
        Writer w(MsgType::DesyncReport);
        w.U16(m.npcs);
        w.U16(m.items);
        w.U16(m.doors);
        return w.Take();
    }

    bool Decode(Reader& r, DesyncReport& m)
    {
        return r.U16(m.npcs) && m.npcs <= kMaxDesyncCount && r.U16(m.items) && m.items <= kMaxDesyncCount &&
            r.U16(m.doors) && m.doors <= kMaxDesyncCount && r.AtEnd();
    }

    Bytes Encode(const ExplosionAt& m)
    {
        Writer w(MsgType::ExplosionAt);
        w.U32(m.form);
        w.U32(m.space);
        w.F32(m.x);
        w.F32(m.y);
        w.F32(m.z);
        return w.Take();
    }

    bool Decode(Reader& r, ExplosionAt& m)
    {
        return r.U32(m.form) && m.form != 0 && !IsMadeWhilePlaying(m.form) && r.U32(m.space) && m.space != 0 &&
            r.F32(m.x) && r.F32(m.y) && r.F32(m.z) && r.AtEnd();
    }

    Bytes Encode(const ModsMatch& m)
    {
        Writer w(MsgType::ModsMatch);
        w.String(m.hostName, kMaxNameLength);
        w.String(m.save, kMaxSaveOrderName);
        return w.Take();
    }

    bool Decode(Reader& r, ModsMatch& m)
    {
        return r.String(m.hostName, kMaxNameLength) && !m.hostName.empty() && r.String(m.save, kMaxSaveOrderName) &&
            r.AtEnd();
    }

    Bytes Encode(const Freeze& m)
    {
        Writer w(MsgType::Freeze);
        w.U8(m.frozen ? 1 : 0);
        w.U8(m.waitingFor);
        return w.Take();
    }

    bool Decode(Reader& r, Freeze& m)
    {
        return Flag(r, m.frozen) && r.U8(m.waitingFor) && r.AtEnd();
    }

    bool IsPlainModFileName(const std::string& name, ModKind kind)
    {
        const char* ext = kind == ModKind::Plugin ? nullptr : kind == ModKind::Archive ? ".bsa" : ".dll";
        if (name.size() < 5 || name.size() > kMaxModFileNameLength || name.front() == '.' || name.front() == ' ' ||
            name.back() == '.' || name.back() == ' ')
            return false;
        for (unsigned char c : name)
            if (c < 0x20 || c == 0x7F || std::strchr("\\/:*?\"<>|", c))
                return false;
        std::string end = name.substr(name.size() - 4);
        if (ext ? _stricmp(end.c_str(), ext) != 0 : _stricmp(end.c_str(), ".esm") != 0 && _stricmp(end.c_str(), ".esp") != 0)
            return false;
        std::string stem = name.substr(0, name.find('.'));
        while (!stem.empty() && stem.back() == ' ')
            stem.pop_back();
        static const char* const kDevices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
        for (const char* device : kDevices)
            if (_stricmp(stem.c_str(), device) == 0)
                return false;
        if (stem.size() == 4 && (_strnicmp(stem.c_str(), "COM", 3) == 0 || _strnicmp(stem.c_str(), "LPT", 3) == 0))
        {
            unsigned char digit = static_cast<unsigned char>(stem[3]);
            if ((digit >= '0' && digit <= '9') || digit == 0xB9 || digit == 0xB2 || digit == 0xB3)
                return false;
        }
        return true;
    }

    Bytes Encode(const ModFiles& m)
    {
        Writer w(MsgType::ModFiles);
        size_t count = m.files.size() < kModFilesPerMessage ? m.files.size() : kModFilesPerMessage;
        w.U8(m.last ? 1 : 0);
        w.U16(static_cast<uint16_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            const ModFile& f = m.files[i];
            w.String(f.name, kMaxModFileNameLength);
            w.U8(static_cast<uint8_t>(f.kind));
            w.U32(static_cast<uint32_t>(f.size));
            w.U32(static_cast<uint32_t>(f.size >> 32));
            w.U8(f.hashed ? 1 : 0);
            if (f.hashed)
                w.Block(f.hash.data(), f.hash.size());
        }
        return w.Take();
    }

    bool Decode(Reader& r, ModFiles& m)
    {
        uint8_t last;
        uint16_t count;
        if (!r.U8(last) || last > 1 || !r.U16(count) || count > kModFilesPerMessage)
            return false;
        m.last = last == 1;
        m.files.resize(count);
        for (ModFile& f : m.files)
        {
            uint8_t kind, hashed;
            uint32_t low, high;
            if (!r.String(f.name, kMaxModFileNameLength) || !r.U8(kind) || kind > 2 || !r.U32(low) || !r.U32(high) ||
                !r.U8(hashed) || hashed > 1)
                return false;
            f.kind = static_cast<ModKind>(kind);
            f.size = (uint64_t(high) << 32) | low;
            f.hashed = hashed == 1;
            if (f.size > kMaxModFileSize || !IsPlainModFileName(f.name, f.kind) ||
                (f.hashed && !r.Block(f.hash.data(), f.hash.size())))
                return false;
        }
        return r.AtEnd();
    }

    Bytes Encode(const ModFileAsk& m)
    {
        Writer w(MsgType::ModFileAsk);
        w.U16(m.index);
        w.U32(static_cast<uint32_t>(m.offset));
        w.U32(static_cast<uint32_t>(m.offset >> 32));
        return w.Take();
    }

    bool Decode(Reader& r, ModFileAsk& m)
    {
        uint32_t low, high;
        if (!r.U16(m.index) || !r.U32(low) || !r.U32(high) || !r.AtEnd())
            return false;
        m.offset = (uint64_t(high) << 32) | low;
        return m.offset % kModPieceSize == 0;
    }

    Bytes Encode(const ModPiece& m)
    {
        Writer w(MsgType::ModPiece);
        w.U16(m.index);
        w.U32(static_cast<uint32_t>(m.offset));
        w.U32(static_cast<uint32_t>(m.offset >> 32));
        w.U8(m.ok ? 1 : 0);
        size_t size = m.data.size() < kModPieceSize ? m.data.size() : kModPieceSize;
        w.U32(static_cast<uint32_t>(size));
        w.Block(m.data.data(), size);
        return w.Take();
    }

    bool Decode(Reader& r, ModPiece& m)
    {
        uint32_t low, high, size;
        uint8_t ok;
        if (!r.U16(m.index) || !r.U32(low) || !r.U32(high) || !r.U8(ok) || ok > 1 || !r.U32(size) ||
            size > kModPieceSize)
            return false;
        m.offset = (uint64_t(high) << 32) | low;
        m.ok = ok == 1;
        m.data.resize(size);
        return (size == 0 || r.Block(m.data.data(), size)) && r.AtEnd();
    }
}
