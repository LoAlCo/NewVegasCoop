// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/mod_transfer.h"

#include "log.h"

#include <windows.h>

namespace coop
{
    using net::ModFile;
    using net::kModPieceSize;

    namespace
    {
        std::string WithSlash(std::string folder)
        {
            if (!folder.empty() && folder.back() != '\\' && folder.back() != '/')
                folder += '\\';
            return folder;
        }

        bool MakeFolders(const std::string& folder)
        {
            std::string path = WithSlash(folder);
            for (size_t i = path.find_first_of("\\/", 3); i != std::string::npos; i = path.find_first_of("\\/", i + 1))
            {
                std::string part = path.substr(0, i);
                if (!CreateDirectoryA(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                    return false;
            }
            return true;
        }

        bool Exists(const std::string& path)
        {
            DWORD a = GetFileAttributesA(path.c_str());
            return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        }

        bool Move(const std::string& from, const std::string& to)
        {
            return MoveFileExA(from.c_str(), to.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
        }

        bool ReadPiece(HANDLE file, uint64_t offset, uint32_t length, net::Bytes& out)
        {
            LARGE_INTEGER at;
            at.QuadPart = static_cast<LONGLONG>(offset);
            out.resize(length);
            DWORD read = 0;
            return SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && ReadFile(file, out.data(), length, &read, nullptr) &&
                read == length;
        }
    }

    ModServer::~ModServer()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    size_t ModServer::WaitingFor(net::PeerId peer) const
    {
        size_t n = 0;
        for (const Job& j : m_jobs)
            n += j.peer == peer;
        for (const Piece& p : m_done)
            n += p.peer == peer;
        return n;
    }

    bool ModServer::Ask(net::PeerId peer, uint16_t index, const std::string& path, uint64_t offset, uint32_t length)
    {
        {
            std::lock_guard lock(m_mutex);
            if (WaitingFor(peer) >= kMaxWaiting)
                return false;
            m_jobs.push_back({peer, index, path, offset, length, m_gen});
            if (!m_thread.joinable())
                m_thread = std::thread(&ModServer::Run, this);
        }
        m_wake.notify_one();
        return true;
    }

    bool ModServer::Take(Piece& out)
    {
        std::lock_guard lock(m_mutex);
        if (m_done.empty())
            return false;
        out = std::move(m_done.front());
        m_done.pop_front();
        return true;
    }

    void ModServer::Forget(net::PeerId peer)
    {
        std::lock_guard lock(m_mutex);
        for (size_t i = m_jobs.size(); i-- > 0;)
            if (m_jobs[i].peer == peer)
                m_jobs.erase(m_jobs.begin() + i);
        for (size_t i = m_done.size(); i-- > 0;)
            if (m_done[i].peer == peer)
                m_done.erase(m_done.begin() + i);
    }

    void ModServer::Clear()
    {
        std::lock_guard lock(m_mutex);
        ++m_gen;
        m_jobs.clear();
        m_done.clear();
    }

    void ModServer::Run()
    {
        HANDLE file = INVALID_HANDLE_VALUE;
        std::string openPath;
        for (;;)
        {
            Job job;
            {
                std::unique_lock lock(m_mutex);
                if (m_jobs.empty() && file != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(file);
                    file = INVALID_HANDLE_VALUE;
                }
                m_wake.wait(lock, [&] { return m_stop || !m_jobs.empty(); });
                if (m_stop)
                    break;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            if (file == INVALID_HANDLE_VALUE || job.path != openPath)
            {
                if (file != INVALID_HANDLE_VALUE)
                    CloseHandle(file);
                file = CreateFileA(job.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                openPath = job.path;
            }
            Piece piece;
            piece.peer = job.peer;
            piece.piece.index = job.index;
            piece.piece.offset = job.offset;
            piece.piece.ok = file != INVALID_HANDLE_VALUE && ReadPiece(file, job.offset, job.length, piece.piece.data);
            if (!piece.piece.ok)
            {
                piece.piece.data.clear();
                LOG_WARN("Mods: couldn't read %s at %llu", job.path.c_str(), static_cast<unsigned long long>(job.offset));
            }
            std::lock_guard lock(m_mutex);
            if (job.gen == m_gen)
                m_done.push_back(std::move(piece));
        }
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
    }

    bool ModDownload::Start(std::vector<Wanted> files, const std::string& folder)
    {
        Close();
        m_files = std::move(files);
        m_folder = WithSlash(folder);
        m_current = 0;
        m_bytesDone = 0;
        m_finished.clear();
        m_failed = false;
        m_error.clear();
        if (!MakeFolders(m_folder))
            return Fail("couldn't make the folder " + m_folder);
        return m_files.empty() || OpenCurrent();
    }

    uint64_t ModDownload::BytesTotal() const
    {
        uint64_t total = 0;
        for (const Wanted& w : m_files)
            total += w.file.size;
        return total;
    }

    bool ModDownload::Fail(const std::string& why)
    {
        if (m_current < m_files.size())
        {
            Close();
            DeleteFileA((m_folder + m_files[m_current].file.name + ".part").c_str());
        }
        m_failed = true;
        m_error = why;
        return false;
    }

    void ModDownload::Close()
    {
        if (m_out)
            std::fclose(m_out);
        m_out = nullptr;
    }

    bool ModDownload::OpenCurrent()
    {
        const ModFile& f = m_files[m_current].file;
        if (fopen_s(&m_out, (m_folder + f.name + ".part").c_str(), "wb") != 0 || !m_out)
        {
            m_out = nullptr;
            return Fail("couldn't write " + m_folder + f.name);
        }
        m_hash = Sha256();
        m_received = m_nextAsk = 0;
        m_waiting = 0;
        return f.size > 0 || FinishCurrent();
    }

    bool ModDownload::FinishCurrent()
    {
        const ModFile& f = m_files[m_current].file;
        Close();
        if (m_hash.Finish() != f.hash)
            return Fail(f.name + " didn't match the host's list");
        std::string done = m_folder + f.name;
        if (!Move(done + ".part", done))
            return Fail("couldn't write " + done);
        m_finished.push_back(f.name);
        ++m_current;
        return m_current >= m_files.size() || OpenCurrent();
    }

    void ModDownload::Asks(std::vector<net::ModFileAsk>& out)
    {
        if (m_failed || m_current >= m_files.size())
            return;
        const Wanted& w = m_files[m_current];
        for (; m_waiting < kAhead && m_nextAsk < w.file.size; m_nextAsk += kModPieceSize, ++m_waiting)
            out.push_back({w.index, m_nextAsk});
    }

    bool ModDownload::Got(const net::ModPiece& piece)
    {
        if (m_failed || m_current >= m_files.size())
            return Fail("a piece of a mod file nobody asked for");
        const Wanted& w = m_files[m_current];
        uint64_t left = w.file.size - m_received;
        size_t expected = left < kModPieceSize ? static_cast<size_t>(left) : kModPieceSize;
        if (!piece.ok)
            return Fail("the host couldn't read " + w.file.name);
        if (piece.index != w.index || piece.offset != m_received || piece.data.size() != expected || !m_waiting)
            return Fail("a wrong piece of " + w.file.name);
        if (std::fwrite(piece.data.data(), 1, piece.data.size(), m_out) != piece.data.size())
            return Fail("couldn't write " + m_folder + w.file.name);
        m_hash.Add(piece.data.data(), piece.data.size());
        m_received += piece.data.size();
        m_bytesDone += piece.data.size();
        --m_waiting;
        return m_received < w.file.size || FinishCurrent();
    }

    bool ApplyModFix(const ModFolders& folders, const ModList& host, const std::vector<std::string>& downloaded,
        const std::vector<std::string>& extra, std::string& error)
    {
        const std::string data = WithSlash(folders.data), downloads = WithSlash(folders.downloads),
            backup = WithSlash(folders.backup);
        if (!MakeFolders(backup))
            return error = "couldn't make the backup folder " + backup, false;

        for (const std::string& name : downloaded)
        {
            bool hadOld = Exists(data + name);
            if (hadOld && !Move(data + name, backup + name))
                return error = "couldn't move the old " + name + " to " + backup, false;
            if (!Move(downloads + name, data + name))
            {
                if (hadOld)
                    Move(backup + name, data + name);
                return error = "couldn't put " + name + " in Data", false;
            }
            LOG_INFO("Mods: put %s in Data", name.c_str());
        }
        for (const std::string& name : extra)
            if (net::IsPlainModFileName(name, net::ModKind::Archive) && Exists(data + name))
            {
                if (!Move(data + name, backup + name))
                    return error = "couldn't move " + name + " to " + backup, false;
                LOG_INFO("Mods: moved %s (not in the host's game) to the backup", name.c_str());
            }

        if (Exists(folders.pluginsTxt) && !CopyFileA(folders.pluginsTxt.c_str(), (backup + "plugins.txt").c_str(), FALSE))
            return error = "couldn't back up " + folders.pluginsTxt, false;
        FILE* f = nullptr;
        if (fopen_s(&f, folders.pluginsTxt.c_str(), "w") != 0 || !f)
            return error = "couldn't write " + folders.pluginsTxt, false;
        std::fprintf(f, "# Set by New Vegas Co-op to match the host's mods (the old list is in %s)\n", backup.c_str());
        for (const ModFile& m : host)
            if (m.kind == net::ModKind::Plugin)
                std::fprintf(f, "%s\n", m.name.c_str());
        std::fclose(f);

        SYSTEMTIME release = {2010, 10, 2, 19, 12, 0, 0, 0};
        FILETIME start;
        SystemTimeToFileTime(&release, &start);
        uint64_t when = (uint64_t(start.dwHighDateTime) << 32) | start.dwLowDateTime;
        for (const ModFile& m : host)
        {
            if (m.kind != net::ModKind::Plugin)
                continue;
            FILETIME ft{static_cast<DWORD>(when), static_cast<DWORD>(when >> 32)};
            when += 60ull * 10000000;
            HANDLE h = CreateFileA((data + m.name).c_str(), FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE || !SetFileTime(h, nullptr, nullptr, &ft))
                LOG_WARN("Mods: couldn't date %s for the load order", m.name.c_str());
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        return true;
    }
}
