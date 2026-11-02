// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/mods.h"
#include "coop/sha256.h"
#include "net/protocol.h"
#include "net/session.h"

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace coop
{
    class ModServer
    {
    public:
        struct Piece
        {
            net::PeerId peer = 0;
            net::ModPiece piece;
        };
        static constexpr size_t kMaxWaiting = 32;

        ~ModServer();
        bool Ask(net::PeerId peer, uint16_t index, const std::string& path, uint64_t offset, uint32_t length);
        bool Take(Piece& out);
        void Forget(net::PeerId peer);

    private:
        struct Job
        {
            net::PeerId peer;
            uint16_t index;
            std::string path;
            uint64_t offset;
            uint32_t length;
        };
        void Run();
        size_t WaitingFor(net::PeerId peer) const;

        std::thread m_thread;
        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<Job> m_jobs;
        std::deque<Piece> m_done;
        bool m_stop = false;
    };

    class ModDownload
    {
    public:
        struct Wanted
        {
            uint16_t index = 0;
            net::ModFile file;
        };
        static constexpr size_t kAhead = 16;

        ~ModDownload() { Close(); }
        bool Start(std::vector<Wanted> files, const std::string& folder);
        void Asks(std::vector<net::ModFileAsk>& out);
        bool Got(const net::ModPiece& piece);
        bool Fail(const std::string& why);

        bool Done() const { return !m_failed && m_current >= m_files.size(); }
        bool Failed() const { return m_failed; }
        const std::string& Error() const { return m_error; }
        const std::vector<std::string>& Finished() const { return m_finished; }
        size_t FilesTotal() const { return m_files.size(); }
        uint64_t BytesTotal() const;
        uint64_t BytesDone() const { return m_bytesDone; }

    private:
        bool OpenCurrent();
        bool FinishCurrent();
        void Close();

        std::vector<Wanted> m_files;
        std::string m_folder;
        size_t m_current = 0;
        FILE* m_out = nullptr;
        Sha256 m_hash;
        uint64_t m_received = 0;
        uint64_t m_nextAsk = 0;
        size_t m_waiting = 0;
        uint64_t m_bytesDone = 0;
        std::vector<std::string> m_finished;
        bool m_failed = false;
        std::string m_error;
    };

    struct ModFolders
    {
        std::string data;
        std::string downloads;
        std::string backup;
        std::string pluginsTxt;
        std::string manager;
    };

    bool ApplyModFix(const ModFolders& folders, const ModList& host, const std::vector<std::string>& downloaded,
        const std::vector<std::string>& extra, std::string& error);
}
