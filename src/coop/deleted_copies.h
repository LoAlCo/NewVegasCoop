// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    constexpr uint32_t kDeletedCopyRetryMs = 1000;
    constexpr uint32_t kDeletedCopyForceMs = 5000;
    constexpr size_t kMaxDeletedCopies = 256;

    enum class DeletedCopyStep : uint8_t
    {
        Wait,        // asked to go; give the game a moment
        RemoveAgain, // still switched on: ask again
        ForceOff,    // still on after a while: switch it off past anything that refuses
        Gone,        // deleted or off for good: stop watching
    };

    // Copies of the host's NPCs we deleted. The game switches a reference off later in the frame, so for a moment a
    // deleted copy still looks like a live NPC of our own; some never went and came back as strays. This keeps
    // watching each until it's really gone.
    class DeletedCopies
    {
    public:
        void Deleted(uint32_t localId, uint32_t now)
        {
            if (m_since.size() < kMaxDeletedCopies || m_since.count(localId))
                m_since[localId] = now;
        }
        bool Contains(uint32_t localId) const { return m_since.count(localId) != 0; }

        // exists: the reference is still there and not marked deleted; enabled: it's switched on
        DeletedCopyStep Look(uint32_t localId, uint32_t now, bool exists, bool enabled)
        {
            auto it = m_since.find(localId);
            if (it == m_since.end())
                return DeletedCopyStep::Gone;
            uint32_t age = now - it->second;
            if (!exists)
            {
                m_since.erase(it);
                return DeletedCopyStep::Gone;
            }
            if (!enabled)
                return DeletedCopyStep::Wait;
            if (age >= kDeletedCopyForceMs)
                return DeletedCopyStep::ForceOff;
            if (age >= kDeletedCopyRetryMs)
                return DeletedCopyStep::RemoveAgain;
            return DeletedCopyStep::Wait;
        }

        std::vector<uint32_t> Ids() const
        {
            std::vector<uint32_t> ids;
            for (const auto& [id, _] : m_since)
                ids.push_back(id);
            return ids;
        }
        void Clear() { m_since.clear(); }

    private:
        std::map<uint32_t, uint32_t> m_since;
    };
}
