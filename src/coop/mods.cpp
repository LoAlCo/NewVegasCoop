// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/mods.h"

#include "coop/quests.h"
#include "coop/sha256.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <windows.h>

namespace coop
{
    using net::ModFile;
    using net::ModKind;

    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool EndsWith(const std::string& s, const std::string& end)
        {
            return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
        }

        std::string Key(const ModFile& f) { return std::to_string(static_cast<int>(f.kind)) + Lower(f.name); }

        bool SameFile(const ModFile& a, const ModFile& b)
        {
            if (a.kind == ModKind::Archive && IsMadeFromOwnGame(a.name))
                return true;
            return a.size == b.size && (!a.hashed || !b.hashed || a.hash == b.hash);
        }
    }

    bool IsGameArchive(const std::string& archive)
    {
        std::string a = Lower(archive);
        return a == "update.bsa" || (a.rfind("fallout - ", 0) == 0 && EndsWith(a, ".bsa"));
    }

    bool IsMadeFromOwnGame(const std::string& name)
    {
        static const char* const kMasters[] = {"taleoftwowastelands.esm", "yupttw.esm", "fallout3.esm", "anchorage.esm",
            "thepitt.esm", "brokensteel.esm", "pointlookout.esm", "zeta.esm"};
        std::string n = Lower(name);
        for (const char* master : kMasters)
            if (n == master || ArchiveBelongsTo(n, master))
                return true;
        return false;
    }

    bool ArchiveBelongsTo(const std::string& archive, const std::string& plugin)
    {
        std::string a = Lower(archive), p = Lower(plugin);
        size_t dot = p.rfind('.');
        std::string base = dot == std::string::npos ? p : p.substr(0, dot);
        return a == base + ".bsa" || (a.rfind(base + " - ", 0) == 0 && EndsWith(a, ".bsa"));
    }

    net::Hash ModsFingerprint(const ModList& mods)
    {
        Sha256 h;
        for (const ModFile& f : mods)
        {
            if (f.kind == ModKind::Dll)
                continue;
            std::string name = Lower(f.name);
            uint8_t kind = static_cast<uint8_t>(f.kind), hashed = f.hashed ? 1 : 0;
            h.Add(&kind, 1);
            h.Add(name.c_str(), name.size() + 1);
            if (f.kind == ModKind::Archive && IsMadeFromOwnGame(f.name))
                continue;
            h.Add(&f.size, sizeof(f.size));
            h.Add(&hashed, 1);
            if (f.hashed)
                h.Add(f.hash.data(), f.hash.size());
        }
        return h.Finish();
    }

    ModDifference CompareMods(const ModList& host, const ModList& mine)
    {
        ModDifference d;
        std::map<std::string, const ModFile*> mineByKey;
        for (const ModFile& f : mine)
            mineByKey[Key(f)] = &f;
        std::set<std::string> hostKeys;
        bool pluginsDiffer = false;
        for (const ModFile& f : host)
        {
            hostKeys.insert(Key(f));
            bool dll = f.kind == ModKind::Dll;
            auto it = mineByKey.find(Key(f));
            if (it == mineByKey.end())
                (dll ? d.missingDlls : d.missing).push_back(f.name);
            else if (!SameFile(f, *it->second))
                (dll ? d.differentDlls : d.different).push_back(f.name);
            pluginsDiffer |= f.kind == ModKind::Plugin && it == mineByKey.end();
        }
        for (const ModFile& f : mine)
            if (f.kind != ModKind::Dll && !hostKeys.count(Key(f)))
            {
                d.extra.push_back(f.name);
                pluginsDiffer |= f.kind == ModKind::Plugin;
            }
        if (!pluginsDiffer)
        {
            std::vector<const ModFile*> a, b;
            for (const ModFile& f : host)
                if (f.kind == ModKind::Plugin)
                    a.push_back(&f);
            for (const ModFile& f : mine)
                if (f.kind == ModKind::Plugin)
                    b.push_back(&f);
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                if (Lower(a[i]->name) != Lower(b[i]->name))
                {
                    d.orderDiffers = true;
                    d.orderAt = i + 1;
                    d.orderHost = a[i]->name;
                    d.orderMine = b[i]->name;
                    break;
                }
            d.orderDiffers |= a.size() != b.size();
        }
        return d;
    }

    std::string DescribeForScreen(const ModDifference& d, const std::string& hostName)
    {
        std::string whose = hostName.empty() ? "the host's" : hostName + "'s";
        auto list = [](const std::vector<std::string>& names)
        {
            const size_t kShown = 4;
            std::string text;
            for (size_t i = 0; i < names.size() && i < kShown; ++i)
                text += (i ? ", " : "") + names[i];
            if (names.size() > kShown)
                text += " and " + std::to_string(names.size() - kShown) + " more";
            return text;
        };
        std::string text = d.Blocks() ? "Your mods don't match " + whose + " game, so you can't join yet."
                                      : "Joined, but your xNVSE plugins aren't " + whose + " (things may go out of sync).";
        if (!d.missing.empty())
            text += "\nMissing: " + list(d.missing);
        if (!d.different.empty())
            text += "\nDifferent version: " + list(d.different);
        if (!d.extra.empty())
            text += "\nNot in " + whose + " game (turn off): " + list(d.extra);
        if (d.orderDiffers && d.orderAt)
            text += "\nThe load order is different from plugin " + std::to_string(d.orderAt) + ": " + whose + " has " +
                d.orderHost + " there, yours " + d.orderMine + ".";
        else if (d.orderDiffers)
            text += "\nThe load order is different.";
        if (!d.missingDlls.empty())
            text += "\nxNVSE plugins to install by hand: " + list(d.missingDlls);
        if (!d.differentDlls.empty())
            text += "\nDifferent xNVSE plugin versions: " + list(d.differentDlls);
        return text + "\nThe full list is in NVCoop.log.";
    }

    std::vector<std::string> DescribeForLog(const ModDifference& d)
    {
        std::vector<std::string> lines;
        for (const std::string& n : d.missing)
            lines.push_back("missing: " + n);
        for (const std::string& n : d.different)
            lines.push_back("different version: " + n);
        for (const std::string& n : d.extra)
            lines.push_back("not in the host's game: " + n);
        if (d.orderDiffers && d.orderAt)
            lines.push_back("the load order is different from plugin " + std::to_string(d.orderAt) + ": the host has " +
                d.orderHost + " there, we have " + d.orderMine);
        else if (d.orderDiffers)
            lines.push_back("the load order is different");
        for (const std::string& n : d.missingDlls)
            lines.push_back("xNVSE plugin missing: " + n);
        for (const std::string& n : d.differentDlls)
            lines.push_back("xNVSE plugin, different version: " + n);
        return lines;
    }

    namespace
    {
        struct FileInfo
        {
            uint64_t size = 0;
            uint64_t time = 0;
        };

        bool Stat(const std::string& path, FileInfo& out)
        {
            WIN32_FILE_ATTRIBUTE_DATA data;
            if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                return false;
            out.size = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            out.time = (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
            return true;
        }

        std::vector<std::string> FilesIn(const std::string& folder, const char* pattern)
        {
            std::vector<std::string> names;
            WIN32_FIND_DATAA found;
            HANDLE h = FindFirstFileA((folder + pattern).c_str(), &found);
            if (h == INVALID_HANDLE_VALUE)
                return names;
            do
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    names.push_back(found.cFileName);
            while (FindNextFileA(h, &found) && names.size() < net::kMaxModFiles);
            FindClose(h);
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
            return names;
        }

        bool HashFile(const std::string& path, std::vector<uint8_t>& buffer, net::Hash& out)
        {
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                return false;
            Sha256 sha;
            DWORD read = 0;
            bool ok = true;
            while ((ok = ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != 0) && read)
                sha.Add(buffer.data(), read);
            CloseHandle(h);
            out = sha.Finish();
            return ok;
        }

        std::string Hex(const net::Hash& hash)
        {
            std::string s;
            char b[3];
            for (uint8_t v : hash)
            {
                std::snprintf(b, sizeof(b), "%02x", v);
                s += b;
            }
            return s;
        }

        bool FromHex(const std::string& s, net::Hash& out)
        {
            if (s.size() != out.size() * 2)
                return false;
            for (size_t i = 0; i < out.size(); ++i)
            {
                auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
                int high = digit(s[i * 2]), low = digit(s[i * 2 + 1]);
                if (high < 0 || low < 0)
                    return false;
                out[i] = static_cast<uint8_t>(high * 16 + low);
            }
            return true;
        }

        using HashCache = std::map<std::string, net::Hash>;
        std::string CacheKey(const ModFile& f, const FileInfo& info)
        {
            return std::to_string(static_cast<int>(f.kind)) + "\t" + Lower(f.name) + "\t" + std::to_string(info.size) +
                "\t" + std::to_string(info.time);
        }

        HashCache ReadCache(const std::string& file)
        {
            HashCache cache;
            FILE* f = nullptr;
            if (fopen_s(&f, file.c_str(), "r") != 0 || !f)
                return cache;
            char line[512];
            while (std::fgets(line, sizeof(line), f) && cache.size() < net::kMaxModFiles)
            {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                size_t tab = text.rfind('\t');
                net::Hash hash;
                if (tab != std::string::npos && FromHex(text.substr(tab + 1), hash))
                    cache[text.substr(0, tab)] = hash;
            }
            std::fclose(f);
            return cache;
        }

        void WriteCache(const std::string& file, const HashCache& cache)
        {
            FILE* f = nullptr;
            if (fopen_s(&f, file.c_str(), "w") != 0 || !f)
                return LOG_WARN("Mods: couldn't write %s", file.c_str());
            for (const auto& [key, hash] : cache)
                std::fprintf(f, "%s\t%s\n", key.c_str(), Hex(hash).c_str());
            std::fclose(f);
        }
    }

    ModList ScanMods(const std::string& dataFolder, const std::vector<std::string>& plugins,
        const std::string& cacheFile, const std::string& skipDll)
    {
        std::string data = dataFolder;
        if (!data.empty() && data.back() != '\\' && data.back() != '/')
            data += '\\';
        const std::string dllFolder = data + "NVSE\\Plugins\\";

        ModList mods;
        std::vector<std::string> paths;
        auto add = [&](const std::string& folder, const std::string& name, ModKind kind, bool hashed)
        {
            FileInfo info;
            if (mods.size() >= net::kMaxModFiles || !net::IsPlainModFileName(name, kind) || !Stat(folder + name, info))
                return LOG_WARN("Mods: skipping %s (not found, or a name we can't share)", name.c_str());
            ModFile f;
            f.name = name;
            f.kind = kind;
            f.size = info.size;
            f.hashed = hashed;
            mods.push_back(f);
            paths.push_back(folder + name);
        };

        for (const std::string& plugin : plugins)
            add(data, plugin, ModKind::Plugin, !IsGameFile(plugin) && !IsMadeFromOwnGame(plugin));
        for (const std::string& archive : FilesIn(data, "*.bsa"))
        {
            if (Lower(archive).find("invalidat") != std::string::npos)
                continue;
            bool game = IsGameArchive(archive) || IsMadeFromOwnGame(archive), loaded = IsGameArchive(archive);
            for (const std::string& plugin : plugins)
                if (ArchiveBelongsTo(archive, plugin))
                {
                    loaded = true;
                    game |= IsGameFile(plugin);
                }
            if (loaded)
                add(data, archive, ModKind::Archive, !game);
        }
        for (const std::string& dll : FilesIn(dllFolder, "*.dll"))
            if (_stricmp(dll.c_str(), skipDll.c_str()) != 0)
                add(dllFolder, dll, ModKind::Dll, true);

        HashCache cache = ReadCache(cacheFile), used;
        std::vector<uint8_t> buffer;
        size_t read = 0;
        uint64_t readBytes = 0;
        for (size_t i = 0; i < mods.size(); ++i)
        {
            ModFile& f = mods[i];
            FileInfo info;
            if (!f.hashed || !Stat(paths[i], info))
                continue;
            std::string key = CacheKey(f, info);
            if (auto it = cache.find(key); it != cache.end())
                f.hash = it->second;
            else
            {
                if (buffer.empty())
                    buffer.resize(1 << 20);
                if (!HashFile(paths[i], buffer, f.hash))
                {
                    LOG_WARN("Mods: couldn't read %s", paths[i].c_str());
                    continue;
                }
                ++read;
                readBytes += f.size;
            }
            used[key] = f.hash;
        }
        if (read)
            LOG_INFO("Mods: fingerprinted %zu changed files (%.1f MB)", read, readBytes / 1048576.0);
        if (used != cache)
            WriteCache(cacheFile, used);
        return mods;
    }
}
