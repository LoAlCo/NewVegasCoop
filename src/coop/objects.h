// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    constexpr uint32_t kObjectResendMs = 10000;
    constexpr size_t kMaxKnownObjects = 1024;
    constexpr float kHealthStep = 0.5f;

    class HostObjects
    {
    public:
        bool Destroyed(uint32_t ref, bool destroyed);
        bool Played(uint32_t ref, bool destroyed, uint16_t group, bool init);
        bool Health(uint32_t ref, float health, float full, bool destroyed);
        std::vector<net::ObjectState> Take(uint32_t now);
        void Clear();
        size_t Known() const { return m_known.size(); }

    private:
        net::ObjectState* Find(uint32_t ref, bool& added);
        std::map<uint32_t, net::ObjectState> m_known;
        std::vector<uint32_t> m_changed;
        uint32_t m_nextResend = 0;
    };

    struct LocalObject
    {
        bool found = false;
        bool destroyed = false;
        const void* model = nullptr;
        float health = net::kNoHealth;
    };

    struct SeenObject
    {
        uint32_t ref = 0;
        float health = 0;
        float full = 0;
        float margin = 0;
    };

    constexpr uint32_t kObjectLowEveryMs = 250;

    struct ObjectAction
    {
        uint32_t ref = 0;
        bool setDestroyed = false;
        bool destroyed = false;
        bool play = false;
        uint16_t group = net::kNoAnimGroup;
        bool init = false;
        float damage = 0;
    };

    class FriendObjects
    {
    public:
        void Heard(const net::ObjectState& host);
        template <typename Look>
        std::vector<ObjectAction> Due(Look look)
        {
            std::vector<ObjectAction> out;
            for (auto& [ref, o] : m_objects)
                Decide(ref, o, look(ref), out);
            return out;
        }
        std::vector<net::ObjectLow> Lows(const std::vector<SeenObject>& seen, uint32_t now);
        void Clear() { m_objects.clear(); m_reported.clear(); }
        size_t Known() const { return m_objects.size(); }

    private:
        struct Object
        {
            net::ObjectState host;
            bool playedAny = false;
            uint8_t playedCount = 0;
            const void* playedModel = nullptr;
        };
        static void Decide(uint32_t ref, Object& o, const LocalObject& here, std::vector<ObjectAction>& out);
        std::map<uint32_t, Object> m_objects;
        struct Reported { float health; uint32_t at; };
        std::map<uint32_t, Reported> m_reported;
    };
}
