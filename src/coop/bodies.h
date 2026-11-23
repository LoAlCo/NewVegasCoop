// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/containers.h"
#include "net/protocol.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    constexpr size_t kMaxBodies = 1024;

    class HostBodies
    {
    public:
        bool Set(uint32_t ref, const std::vector<net::ItemCount>& items)
        {
            auto it = m_bodies.find(ref);
            if (it != m_bodies.end() && it->second.items == items)
                return false;
            if (it == m_bodies.end() && m_bodies.size() >= kMaxBodies)
                return false;
            Body& body = m_bodies[ref];
            body.items = items;
            body.version = ++m_version;
            ++m_changes;
            return true;
        }
        bool Has(uint32_t ref) const { return m_bodies.count(ref) != 0; }
        size_t Count() const { return m_bodies.size(); }
        template <typename IsShared>
        void KeepOnly(IsShared isShared)
        {
            for (auto it = m_bodies.begin(); it != m_bodies.end();)
            {
                if (isShared(it->first))
                {
                    ++it;
                    continue;
                }
                for (auto& [player, sent] : m_sent)
                    sent.versions.erase(it->first);
                it = m_bodies.erase(it);
            }
        }
        void Touch(uint8_t player, uint32_t ref)
        {
            if (m_sent[player].versions.erase(ref))
                ++m_changes;
        }
        void Left(uint8_t player) { m_sent.erase(player); }
        const std::vector<net::ItemCount>* Next(uint8_t player, uint32_t& ref)
        {
            Sent& sent = m_sent[player];
            if (sent.upTo == m_changes)
                return nullptr;
            for (const auto& [id, body] : m_bodies)
            {
                uint32_t& version = sent.versions[id];
                if (version == body.version)
                    continue;
                version = body.version;
                ref = id;
                return &body.items;
            }
            sent.upTo = m_changes;
            return nullptr;
        }

    private:
        struct Body
        {
            std::vector<net::ItemCount> items;
            uint32_t version = 0;
        };
        struct Sent
        {
            std::map<uint32_t, uint32_t> versions;
            uint32_t upTo = 0;
        };
        std::map<uint32_t, Body> m_bodies;
        std::map<uint8_t, Sent> m_sent;
        uint32_t m_version = 0;
        uint32_t m_changes = 1;
    };

    class FriendBodies
    {
    public:
        void Heard(const net::ContainerContents& m)
        {
            if (!m.ref)
                return;
            Body* body = Find(m.ref, true);
            body->has = true;
            body->waiting = true;
            body->seen = m.seen;
            body->items = m.items;
        }
        void OurChange(uint32_t ref, uint32_t seq)
        {
            if (!ref)
                return;
            Body* body = Find(ref, true);
            body->changedByUs = true;
            body->ourSeq = seq;
        }
        void Waiting(std::vector<uint32_t>& refs) const
        {
            for (const auto& [ref, body] : m_bodies)
                if (body.waiting)
                    refs.push_back(ref);
        }
        bool Fixes(uint32_t ref, const std::vector<net::ItemCount>& local, std::vector<net::ItemCount>& fixes) const
        {
            auto it = m_bodies.find(ref);
            if (it == m_bodies.end() || !it->second.has)
                return false;
            const Body& body = it->second;
            if (body.changedByUs && static_cast<int32_t>(body.seen - body.ourSeq) < 0)
                return false;
            fixes = ItemDiff(local, body.items);
            return true;
        }
        void Done(uint32_t ref)
        {
            if (Body* body = Find(ref, false))
                body->waiting = false;
        }
        void Forget(uint32_t ref) { m_bodies.erase(ref); }
        bool NextToCheck(uint32_t& ref)
        {
            auto it = m_bodies.upper_bound(m_checked);
            for (size_t n = 0; n < m_bodies.size(); ++n, ++it)
            {
                if (it == m_bodies.end())
                    it = m_bodies.begin();
                if (it->second.has && !it->second.waiting)
                {
                    ref = m_checked = it->first;
                    return true;
                }
            }
            return false;
        }
        size_t Count() const { return m_bodies.size(); }
        void Reset() { *this = FriendBodies(); }

    private:
        struct Body
        {
            bool has = false;
            bool waiting = false;
            uint32_t seen = 0;
            std::vector<net::ItemCount> items;
            bool changedByUs = false;
            uint32_t ourSeq = 0;
            uint32_t stamp = 0;
        };
        Body* Find(uint32_t ref, bool make)
        {
            auto it = m_bodies.find(ref);
            if (it == m_bodies.end())
            {
                if (!make)
                    return nullptr;
                if (m_bodies.size() >= kMaxBodies)
                {
                    auto oldest = m_bodies.begin();
                    for (auto b = m_bodies.begin(); b != m_bodies.end(); ++b)
                        if (static_cast<int32_t>(b->second.stamp - oldest->second.stamp) < 0)
                            oldest = b;
                    m_bodies.erase(oldest);
                }
                it = m_bodies.emplace(ref, Body()).first;
            }
            if (make)
                it->second.stamp = ++m_stamp;
            return &it->second;
        }
        std::map<uint32_t, Body> m_bodies;
        uint32_t m_stamp = 0;
        uint32_t m_checked = 0;
    };
}
