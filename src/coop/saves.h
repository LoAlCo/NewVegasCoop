// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace coop
{
    constexpr size_t kMaxSaveName = 100;
    constexpr size_t kMaxSaveHostName = 32;

    // Which game a save belongs to. Names can't say: autosaves take turns reusing the same few names, so one name can
    // hold a different character later. `game` stays the same for a whole playthrough and `save` is new each save.
    // Both are random and kept in a small file beside the save. 0 means not known.
    struct SaveTag
    {
        uint64_t game = 0;
        uint64_t save = 0;
    };
    inline bool operator==(const SaveTag& a, const SaveTag& b) { return a.game == b.game && a.save == b.save; }

    inline uint64_t ParseHex64(const std::string& text)
    {
        char* end = nullptr;
        uint64_t value = std::strtoull(text.c_str(), &end, 16);
        return text.size() == 16 && end == text.c_str() + text.size() ? value : 0ull;
    }

    // The file beside a save that holds its tag.
    inline std::string SaveTagFileName(std::string_view file)
    {
        size_t slash = file.find_last_of("/\\");
        if (slash != std::string_view::npos)
            file.remove_prefix(slash + 1);
        if (file.size() > 4 && (file.substr(file.size() - 4) == ".fos" || file.substr(file.size() - 4) == ".FOS"))
            file.remove_suffix(4);
        return std::string(file) + ".nvcoopid";
    }

    // `written` is when the tag file was written, in Windows file time (100 ns steps).
    inline std::string FormatSaveTagFile(const SaveTag& tag, uint64_t written)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%016llx %016llx %016llx\n", static_cast<unsigned long long>(tag.game),
            static_cast<unsigned long long>(tag.save), static_cast<unsigned long long>(written));
        return text;
    }

    // The tag of a save whose file was last written at `saveWritten`. The game finishes writing the save just after
    // the tag file is written, so a save much newer was written over without us, and one older than its tag file was
    // never written or was moved here from another name: neither is the save the tag was made for.
    inline SaveTag SaveTagFromFile(std::string_view text, uint64_t saveWritten)
    {
        constexpr uint64_t kSecond = 10000000, kSaveOlderBy = 5 * kSecond, kSaveNewerBy = 60 * kSecond;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.remove_suffix(1);
        if (text.size() != 50 || text[16] != ' ' || text[33] != ' ')
            return {};
        SaveTag tag{ParseHex64(std::string(text.substr(0, 16))), ParseHex64(std::string(text.substr(17, 16)))};
        uint64_t written = ParseHex64(std::string(text.substr(34, 16)));
        if (tag.game == 0 || written == 0 || saveWritten + kSaveOlderBy < written || saveWritten > written + kSaveNewerBy)
            return {};
        return tag;
    }

    // One of our saves, and the host's save and game it was made alongside.
    struct SaveLink
    {
        std::string theirs;
        SaveTag tag;
    };

    struct SaveOnDisk
    {
        std::string name;
        SaveTag tag;
        uint64_t written = 0;
    };

    // The save to load into the host's game: the one made alongside the very save they're playing, otherwise our
    // newest from the same game (they saved without us). Nothing from another game, or when we can't tell.
    inline std::string PickLinkedSave(const std::vector<SaveOnDisk>& ours, const SaveTag& host)
    {
        if (host.game == 0)
            return "";
        const SaveOnDisk* best = nullptr;
        bool bestExact = false;
        for (const SaveOnDisk& s : ours)
        {
            if (s.tag.game != host.game)
                continue;
            bool exact = host.save != 0 && s.tag.save == host.save;
            if (!best || (exact && !bestExact) || (exact == bestExact && s.written > best->written))
            {
                best = &s;
                bestExact = exact;
            }
        }
        return best ? best->name : "";
    }

    inline std::string SaveNameOf(std::string_view file)
    {
        if (file.size() > 4 && (file.substr(file.size() - 4) == ".fos" || file.substr(file.size() - 4) == ".FOS"))
            file.remove_suffix(4);
        if (file.empty() || file.size() > kMaxSaveName || file.find("..") != std::string_view::npos)
            return "";
        for (char c : file)
        {
            bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
                c == '-' || c == '_' || c == '.' || c == ',' || c == '(' || c == ')' || c == '\'';
            if (!plain)
                return "";
        }
        if (file.front() == ' ' || file.front() == '.' || file.back() == ' ' || file.back() == '.')
            return "";
        return std::string(file);
    }

    inline std::string PlainName(std::string_view name, const char* otherwise)
    {
        std::string who;
        for (char c : name)
            if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                    c == '_') && who.size() < kMaxSaveHostName)
                who += c;
        while (!who.empty() && who.back() == ' ')
            who.pop_back();
        while (!who.empty() && who.front() == ' ')
            who.erase(who.begin());
        return who.empty() ? otherwise : who;
    }

    // A line of NVCoopLinkedSaves.txt: host, our save, their save, then the tag. Lines from before tags have only the
    // first three: they still say which host's save goes with ours, but not which game, so they never auto-load.
    inline std::string FormatSaveLink(std::string_view host, std::string_view own, const SaveLink& link)
    {
        char tag[40];
        std::snprintf(tag, sizeof(tag), "%016llx\t%016llx", static_cast<unsigned long long>(link.tag.game),
            static_cast<unsigned long long>(link.tag.save));
        return std::string(host) + "\t" + std::string(own) + "\t" + link.theirs + "\t" + tag;
    }

    inline bool ParseSaveLink(std::string_view line, std::string& host, std::string& own, SaveLink& link)
    {
        std::vector<std::string> parts;
        for (size_t start = 0;;)
        {
            size_t tab = line.find('\t', start);
            parts.emplace_back(line.substr(start, tab == std::string_view::npos ? tab : tab - start));
            if (tab == std::string_view::npos)
                break;
            start = tab + 1;
        }
        if (parts.size() != 3 && parts.size() != 5)
            return false;
        if (parts[0].empty() || PlainName(parts[0], "") != parts[0] || parts[1].empty() || SaveNameOf(parts[1]) != parts[1] ||
            parts[2].empty() || SaveNameOf(parts[2]) != parts[2])
            return false;
        host = parts[0];
        own = parts[1];
        link = SaveLink{parts[2], {}};
        if (parts.size() == 5)
        {
            link.tag.game = ParseHex64(parts[3]);
            link.tag.save = ParseHex64(parts[4]);
        }
        return true;
    }

    inline std::string LinkedSavePrefix(std::string_view host) { return "Co-op - " + PlainName(host, "Host") + " - "; }

    inline std::string LinkedSaveName(std::string_view host, std::string_view hostSave)
    {
        std::string save = SaveNameOf(hostSave);
        return save.empty() ? "" : LinkedSavePrefix(host) + save;
    }

    inline std::string SaveForFriend(std::string_view friendName, std::string_view when)
    {
        return SaveNameOf("Co-op " + PlainName(friendName, "Friend") + " " + std::string(when));
    }

    inline std::string HostSaveOf(std::string_view host, std::string_view friendSave)
    {
        std::string name = SaveNameOf(friendSave), prefix = LinkedSavePrefix(host);
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
            return "";
        return SaveNameOf(name.substr(prefix.size()));
    }

    inline bool IsAutosave(std::string_view save)
    {
        constexpr std::string_view kAuto = "autosave";
        if (save.size() < kAuto.size())
            return false;
        for (size_t i = 0; i < kAuto.size(); ++i)
            if ((save[i] | 0x20) != kAuto[i])
                return false;
        return true;
    }
}
