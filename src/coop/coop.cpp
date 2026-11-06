// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/coop.h"

#include "coop/clock.h"

#include "log.h"
#include "net/both.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <windows.h>

namespace coop
{
    using namespace net;

    bool Coop::Host(uint16_t port, const std::string& myName)
    {
        if (!m_enet.StartHost(port, kMaxFriends + 2))
            return false;
        m_net = &m_enet;
        m_role = Role::Host;
        m_myName = myName;
        m_myId = 0;
        LOG_INFO("Hosting on port %u as %s", port, myName.c_str());
        return true;
    }

    bool Coop::Join(const std::string& address, uint16_t port, const std::string& myName, bool checkModsOnly)
    {
        if (!m_enet.StartClient(address, port))
            return false;
        m_net = &m_enet;
        m_role = Role::Friend;
        m_myName = myName;
        m_friendState = FriendState::Connecting;
        m_checkOnly = checkModsOnly;
        m_modsMatched = false;
        LOG_INFO("%s the host by IP (port %u) as %s", checkModsOnly ? "Comparing mods with" : "Joining", port,
            myName.c_str());
        return true;
    }

    void Coop::HostOn(std::unique_ptr<Transport> started, const std::string& myName)
    {
        m_other = std::move(started);
        m_net = m_other.get();
        m_role = Role::Host;
        m_myName = myName;
        m_myId = 0;
        LOG_INFO("Hosting as %s", myName.c_str());
    }

    bool Coop::HostBoth(uint16_t port, std::unique_ptr<Transport> started, const std::string& myName)
    {
        if (!m_enet.StartHost(port, kMaxFriends + 2))
            return false;
        HostOn(std::make_unique<Both>(m_enet, std::move(started)), myName);
        LOG_INFO("Hosting on port %u too", port);
        return true;
    }

    void Coop::JoinOn(std::unique_ptr<Transport> started, const std::string& myName, bool checkModsOnly)
    {
        m_other = std::move(started);
        m_net = m_other.get();
        m_role = Role::Friend;
        m_myName = myName;
        m_friendState = FriendState::Connecting;
        m_checkOnly = checkModsOnly;
        m_modsMatched = false;
        LOG_INFO("%s as %s", checkModsOnly ? "Comparing mods" : "Joining", myName.c_str());
    }

    void Coop::Stop()
    {
        if (m_role != Role::None)
            LOG_INFO("Leaving co-op");
        m_net->Stop();
        m_net = &m_enet;
        m_other.reset();
        m_role = Role::None;
        m_players.clear();
        m_remotes.clear();
        m_hasPose = false;
        m_gearToSend = !m_myGear.empty();
        m_friendState = FriendState::Disconnected;
        m_hostMods.clear();
        m_hostModsDone = m_modsDiffer = m_downloading = false;
        m_explosions.clear();
        m_downedSince.clear();
        m_drags.clear();
        m_dragSince.clear();
        m_attacks.clear();
        m_gameOverSent = m_revived = m_gameOver = false;
        m_hostSaysFighting = false;
        m_hasPlace = m_expecting = m_hasTravel = m_hasGroupPlace = m_regroup = false;
        m_myNpcs.clear();
        m_sentNpcs.clear();
        m_npcShots.clear();
        m_npcGear.clear();
        m_npcAttacks.clear();
        m_npcs.clear();
        m_npcListAt = m_npcListSpace = 0;
        m_myDoors.clear();
        m_sentDoors.clear();
        m_doorUses.clear();
        m_friendDoors.Clear();
        m_views.clear();
        m_itemChanges.clear();
        m_friendContainer.Reset();
        m_refunds.clear();
        m_myItems.clear();
        m_sentItems.clear();
        m_itemTakes.clear();
        m_itemDrops.clear();
        m_itemMoves.clear();
        m_lastMoveSent.clear();
        m_desyncs.Clear();
        m_desyncsSent = {};
        m_myCompanions.clear();
        m_companionsChanged = false;
        m_companionOrders.clear();
        m_companions.clear();
        m_playerCommands.clear();
        m_chat.clear();
        m_voice.clear();
        m_radioLines.clear();
        m_radioStartedAt.clear();
        m_desyncNotices.clear();
        m_friendItems.Clear();
        m_questStates.clear();
        m_refEnables.clear();
        m_progress.clear();
        m_reputations.clear();
        m_travelMarkers.clear();
        m_teleports.clear();
        m_anims.clear();
        m_modAnims.clear();
        m_looks.clear();
        ++m_looksChanges;
        m_repGains.clear();
        m_talk.Clear();
        m_wait = WaitVote{};
        m_waitEvents.clear();
        m_saveOrders.clear();
        m_saveAsks.clear();
        m_loads = LoadGate{};
        m_frozen = false;
        m_loadRefused = false;
        m_hasGameTime = m_hostTimeNew = false;
        m_dialogueLines.clear();
        m_subtitles.clear();
    }

    void Coop::ForgetMyPlace()
    {
        m_hasPose = m_hasPlace = m_expecting = m_hasTravel = false;
        m_regroup = InGame();
    }

    void Coop::Update()
    {
        if (m_role == Role::None)
            return;

        Event ev;
        while (m_net->Poll(ev))
        {
            switch (ev.kind)
            {
            case Event::Kind::Connected:
                if (m_role == Role::Host)
                {
                    m_players[ev.peer] = Player{0, "", false, GetTickCount()};
                    m_players[ev.peer].from = ev.from;
                    LOG_DEBUG("Connection %u opened, waiting for hello", ev.peer);
                }
                else
                {
                    m_friendState = FriendState::WaitingForWelcome;
                    m_hostMods.clear();
                    m_hostModsDone = m_modsDiffer = false;
                    m_net->Send(ev.peer, Encode(Hello{kProtocolVersion, m_myName, !m_mods.empty(), m_modsPrint, m_checkOnly}));
                    LOG_INFO("Connected to host, saying hello");
                }
                break;

            case Event::Kind::Disconnected:
                if (m_role == Role::Host)
                    RemovePlayer(ev.peer);
                else
                {
                    if (m_friendState == FriendState::Connecting)
                        LOG_WARN("Couldn't reach the host");
                    else if (m_friendState != FriendState::Rejected)
                        LOG_INFO("Disconnected from host");
                    if (m_friendState != FriendState::Rejected)
                        m_friendState = FriendState::Disconnected;
                    if (m_downloading)
                    {
                        m_downloading = false;
                        m_download.Fail("lost the connection to the host");
                        m_modFixResult = "Couldn't get the host's mods: lost the connection. Nothing was changed.";
                    }
                    m_frozen = false;
                    m_remotes.clear();
                    m_npcs.clear();
                    m_npcListAt = m_npcListSpace = 0;
                    m_looks.clear();
                    ++m_looksChanges;
                    m_friendContainer.Reset();
                }
                break;

            case Event::Kind::Received:
                if (m_role == Role::Host)
                    HostReceived(ev.peer, ev.data);
                else
                    FriendReceived(ev.data);
                break;
            }
            if (m_role == Role::None)
                return;
        }

        uint32_t now = GetTickCount();
        if (m_role == Role::Host)
        {
            std::vector<PeerId> silent;
            std::vector<PeerId> unanswered;
            for (auto& [peer, p] : m_players)
                if (p.awaitingApproval)
                {
                    if (now - p.askedAt > kApprovalTimeoutMs)
                        unanswered.push_back(peer);
                }
                else if (!p.joined && (p.fixingMods ? now - p.lastAsk > kModIdleMs : now - p.connectedAt > kHelloTimeoutMs))
                    silent.push_back(peer);
            for (PeerId peer : silent)
                Kick(peer, "No hello received");
            for (PeerId peer : unanswered)
                Kick(peer, "The host didn't answer");
            ModServer::Piece piece;
            while (m_modServer.Take(piece))
                m_net->Send(piece.peer, Encode(piece.piece));
            CheckDowned(now);
            CheckWaitVote(now);
            UpdateFreeze(now);
            if (m_hasGameTime && now - m_lastGameTimeSent >= kGameTimeEveryMs)
            {
                m_lastGameTimeSent = now;
                SendToJoined(Encode(m_myGameTime), true);
            }
        }

        if (m_role == Role::Friend && m_downloading)
        {
            UpdateDownload();
            if (m_role == Role::None)
                return;
        }
        if (InGame())
            CheckMyTravel(now);
        if (InGame() && m_hasMyLooks && !m_myLooksSent)
        {
            m_myLooksSent = true;
            net::PlayerLooks mine = m_myLooks;
            mine.player = m_myId;
            if (m_role == Role::Host)
                SendToJoined(Encode(mine), true);
            else
                m_net->Send(1, Encode(mine));
        }
        if (InGame() && !m_myModAnimsSent)
        {
            m_myModAnimsSent = true;
            if (m_role == Role::Host)
                SendToJoined(Encode(PlayerModAnims{kHostPlayerId, m_myModAnims}), true);
            else
                m_net->Send(1, Encode(PlayerModAnims{m_myId, m_myModAnims}));
        }
        if (InGame() && !m_myAnimSent)
        {
            m_myAnimSent = true;
            if (m_role == Role::Host)
                SendToJoined(Encode(PlayerAnim{kHostPlayerId, m_myAnim}), true);
            else
                m_net->Send(1, Encode(PlayerAnim{m_myId, m_myAnim}));
        }
        SendMyPose(now);
        SendMyGear();
        SendMyHealth(now);
        if (m_role == Role::Host)
        {
            SendNpcs(now);
            SendDoors(now);
            SendItems(now);
            SendCompanions(now);
            if (!m_radioLines.empty() && IsDue(now, m_nextRadioResend))
            {
                m_nextRadioResend = now + 10000;
                for (auto& [station, line] : m_radioLines)
                {
                    uint32_t elapsed = now - m_radioStartedAt[station];
                    line.elapsed = elapsed < 3600000 ? elapsed : 3599999;
                    SendToJoined(Encode(line), true);
                }
            }
        }
        else
        {
            ForgetOldNpcs(now);
            SweepDesyncs(now);
        }

        if (now - m_lastPingLog > 30000)
        {
            m_lastPingLog = now;
            if (m_role == Role::Host)
            {
                for (auto& [peer, p] : m_players)
                    if (p.joined)
                        LOG_INFO("Ping to %s: %u ms", p.name.c_str(), m_net->RoundTripMs(peer));
                if (m_npcUpdatesSent)
                    LOG_INFO("NPCs: sent %u updates (%u bytes to each friend) in 30 s; sharing %zu now",
                        m_npcUpdatesSent, m_npcBytesSent, m_sentNpcs.size());
                m_npcUpdatesSent = m_npcBytesSent = 0;
                if (m_playerCommandsSent)
                    LOG_INFO("Scripted: sent %u commands (%u bytes to each friend) in 30 s", m_playerCommandsSent,
                        m_playerCommandBytesSent);
                m_playerCommandsSent = m_playerCommandBytesSent = 0;
            }
            else if (m_friendState == FriendState::Joined)
            {
                LOG_INFO("Ping to host: %u ms", m_net->RoundTripMs(1));
                LOG_INFO("NPCs: the host is sharing %zu", m_npcs.size());
            }
        }
    }

    void Coop::SetMyAnim(const std::string& path)
    {
        if (path == m_myAnim || (!path.empty() && !IsAnimPath(path)))
            return;
        m_myAnim = path;
        m_myAnimSent = false;
    }

    void Coop::HeardAnim(uint8_t player, const std::string& path)
    {
        auto it = m_anims.find(player);
        if (path.empty() ? it == m_anims.end() : it != m_anims.end() && it->second == path)
            return;
        if (path.empty())
            m_anims.erase(it);
        else if (it != m_anims.end() || m_anims.size() < 8)
            m_anims[player] = path;
        else
            return;
        ++m_animChanges;
        LOG_DEBUG("Animations: %s's character %s %s", NameOf(player).c_str(), path.empty() ? "stopped" : "plays",
            path.c_str());
    }

    void Coop::SetMyModAnims(const std::vector<std::string>& paths)
    {
        std::vector<std::string> checked;
        for (const std::string& path : paths)
            if (IsAnimPath(path) && checked.size() < kMaxModAnims)
                checked.push_back(path);
        if (checked == m_myModAnims)
            return;
        m_myModAnims = std::move(checked);
        m_myModAnimsSent = false;
    }

    void Coop::HeardModAnims(uint8_t player, const std::vector<std::string>& paths)
    {
        auto it = m_modAnims.find(player);
        if (paths.empty() ? it == m_modAnims.end() : it != m_modAnims.end() && it->second == paths)
            return;
        if (paths.empty())
            m_modAnims.erase(it);
        else if (it != m_modAnims.end() || m_modAnims.size() < 8)
            m_modAnims[player] = paths;
    }

    void Coop::SetMyLooks(const net::PlayerLooks& looks)
    {
        if (m_hasMyLooks && looks == m_myLooks)
            return;
        m_myLooks = looks;
        m_hasMyLooks = true;
        m_myLooksSent = false;
    }

    void Coop::HostReceived(PeerId peer, const Bytes& data)
    {
        auto it = m_players.find(peer);
        if (it == m_players.end())
            return;
        Player& player = it->second;

        Reader r(data);
        MsgType type;
        if (!r.Type(type))
            return Kick(peer, "Empty message");

        if (!player.joined && player.fixingMods)
        {
            ModFileAsk ask;
            if (type != MsgType::ModFileAsk || !Decode(r, ask))
                return Kick(peer, "Expected an ask for a mod file");
            if (ask.index >= m_mods.size() || m_mods[ask.index].kind == ModKind::Dll || !m_mods[ask.index].hashed ||
                ask.offset >= m_mods[ask.index].size)
                return Kick(peer, "Asked for a mod file that isn't sent");
            const ModFile& f = m_mods[ask.index];
            uint64_t left = f.size - ask.offset;
            uint32_t length = left < kModPieceSize ? static_cast<uint32_t>(left) : kModPieceSize;
            if (ask.offset == 0)
                LOG_INFO("Mods: sending %s to %s (%.1f MB)", f.name.c_str(), player.name.c_str(), f.size / 1048576.0);
            if (!m_modServer.Ask(peer, ask.index, m_modFolders.data + f.name, ask.offset, length))
                return Kick(peer, "Asked for too many mod file pieces at once");
            player.lastAsk = GetTickCount();
            return;
        }

        if (player.awaitingApproval)
            return;

        if (!player.joined)
        {
            Hello hello;
            if (type != MsgType::Hello || !Decode(r, hello))
                return Kick(peer, "Expected a hello");
            if (hello.version != kProtocolVersion)
                return Kick(peer, "Different mod version: everyone needs the same version (host protocol " +
                    std::to_string(kProtocolVersion) + ", yours " + std::to_string(hello.version) + ")");
            if (hello.checkOnly && (!hello.hasMods || m_mods.empty()))
                return Kick(peer, "No mods to compare");
            if (!hello.hasMods || m_mods.empty())
                LOG_WARN("%s's mods not checked (%s)", hello.name.c_str(),
                    hello.hasMods ? "ours aren't known" : "they sent none: a tool like the fake friend?");
            else
            {
                SendModList(peer);
                player.wantsFiles = hello.mods != m_modsPrint;
                if (player.wantsFiles)
                    LOG_WARN("%s's mods aren't ours: their game shows what differs, and may fetch ours",
                        hello.name.c_str());
                else if (hello.checkOnly)
                {
                    LOG_INFO("%s's mods are ours (compared from their main menu)", hello.name.c_str());
                    m_net->Send(peer, Encode(ModsMatch{m_myName, m_currentSave}));
                    m_net->Disconnect(peer);
                    RemovePlayer(peer);
                    return;
                }
            }

            player.name = hello.name;
            if (!m_autoApprove && !m_approved.count(hello.name + "@" + player.from))
            {
                player.awaitingApproval = true;
                player.askId = m_nextAskId++;
                player.askedAt = GetTickCount();
                m_joinAsks.push_back({player.askId, hello.name, player.wantsFiles});
                LOG_INFO("%s (%s) asks to join; asking the host", hello.name.c_str(),
                    player.from.rfind("steam:", 0) == 0 ? player.from.c_str() : "by IP");
                Writer w(MsgType::AskingHost);
                m_net->Send(peer, w.Take());
                return;
            }
            return Admit(peer, player);
        }

        if (type == MsgType::PlayerState)
        {
            PlayerState state;
            if (!Decode(r, state))
                return;
            state.playerId = player.id;
            m_remotes[player.id].motion.Add(state.timeMs,
                Pose{state.space, state.x, state.y, state.z, state.heading, state.flags, state.moves, state.pitch,
                    state.runSpeed},
                GetTickCount());
            SendToJoined(Encode(state), false, peer);
            return;
        }

        if (type == MsgType::PlayerGear)
        {
            PlayerGear gear;
            if (!Decode(r, gear))
                return;
            gear.playerId = player.id;
            SendToJoined(Encode(gear), true, peer);
            SetRemoteGear(player.id, std::move(gear.items));
            return;
        }

        if (type == MsgType::PlayerHealth)
        {
            PlayerHealth m;
            if (!Decode(r, m))
                return;
            m.playerId = player.id;
            SendToJoined(Encode(m), true, peer);
            RemotePlayer& remote = m_remotes[player.id];
            remote.health = m.health;
            remote.maxHealth = m.maxHealth;
            return;
        }

        if (type == MsgType::TravelMarkers)
        {
            std::vector<uint32_t> m;
            if (DecodeTravelMarkers(r, m) && m_travelMarkers.size() + m.size() <= kMaxQueuedMarkers)
                m_travelMarkers.insert(m_travelMarkers.end(), m.begin(), m.end());
            return;
        }

        if (type == MsgType::Teleport)
        {
            Teleport m;
            if (Decode(r, m) && (m.mover == player.id || m.target == player.id) && IsPlayer(m.mover) &&
                IsPlayer(m.target))
            {
                LOG_INFO("Teleporter: %s asks for %s to go to %s", player.name.c_str(), NameOf(m.mover).c_str(),
                    NameOf(m.target).c_str());
                AskTeleport(m.mover, m.target);
            }
            return;
        }

        if (type == MsgType::ReputationGain)
        {
            std::vector<ReputationGain> m;
            if (DecodeGains(r, m) && m_repGains.size() + m.size() <= kMaxQueuedQuests)
                for (const ReputationGain& g : m)
                    m_repGains.push_back({player.id, g});
            return;
        }

        if (type == MsgType::Progress)
        {
            Progress m;
            if (!Decode(r, m))
                return;
            m.playerId = player.id;
            SendToJoined(Encode(m), true, peer);
            if (m_progress.size() < kMaxQueuedItemChanges)
                m_progress.push_back(m);
            return;
        }

        if (type == MsgType::Arrived)
        {
            if (r.AtEnd())
            {
                LOG_INFO("Loading: %s has arrived", player.name.c_str());
                m_loads.Arrived(player.id);
                UpdateFreeze(GetTickCount());
            }
            return;
        }

        if (type == MsgType::SaveOrder)
        {
            SaveOrder m;
            if (!Decode(r, m) || SaveNameOf(m.name) != m.name)
                return;
            uint32_t now = GetTickCount();
            if (m_lastSaveAsk && now - m_lastSaveAsk < 1000)
                return;
            m_lastSaveAsk = now;
            LOG_INFO("Saves: %s %s \"%s\"", player.name.c_str(), m.load ? "asks to load" : "saved", m.name.c_str());
            if (m_saveAsks.size() < kMaxQueuedItemChanges)
                m_saveAsks.push_back(SaveAsk{player.id, m.load, std::move(m.name)});
            return;
        }

        if (type == MsgType::WaitAsk)
        {
            WaitAsk m;
            if (!Decode(r, m))
                return;
            if (m.hours == 0)
            {
                if (m_wait.IsOpen() && m_wait.Requester() == player.id)
                {
                    m_wait.Cancel(player.id);
                    LOG_INFO("Wait: %s called it off", player.name.c_str());
                    SendWaitResult(WaitResult{player.id, false, 0, false, kNobody});
                }
                return;
            }
            StartWaitVote(player.id, m.hours, m.sleep, peer);
            return;
        }

        if (type == MsgType::WaitAnswer)
        {
            WaitAnswer m;
            if (!Decode(r, m))
                return;
            if (m_wait.IsOpen() && m_wait.Asked(player.id))
                LOG_INFO("Wait: %s says %s", player.name.c_str(), m.yes ? "yes" : "no");
            m_wait.Answer(player.id, m.yes);
            CheckWaitVote(GetTickCount());
            return;
        }

        if (type == MsgType::TalkState)
        {
            TalkState m;
            if (!Decode(r, m))
                return;
            bool wasBusy = m_talk.Busy();
            uint8_t wasTalker = m_talk.Talker();
            if (!m_talk.Report(player.id, m.talking, m.npc))
            {
                LOG_INFO("Dialogue: %s started a conversation while %s is talking: refused", player.name.c_str(),
                    NameOf(m_talk.Talker()).c_str());
                return SendTalk(peer);
            }
            LOG_INFO("Dialogue: %s %s (%08X)", player.name.c_str(), m.talking ? "is talking" : "stopped talking",
                m.npc);
            if (m_talk.Busy() != wasBusy || m_talk.Talker() != wasTalker || m.talking)
                SendTalk(0);
            return;
        }

        if (type == MsgType::Subtitle)
        {
            Subtitle m;
            if (!Decode(r, m))
                return;
            if (!m_talk.Busy() || m_talk.Talker() != player.id)
                return;
            m.playerId = player.id;
            SendToJoined(Encode(m), true, peer);
            if (m_subtitles.size() < kMaxQueuedItemChanges)
                m_subtitles.push_back(std::move(m));
            return;
        }

        if (type == MsgType::DialogueLine)
        {
            DialogueLine m;
            if (!Decode(r, m))
                return;
            if (!m_talk.Busy() || m_talk.Talker() != player.id)
                return LOG_WARN("Dialogue: a line from %s, who isn't the one talking; ignored", player.name.c_str());
            if (m_dialogueLines.size() >= kMaxQueuedItemChanges)
                return LOG_WARN("Dialogue: too many lines from %s at once; dropped one", player.name.c_str());
            m_dialogueLines.push_back({player.id, m});
            return;
        }

        if (type == MsgType::PlayerHit)
        {
            net::PlayerHit m;
            uint32_t now = GetTickCount();
            if (now - player.playerHitsSince >= 1000)
            {
                player.playerHitsSince = now;
                player.playerHits = 0;
            }
            if (!player.joined || !m_mode.friendlyFire || !Decode(r, m) || m.target == player.id ||
                ++player.playerHits > 30)
                return;
            float damage = m.damage > kMaxHitDamage ? kMaxHitDamage : m.damage;
            if (m.target == m_myId)
                m_damage += damage;
            else if (std::any_of(m_players.begin(), m_players.end(),
                [&](const auto& e) { return e.second.joined && e.second.id == m.target; }))
                DamagePlayer(m.target, damage);
            return;
        }

        if (type == MsgType::VoiceData)
        {
            net::VoiceData m;
            if (!player.joined || !Decode(r, m) || m_voice.size() >= 64 || !player.voiceLimit.Allow(GetTickCount(),
                m.data.size()))
                return;
            m.player = player.id;
            SendToJoined(Encode(m), false, peer);
            m_voice.push_back({player.id, std::move(m.data)});
            return;
        }

        if (type == MsgType::PlayerModAnims)
        {
            net::PlayerModAnims m;
            if (!player.joined || !Decode(r, m))
                return;
            m.player = player.id;
            HeardModAnims(m.player, m.paths);
            SendToJoined(Encode(m), true, peer);
            return;
        }

        if (type == MsgType::PlayerAnim)
        {
            net::PlayerAnim m;
            if (!player.joined || !Decode(r, m))
                return;
            m.player = player.id;
            HeardAnim(m.player, m.path);
            SendToJoined(Encode(m), true, peer);
            return;
        }

        if (type == MsgType::PlayerLooks)
        {
            net::PlayerLooks m;
            if (!player.joined || !Decode(r, m))
                return;
            m.player = player.id;
            auto old = m_looks.find(player.id);
            if (old != m_looks.end() && old->second == m)
                return;
            m_looks[player.id] = m;
            ++m_looksChanges;
            if (!m.name.empty())
                m_remotes[player.id].name = m.name;
            LOG_INFO("Looks: %s's character, %s (%s)", player.name.c_str(), m.name.c_str(), m.female ? "female" : "male");
            SendToJoined(Encode(m), true, peer);
            return;
        }

        if (type == MsgType::ChatLine)
        {
            net::ChatLine m;
            if (!player.joined || !Decode(r, m))
                return;
            std::string line = CleanChat(m.text);
            if (line.empty() || m_chat.size() >= 32)
                return;
            if (!player.chatLimit.Allow(GetTickCount()))
                return LOG_WARN("Chat: %s says too much at once; a line dropped", player.name.c_str());
            SendToJoined(Encode(net::ChatLine{player.id, line, m.style}), true, peer);
            m_chat.push_back({player.id, NameOf(player.id), line, m.style});
            return;
        }

        if (type == MsgType::KilledByScript)
        {
            if (r.AtEnd() && !m_gameOverSent)
                EndGame(GameOverReason::Killed, player.id);
            return;
        }

        if (type == MsgType::CompanionOrder)
        {
            net::CompanionOrder m;
            if (!Decode(r, m) || m_companionOrders.size() >= 16)
                return;
            if (std::none_of(m_myCompanions.begin(), m_myCompanions.end(),
                [&](const net::CompanionState& c) { return c.ref == m.ref; }))
                return LOG_INFO("Companions: %s gave %08X an order, but it isn't one of ours", player.name.c_str(), m.ref);
            m_companionOrders.push_back({player.id, m});
            return;
        }

        if (type == MsgType::DesyncReport)
        {
            net::DesyncReport d;
            if (!Decode(r, d) || (d.npcs == player.desyncs.npcs && d.items == player.desyncs.items &&
                d.doors == player.desyncs.doors))
                return;
            bool more = uint32_t(d.npcs) + d.items + d.doors >
                uint32_t(player.desyncs.npcs) + player.desyncs.items + player.desyncs.doors;
            player.desyncs = d;
            if (d.npcs + d.items + d.doors == 0)
                return LOG_INFO("Desync: %s's game agrees with ours again", player.name.c_str());
            LOG_WARN("Desync: %s's game disagrees with ours on %u NPCs, %u items, %u doors (their log says what)",
                player.name.c_str(), d.npcs, d.items, d.doors);
            if (more && m_desyncNotices.size() < 8)
            {
                uint32_t n = uint32_t(d.npcs) + d.items + d.doors;
                m_desyncNotices.push_back("Co-op: " + player.name + "'s game disagrees with ours on " +
                    std::to_string(n) + (n == 1 ? " thing" : " things") + " (see the logs)");
            }
            return;
        }

        if (type == MsgType::ItemMoves)
        {
            ItemMoves m;
            if (!Decode(r, m))
                return;
            for (const ItemMove& move : m.moves)
            {
                if (!WithinLimit(player.itemMovesSince, player.itemMoves, kMaxItemMovesPerSecond))
                    return;
                auto ours = m_sentItems.find(move.ref);
                if (ours == m_sentItems.end() || !ours->second.present)
                    continue;
                if (ours->second.held)
                {
                    LOG_DEBUG("Items: %s moved %08X, but our player carries it (ours wins)", player.name.c_str(),
                        move.ref);
                    continue;
                }
                float dx = move.x - ours->second.x, dy = move.y - ours->second.y, dz = move.z - ours->second.z;
                if (dx * dx + dy * dy + dz * dz > kMaxItemMoveReach * kMaxItemMoveReach ||
                    m_itemMoves.size() >= kMaxQueuedItemMoves)
                    continue;
                auto same = std::find_if(m_itemMoves.begin(), m_itemMoves.end(),
                    [&](const ItemMoveFrom& f) { return f.move.ref == move.ref; });
                if (same != m_itemMoves.end())
                    *same = {player.id, move};
                else
                    m_itemMoves.push_back({player.id, move});
            }
            return;
        }

        if (type == MsgType::ItemTake || type == MsgType::ItemDrop)
        {
            if (m_itemTakes.size() + m_itemDrops.size() >= kMaxQueuedItemChanges)
                return LOG_WARN("Too many pick-ups and drops from %s at once, dropped one", player.name.c_str());
            if (type == MsgType::ItemTake)
            {
                ItemTake m;
                if (Decode(r, m))
                    m_itemTakes.push_back({player.id, m});
            }
            else
            {
                ItemDrop m;
                if (Decode(r, m))
                    m_itemDrops.push_back({player.id, m});
            }
            return;
        }

        if (type == MsgType::ContainerView)
        {
            uint32_t ref;
            if (!DecodeContainerView(r, ref))
                return;
            HostView& view = m_views[player.id];
            if (view.ref != ref)
            {
                LOG_DEBUG("%s %s container %08X", player.name.c_str(), ref ? "opened" : "closed", ref ? ref : view.ref);
                view.ref = ref;
                view.sent = false;
            }
            return;
        }

        if (type == MsgType::ContainerChange)
        {
            ContainerChange m;
            if (!Decode(r, m))
                return;
            if (m_itemChanges.size() >= kMaxQueuedItemChanges)
            {
                LOG_WARN("Too many container changes from %s at once; undoing one", player.name.c_str());
                ContainerChangeDone(player.id, m.seq, m.change);
                return;
            }
            m_itemChanges.push_back({player.id, m});
            return;
        }

        if (type == MsgType::DoorUse)
        {
            DoorState door;
            if (!DecodeDoorUse(r, door))
                return;
            if (m_doorUses.size() >= kMaxQueuedDoorUses)
                return LOG_WARN("Too many door uses from %s at once, dropped one", player.name.c_str());
            LOG_DEBUG("%s used door %08X: %s, %s", player.name.c_str(), door.ref,
                door.flags & kDoorOpen ? "open" : "closed", door.flags & kDoorLocked ? "locked" : "unlocked");
            m_doorUses.push_back(door);
            return;
        }

        if (type == MsgType::Hit)
        {
            Hit hit;
            if (!Decode(r, hit) || hit.targetRef == 0 || !(hit.damage > 0) || hit.damage > kMaxHitDamage)
                return;
            if (m_hits.size() >= kMaxQueuedHits)
                return LOG_WARN("Too many hits from %s at once, dropped one", player.name.c_str());
            m_hits.push_back({player.id, hit.targetRef, hit.damage});
            return;
        }

        if (type == MsgType::NpcPile)
        {
            net::NpcPile m;
            if (player.joined && Decode(r, m) && m_piles.size() < 32)
                m_piles.push_back(m);
            return;
        }

        if (type == MsgType::MineChange)
        {
            net::MineChange m;
            if (!player.joined || !Decode(r, m) || m_mines.size() >= 64)
                return;
            m_mines.push_back({player.id, m});
            SendToJoined(Encode(m), true, peer);
            return;
        }

        if (type == MsgType::Revive)
        {
            net::Revive m;
            if (Decode(r, m))
                HostRevive(player.id, m.playerId);
            return;
        }

        if (type == MsgType::Drag)
        {
            net::Drag m;
            if (Decode(r, m) && m.dragger == player.id)
                HostDrag(m.dragger, m.target, m.on);
            return;
        }

        if (type == MsgType::Attack)
        {
            net::Attack m;
            if (!Decode(r, m))
                return;
            m.playerId = player.id;
            SendToJoined(Encode(m), true, peer);
            QueueAttack(player.id, m.shots, m.release);
            return;
        }

        if (type == MsgType::ExplosionAt)
        {
            net::ExplosionAt e;
            if (!Decode(r, e) || !WithinLimit(player.explosionsSince, player.explosions, kMaxExplosionsPerSecond))
                return;
            SendToJoined(Encode(e), true, peer);
            if (m_explosions.size() < kMaxQueuedExplosions)
                m_explosions.push_back(e);
            return;
        }

        if (type == MsgType::Regroup)
        {
            if (r.AtEnd() && m_hasPose)
            {
                LOG_INFO("%s loaded a save; bringing them to us", player.name.c_str());
                SendTravel(player.id, kHostPlayerId, m_myPose);
            }
            return;
        }

        if (type == MsgType::Travel)
        {
            net::Travel m;
            if (Decode(r, m))
                GroupTravels(player.id, Pose{m.space, m.x, m.y, m.z, m.heading}, GetTickCount());
            return;
        }

        LOG_WARN("Unexpected message %u from %s", static_cast<unsigned>(type), player.name.c_str());
    }

    void Coop::FriendReceived(const Bytes& data)
    {
        Reader r(data);
        MsgType type;
        if (!r.Type(type))
            return;

        if (type == MsgType::Welcome)
        {
            Welcome welcome;
            if (m_friendState != FriendState::WaitingForWelcome || !Decode(r, welcome))
            {
                LOG_WARN("Bad welcome from host");
                return;
            }
            m_myId = welcome.playerId;
            m_friendState = FriendState::Joined;
            m_hostXp = welcome.hostXp;
            m_hostXpWaiting = true;
            m_myLooksSent = false;
            m_myAnimSent = m_myAnim.empty();
            m_myModAnimsSent = m_myModAnims.empty();
            m_questMods = welcome.questMods;
            m_mode = welcome.mode;
            LOG_INFO("The host's settings: %s", Describe(m_mode).c_str());
            for (const std::string& mod : m_questMods)
                LOG_INFO("Quests: the host's game runs %s's quests too", mod.c_str());
            m_gearToSend = true;
            LOG_INFO("Joined %s's game as player %u", welcome.hostName.c_str(), m_myId);
        }
        else if (type == MsgType::ModFiles)
        {
            ModFiles m;
            if (m_hostModsDone || !Decode(r, m) || m_hostMods.size() + m.files.size() > kMaxModFiles)
                return LOG_WARN("Bad mod list from the host");
            m_hostMods.insert(m_hostMods.end(), m.files.begin(), m.files.end());
            if (m.last)
            {
                m_hostModsDone = true;
                CheckHostMods();
            }
        }
        else if (type == MsgType::ModsDiffer)
        {
            if (m_friendState != FriendState::WaitingForWelcome || !m_hostModsDone || !r.AtEnd())
                return LOG_WARN("Bad 'mods differ' from the host");
            m_friendState = FriendState::FixingMods;
            OfferModFix();
        }
        else if (type == MsgType::AskingHost)
        {
            if (r.AtEnd() && m_friendState == FriendState::WaitingForWelcome)
            {
                LOG_INFO("The host is being asked to let us in");
                m_askingHost = true;
            }
        }
        else if (type == MsgType::ModsMatch)
        {
            ModsMatch m;
            if (!m_checkOnly || m_friendState != FriendState::WaitingForWelcome || !m_hostModsDone || !Decode(r, m))
                return LOG_WARN("Bad 'mods match' from the host");
            if (!m.save.empty() && SaveNameOf(m.save) != m.save)
                m.save.clear();
            LOG_INFO("Mods: ours are %s's (playing \"%s\"); loading a save joins", m.hostName.c_str(), m.save.c_str());
            m_matchedHost = m;
            m_modsMatched = true;
            Stop();
        }
        else if (type == MsgType::ModPiece)
        {
            ModPiece piece;
            if (!m_downloading)
                return LOG_WARN("A mod file piece we didn't ask for");
            if (!Decode(r, piece))
                m_download.Fail("a bad piece from the host");
            else
                m_download.Got(piece);
        }
        else if (type == MsgType::Reject)
        {
            Reject reject;
            if (!Decode(r, reject))
                reject.reason = "(no reason given)";
            m_friendState = FriendState::Rejected;
            LOG_WARN("Host refused to let us join: %s", reject.reason.c_str());
            m_rejectReason = m_modsDiffer ? "" : reject.reason;
        }
        else if (type == MsgType::PlayerJoined)
        {
            PlayerJoined m;
            if (!Decode(r, m) || m.playerId == m_myId)
                return;
            if (!m_remotes.count(m.playerId) && m_remotes.size() >= kMaxFriends)
                return LOG_WARN("The host announced more players than a game can have; ignoring %u", m.playerId);
            m_remotes[m.playerId].name = m.name;
            LOG_INFO("%s is in the game (player %u)", m.name.c_str(), m.playerId);
        }
        else if (type == MsgType::PlayerLeft)
        {
            PlayerLeft m;
            if (!Decode(r, m))
                return;
            auto it = m_remotes.find(m.playerId);
            if (it == m_remotes.end())
                return;
            LOG_INFO("%s left", it->second.name.c_str());
            m_looks.erase(it->first);
            HeardAnim(it->first, "");
            HeardModAnims(it->first, {});
            m_remotes.erase(it);
        }
        else if (type == MsgType::PlayerState)
        {
            PlayerState m;
            if (!Decode(r, m))
                return;
            auto it = m_remotes.find(m.playerId);
            if (it != m_remotes.end())
                it->second.motion.Add(m.timeMs, Pose{m.space, m.x, m.y, m.z, m.heading, m.flags, m.moves, m.pitch,
                    m.runSpeed},
                    GetTickCount());
        }
        else if (type == MsgType::NpcStates)
        {
            NpcStates m;
            if (Decode(r, m))
                HeardNpcs(m);
        }
        else if (type == MsgType::Progress)
        {
            Progress m;
            if (Decode(r, m) && m.playerId != m_myId && m_progress.size() < kMaxQueuedItemChanges)
                m_progress.push_back(m);
        }
        else if (type == MsgType::Subtitle)
        {
            Subtitle m;
            if (Decode(r, m) && m.playerId != m_myId && m_subtitles.size() < kMaxQueuedItemChanges)
                m_subtitles.push_back(std::move(m));
        }
        else if (type == MsgType::WaitVote)
        {
            net::WaitVote m;
            if (Decode(r, m) && m.requester != m_myId && m_waitEvents.size() < kMaxQueuedItemChanges)
                m_waitEvents.push_back(WaitEvent{WaitEvent::Kind::Asked, m.requester, m.hours, m.sleep, kNobody});
        }
        else if (type == MsgType::WaitResult)
        {
            WaitResult m;
            if (Decode(r, m) && m_waitEvents.size() < kMaxQueuedItemChanges)
                m_waitEvents.push_back(WaitEvent{m.passed ? WaitEvent::Kind::Start : WaitEvent::Kind::Refused,
                    m.requester, m.hours, m.sleep, m.refusedBy});
        }
        else if (type == MsgType::SaveOrder)
        {
            SaveOrder m;
            if (!Decode(r, m) || m_friendState != FriendState::Joined)
                return;
            if (SaveNameOf(m.name) != m.name)
            {
                LOG_WARN("Saves: the host's save name isn't one we use; ignored");
                return;
            }
            if (m_saveOrders.size() < kMaxQueuedItemChanges)
                m_saveOrders.push_back(std::move(m));
        }
        else if (type == MsgType::LoadRefused)
            m_loadRefused = true;
        else if (type == MsgType::Freeze)
        {
            Freeze m;
            if (Decode(r, m))
            {
                m_frozen = m.frozen;
                m_frozenFor = m.waitingFor;
            }
        }
        else if (type == MsgType::GameTime)
        {
            GameTime m;
            if (Decode(r, m))
            {
                m_hostTime = m;
                m_hostTimeNew = true;
            }
        }
        else if (type == MsgType::TalkState)
        {
            TalkState m;
            if (!Decode(r, m))
                return;
            m_talk.Clear();
            if (m.talking)
                m_talk.Report(m.playerId, true, m.npc);
        }
        else if (type == MsgType::Teleport)
        {
            Teleport m;
            if (Decode(r, m) && m.mover == m_myId && IsPlayer(m.target) && m_teleports.size() < 8)
                m_teleports.push_back(m.target);
        }
        else if (type == MsgType::TravelMarkers)
        {
            std::vector<uint32_t> m;
            if (DecodeTravelMarkers(r, m) && m_travelMarkers.size() + m.size() <= kMaxQueuedMarkers)
                m_travelMarkers.insert(m_travelMarkers.end(), m.begin(), m.end());
        }
        else if (type == MsgType::Reputations)
        {
            std::vector<Reputation> m;
            if (Decode(r, m) && m_reputations.size() + m.size() <= kMaxQueuedQuests)
                m_reputations.insert(m_reputations.end(), m.begin(), m.end());
        }
        else if (type == MsgType::RefEnables)
        {
            std::vector<RefEnable> m;
            if (!Decode(r, m))
                return;
            if (m_refEnables.size() + m.size() > kMaxQueuedQuests)
                return LOG_WARN("Too many switched-on/off things waiting; dropped some (the host sends them again)");
            m_refEnables.insert(m_refEnables.end(), m.begin(), m.end());
        }
        else if (type == MsgType::QuestStates)
        {
            QuestStates m;
            if (!Decode(r, m))
                return;
            if (m_questStates.size() + m.quests.size() > kMaxQueuedQuests)
                return LOG_WARN("Too many quest updates waiting; dropped some (the host sends them again)");
            for (QuestState& q : m.quests)
                m_questStates.push_back(std::move(q));
        }
        else if (type == MsgType::ItemStates)
        {
            ItemStates m;
            if (Decode(r, m))
                for (const ItemState& it : m.items)
                    m_friendItems.Heard(it, GetTickCount());
        }
        else if (type == MsgType::ContainerContents)
        {
            ContainerContents m;
            if (Decode(r, m))
                m_friendContainer.Heard(m);
        }
        else if (type == MsgType::ItemRefund)
        {
            ItemCount m;
            if (DecodeItemRefund(r, m) && m_refunds.size() < kMaxQueuedItemChanges)
                m_refunds.push_back(m);
        }
        else if (type == MsgType::DoorStates)
        {
            DoorStates m;
            if (!Decode(r, m))
                return;
            for (const DoorState& d : m.doors)
                m_friendDoors.Heard(d);
        }
        else if (type == MsgType::NpcGear)
        {
            NpcGear m;
            if (!Decode(r, m))
                return;
            auto it = m_npcs.find(m.ref);
            if (it != m_npcs.end() && (it->second.gear != m.items || it->second.gearChanges == 0))
            {
                it->second.gear = std::move(m.items);
                ++it->second.gearChanges;
            }
        }
        else if (type == MsgType::NpcAttacks)
        {
            NpcAttacks m;
            if (!Decode(r, m))
                return;
            for (const NpcAttack& a : m.attacks)
                if (m_npcs.count(a.ref) && m_npcAttacks.size() < kMaxNpcs)
                    m_npcAttacks.push_back(a);
        }
        else if (type == MsgType::PlayerGear)
        {
            PlayerGear m;
            if (!Decode(r, m) || !m_remotes.count(m.playerId))
                return;
            SetRemoteGear(m.playerId, std::move(m.items));
        }
        else if (type == MsgType::PlayerHealth)
        {
            PlayerHealth m;
            if (!Decode(r, m))
                return;
            auto it = m_remotes.find(m.playerId);
            if (it == m_remotes.end())
                return;
            it->second.health = m.health;
            it->second.maxHealth = m.maxHealth;
        }
        else if (type == MsgType::Travel)
        {
            net::Travel m;
            if (m_friendState == FriendState::Joined && Decode(r, m))
                PullMe(m.playerId, Pose{m.space, m.x, m.y, m.z, m.heading});
        }
        else if (type == MsgType::Fighting)
        {
            net::Fighting m;
            if (Decode(r, m))
                m_hostSaysFighting = m.fighting;
        }
        else if (type == MsgType::Damage)
        {
            Damage m;
            if (!Decode(r, m) || m.playerId != m_myId || !(m.amount > 0) || m.amount > kMaxHitDamage)
                return;
            m_damage += m.amount;
            if (m_damage > kMaxHitDamage)
                m_damage = kMaxHitDamage;
        }
        else if (type == MsgType::Attack)
        {
            net::Attack m;
            if (Decode(r, m) && m.playerId != m_myId && m_remotes.count(m.playerId))
                QueueAttack(m.playerId, m.shots, m.release);
        }
        else if (type == MsgType::VoiceData)
        {
            net::VoiceData m;
            if (Decode(r, m) && m.player != m_myId && m_voice.size() < 64)
                m_voice.push_back({m.player, std::move(m.data)});
        }
        else if (type == MsgType::PlayerAnim)
        {
            net::PlayerAnim m;
            if (Decode(r, m) && m.player != m_myId)
                HeardAnim(m.player, m.path);
        }
        else if (type == MsgType::PlayerModAnims)
        {
            net::PlayerModAnims m;
            if (Decode(r, m) && m.player != m_myId)
                HeardModAnims(m.player, m.paths);
        }
        else if (type == MsgType::PlayerLooks)
        {
            net::PlayerLooks m;
            if (Decode(r, m) && m.player != m_myId && (m_looks.count(m.player) || m_looks.size() < 8))
            {
                m_looks[m.player] = m;
                ++m_looksChanges;
                LOG_INFO("Looks: %s's character, %s (%s)", NameOf(m.player).c_str(), m.name.c_str(),
                    m.female ? "female" : "male");
                if (!m.name.empty() && m_remotes.count(m.player))
                    m_remotes[m.player].name = m.name;
            }
        }
        else if (type == MsgType::ChatLine)
        {
            net::ChatLine m;
            if (Decode(r, m) && m.player != m_myId && m_chat.size() < 32)
            {
                std::string line = CleanChat(m.text);
                if (!line.empty())
                    m_chat.push_back({m.player, NameOf(m.player), line, m.style});
            }
        }
        else if (type == MsgType::MineChange)
        {
            net::MineChange m;
            if (Decode(r, m) && m_mines.size() < 64)
                m_mines.push_back({kHostPlayerId, m});
        }
        else if (type == MsgType::RadioLine)
        {
            net::RadioLine line;
            if (Decode(r, line) && (m_radioLines.size() < 64 || m_radioLines.count(line.station)))
                m_radioLines[line.station] = line;
        }
        else if (type == MsgType::PlayerCommand)
        {
            net::PlayerCommand pc;
            if (Decode(r, pc) && m_playerCommands.size() < 256)
                m_playerCommands.push_back(pc);
        }
        else if (type == MsgType::CompanionStates)
        {
            net::CompanionStates m;
            if (Decode(r, m))
            {
                m_companions.clear();
                for (const net::CompanionState& cs : m.companions)
                    m_companions[cs.ref] = cs.leader;
            }
        }
        else if (type == MsgType::ExplosionAt)
        {
            net::ExplosionAt e;
            if (Decode(r, e) && WithinLimit(m_hostExplosionsSince, m_hostExplosions, kMaxHostExplosionsPerSecond) &&
                m_explosions.size() < kMaxQueuedExplosions)
                m_explosions.push_back(e);
        }
        else if (type == MsgType::Revive)
        {
            net::Revive m;
            if (!Decode(r, m) || m.playerId != m_myId)
                return;
            m_revived = true;
            LOG_INFO("We were revived");
        }
        else if (type == MsgType::Drag)
        {
            net::Drag m;
            if (!Decode(r, m) || !IsPlayer(m.dragger) || !IsPlayer(m.target))
                return;
            auto it = m_drags.find(m.target);
            if (m.on)
                m_drags[m.target] = m.dragger;
            else if (it != m_drags.end() && it->second == m.dragger)
                m_drags.erase(it);
            LOG_INFO("%s %s %s", NameOf(m.dragger).c_str(), m.on ? "drags" : "lets go of", NameOf(m.target).c_str());
        }
        else if (type == MsgType::Mode)
        {
            net::ModeChange m;
            if (!InGame() || !Decode(r, m))
                return LOG_WARN("Bad mode change from the host");
            m_mode = m.mode;
            LOG_INFO("The host's mode is now %s", Describe(m_mode).c_str());
        }
        else if (type == MsgType::GameOver)
        {
            GameOver m;
            if (!Decode(r, m))
                return;
            m_gameOver = true;
            m_gameOverInfo = m;
            LOG_INFO("Game over (%s)", m.reason == GameOverReason::BledOut ? "someone bled out" : "everyone is down");
        }
        else
            LOG_WARN("Unexpected message %u from host", static_cast<unsigned>(type));
    }

    void Coop::SetMyGear(std::vector<uint32_t> items)
    {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        if (items.size() > kMaxGearItems)
            items.resize(kMaxGearItems);
        if (items == m_myGear)
            return;
        m_myGear = std::move(items);
        m_gearToSend = true;
    }

    void Coop::ReportMine(uint32_t ref, uint8_t state)
    {
        net::MineChange m{ref, state};
        if (!InGame() || !net::IsSharedRef(ref) || net::IsMadeWhilePlaying(ref) || state < net::kMineOff ||
            state > net::kMineBlown)
            return;
        if (m_role == Role::Host)
            SendToJoined(Encode(m), true);
        else
            m_net->Send(1, Encode(m), true);
    }

    void Coop::ReportPile(uint32_t hostRef, uint8_t kind) const
    {
        if (m_role == Role::Friend && InGame() && kind >= net::kPileGoo && kind <= net::kPileLast)
            m_net->Send(1, Encode(net::NpcPile{hostRef, kind}), true);
    }

    void Coop::ReportHit(uint32_t targetRef, float damage)
    {
        if (m_role == Role::Friend && InGame())
            m_net->Send(1, Encode(Hit{m_myId, targetRef, damage}), true);
    }

    void Coop::Attack(uint8_t shots, bool release)
    {
        if (!InGame() || (shots == 0 && !release))
            return;
        Bytes msg = Encode(net::Attack{m_myId, release ? uint8_t(0) : shots < net::kMaxShots ? shots : net::kMaxShots,
            release});
        if (m_role == Role::Host)
            SendToJoined(msg, true);
        else
            m_net->Send(1, msg, true);
    }

    bool Coop::WithinLimit(uint32_t& since, uint32_t& count, uint32_t most)
    {
        uint32_t now = GetTickCount();
        if (now - since >= 1000)
        {
            since = now;
            count = 0;
        }
        return ++count <= most;
    }

    void Coop::SawDesync(const Disagreement& d)
    {
        if (m_role == Role::Friend && InGame())
            m_desyncs.Saw(d, GetTickCount());
    }

    void Coop::SweepDesyncs(uint32_t now)
    {
        if (!InGame() || !IsDue(now, m_nextDesyncSweep))
            return;
        m_nextDesyncSweep = now + 1000;
        for (const DesyncEvent& e : m_desyncs.Sweep(now))
        {
            if (e.started)
            {
                LOG_WARN("Desync: %s %08X: %s (for %u s now)", DesyncKindName(e.what.kind), e.what.ref,
                    e.what.detail.c_str(), e.lastedMs / 1000);
                if (m_desyncNotices.size() < 8)
                    m_desyncNotices.push_back(std::string("Co-op: out of step with the host: ") +
                        (e.what.kind == DesyncKind::Door ? "a " : "an ") + DesyncKindName(e.what.kind) + " " + e.what.detail + " (see NVCoop.log)");
            }
            else
                LOG_INFO("Desync over: %s %08X: %s (lasted %u s)", DesyncKindName(e.what.kind), e.what.ref,
                    e.what.detail.c_str(), e.lastedMs / 1000);
        }
        DesyncCounts now_ = m_desyncs.Lasting();
        if (now_ != m_desyncsSent)
        {
            m_desyncsSent = now_;
            auto cap = [](uint16_t n) { return n < net::kMaxDesyncCount ? n : net::kMaxDesyncCount; };
            m_net->Send(1, Encode(net::DesyncReport{cap(now_.npcs), cap(now_.items), cap(now_.doors)}), true);
        }
    }

    void Coop::SetCompanions(const std::vector<net::CompanionState>& companions)
    {
        if (m_role != Role::Host || companions == m_myCompanions)
            return;
        m_myCompanions = companions;
        m_companionsChanged = true;
    }

    void Coop::SendCompanions(uint32_t now)
    {
        if (m_players.empty() || (!m_companionsChanged && now - m_lastCompanionsSent < 5000))
            return;
        m_companionsChanged = false;
        m_lastCompanionsSent = now;
        SendToJoined(Encode(net::CompanionStates{m_myCompanions}), true);
    }

    bool Coop::CompanionLeader(uint32_t hostRef, uint8_t& leader) const
    {
        auto it = m_companions.find(hostRef);
        if (it == m_companions.end())
            return false;
        leader = it->second;
        return true;
    }

    void Coop::OrderCompanion(uint32_t hostRef, net::CompanionOrderKind order)
    {
        if (m_role != Role::Friend || !InGame() || !m_companions.count(hostRef))
            return;
        LOG_INFO("Companions: telling %08X to %s", hostRef, order == net::CompanionOrderKind::FollowMe ? "follow me" : "wait");
        m_net->Send(1, Encode(net::CompanionOrder{hostRef, order}), true);
    }

    void Coop::ShareRadioLine(net::RadioLine line, uint32_t startedAt)
    {
        if (m_role != Role::Host || !InGame())
            return;
        net::RadioLine& now = m_radioLines[line.station];
        line.seq = now.seq + 1;
        line.elapsed = 0;
        now = std::move(line);
        m_radioStartedAt[now.station] = startedAt;
        SendToJoined(Encode(now), true);
    }

    void Coop::KilledByScript()
    {
        if (!InGame())
            return;
        LOG_INFO("A script killed our player");
        if (m_role == Role::Host)
        {
            if (!m_gameOverSent)
                EndGame(GameOverReason::Killed, m_myId);
        }
        else
            m_net->Send(1, net::Bytes{static_cast<uint8_t>(MsgType::KilledByScript)}, true);
    }

    void Coop::SendChat(const std::string& text, VoiceStyle style)
    {
        std::string line = CleanChat(text);
        if (!InGame() || line.empty())
            return;
        if (!style.Valid())
            style = VoiceStyle{};
        Bytes data = Encode(net::ChatLine{m_myId, line, style});
        if (m_role == Role::Host)
            SendToJoined(data, true);
        else
            m_net->Send(1, data, true);
    }

    void Coop::SendVoice(const net::Bytes& data)
    {
        if (!InGame() || data.empty() || data.size() > net::kMaxVoiceBytes)
            return;
        Bytes message = Encode(net::VoiceData{m_myId, data});
        if (m_role == Role::Host)
            SendToJoined(message, false);
        else
            m_net->Send(1, message, false);
    }

    void Coop::SharePlayerCommand(const net::PlayerCommand& c)
    {
        if (m_role != Role::Host || !InGame())
            return;
        Bytes data = Encode(c);
        ++m_playerCommandsSent;
        m_playerCommandBytesSent += static_cast<uint32_t>(data.size());
        SendToJoined(data, true);
    }

    void Coop::ShareExplosion(const net::ExplosionAt& e)
    {
        if (!InGame())
            return;
        if (m_role == Role::Host)
            SendToJoined(Encode(e), true);
        else
            m_net->Send(1, Encode(e), true);
    }

    void Coop::QueueAttack(uint8_t playerId, uint8_t shots, bool release)
    {
        if (m_attacks.size() >= kMaxQueuedAttacks)
            return LOG_WARN("Too many attacks at once, dropped one from player %u", playerId);
        m_attacks.push_back({playerId, shots, release});
    }

    void Coop::SetMyHealth(float health, float maxHealth)
    {
        if (!(maxHealth > 0) || maxHealth > kMaxHealth || !(health >= -kMaxHealth && health <= kMaxHealth))
            return;
        if (std::fabs(health - m_myHealth) < 0.1f && maxHealth == m_myMaxHealth)
            return;
        m_myHealth = health;
        m_myMaxHealth = maxHealth;
        m_healthToSend = true;
    }

    void Coop::SendMyHealth(uint32_t now)
    {
        if (!m_healthToSend || !InGame() || now - m_lastHealthSent < kHealthIntervalMs)
            return;
        m_healthToSend = false;
        m_lastHealthSent = now;
        Bytes msg = Encode(PlayerHealth{m_myId, m_myHealth, m_myMaxHealth});
        if (m_role == Role::Host)
            SendToJoined(msg, true);
        else
            m_net->Send(1, msg, true);
    }

    void Coop::SetFighting(uint8_t playerId, bool fighting)
    {
        if (m_role != Role::Host)
            return;
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id == playerId && p.fighting != fighting)
            {
                p.fighting = fighting;
                m_net->Send(peer, Encode(net::Fighting{fighting}), true);
            }
    }

    void Coop::SetSessionMode(const SaveMode& mode)
    {
        if (m_role == Role::Friend || m_mode == mode)
            return;
        bool changed = m_mode.mode != GameMode::Unset;
        m_mode = mode;
        if (m_role == Role::Host && changed && mode.mode != GameMode::Unset)
        {
            SendToJoined(Encode(net::ModeChange{mode}), true);
            LOG_INFO("Mode: the group now plays %s", Describe(mode).c_str());
        }
    }

    void Coop::DamagePlayer(uint8_t playerId, float amount)
    {
        if (m_role != Role::Host || !(amount > 0))
            return;
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id == playerId)
                m_net->Send(peer, Encode(Damage{playerId, amount > kMaxHitDamage ? kMaxHitDamage : amount}), true);
    }

    void Coop::ReportPlayerHit(uint8_t target, float damage)
    {
        if (!InGame() || !m_mode.friendlyFire || !(damage > 0) || target == m_myId)
            return;
        damage = damage > kMaxHitDamage ? kMaxHitDamage : damage;
        if (m_role == Role::Host)
            DamagePlayer(target, damage);
        else
            m_net->Send(1, Encode(net::PlayerHit{target, damage}), true);
    }

    void Coop::Revive(uint8_t playerId)
    {
        if (m_role == Role::Host)
            HostRevive(m_myId, playerId);
        else if (InGame())
            m_net->Send(1, Encode(net::Revive{playerId}), true);
    }

    bool Coop::TakeGameOver(GameOver& out)
    {
        if (!m_gameOver)
            return false;
        m_gameOver = false;
        out = m_gameOverInfo;
        return true;
    }

    std::string Coop::NameOf(uint8_t playerId) const
    {
        if (playerId == m_myId)
            return m_myName;
        auto it = m_remotes.find(playerId);
        return it != m_remotes.end() ? it->second.name : "player " + std::to_string(playerId);
    }

    bool Coop::PoseOf(uint8_t playerId, Pose& out) const
    {
        if (playerId == m_myId)
        {
            out = m_myPose;
            return m_hasPose;
        }
        auto it = m_remotes.find(playerId);
        return it != m_remotes.end() && it->second.motion.Sample(GetTickCount(), out);
    }

    void Coop::HostRevive(uint8_t reviver, uint8_t target)
    {
        auto health = [&](uint8_t id, float& h, float& max) {
            if (id == m_myId)
            {
                h = m_myHealth, max = m_myMaxHealth;
                return true;
            }
            auto it = m_remotes.find(id);
            if (it == m_remotes.end())
                return false;
            h = it->second.health, max = it->second.maxHealth;
            return true;
        };
        float th, tmax, rh, rmax;
        Pose tp, rp;
        bool ok = reviver != target && health(target, th, tmax) && health(reviver, rh, rmax) &&
            IsDowned(th, tmax) && !IsDowned(rh, rmax) && PoseOf(target, tp) && PoseOf(reviver, rp) &&
            tp.space == rp.space;
        float dx = tp.x - rp.x, dy = tp.y - rp.y, dz = tp.z - rp.z;
        if (!ok || dx * dx + dy * dy + dz * dz > kReviveDistance * kReviveDistance)
        {
            LOG_INFO("Ignored a revive of player %u by %s (target down %d, reviver up %d, same place %d, %.0f apart)",
                target, NameOf(reviver).c_str(), IsDowned(th, tmax), !IsDowned(rh, rmax), tp.space == rp.space,
                std::sqrt(dx * dx + dy * dy + dz * dz));
            return;
        }
        LOG_INFO("%s revived %s", NameOf(reviver).c_str(), NameOf(target).c_str());
        m_downedSince.erase(target);
        if (target == m_myId)
        {
            m_revived = true;
            m_myHealth = m_myMaxHealth * kReviveHealth;
            m_healthToSend = true;
            return;
        }
        m_remotes[target].health = tmax * kReviveHealth;
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id == target)
                m_net->Send(peer, Encode(net::Revive{target}), true);
    }

    void Coop::Drag(uint8_t target, bool on)
    {
        if (m_role == Role::Host)
            HostDrag(m_myId, target, on);
        else if (InGame())
            m_net->Send(1, Encode(net::Drag{m_myId, target, on}), true);
    }

    int Coop::DraggedBy(uint8_t target) const
    {
        auto it = m_drags.find(target);
        return it != m_drags.end() ? it->second : -1;
    }

    int Coop::Dragging(uint8_t dragger) const
    {
        for (const auto& [target, by] : m_drags)
            if (by == dragger)
                return target;
        return -1;
    }

    bool Coop::HealthOf(uint8_t playerId, float& health, float& maxHealth) const
    {
        if (playerId == m_myId)
        {
            health = m_myHealth, maxHealth = m_myMaxHealth;
            return true;
        }
        auto it = m_remotes.find(playerId);
        if (it == m_remotes.end())
            return false;
        health = it->second.health, maxHealth = it->second.maxHealth;
        return true;
    }

    void Coop::HostDrag(uint8_t dragger, uint8_t target, bool on)
    {
        auto it = m_drags.find(target);
        if (!on)
        {
            if (it != m_drags.end() && it->second == dragger)
                EndDrag(target);
            return;
        }
        float th = 0, tmax = 0, dh = 0, dmax = 0;
        Pose tp, dp;
        bool ok = dragger != target && it == m_drags.end() && Dragging(dragger) < 0 && HealthOf(target, th, tmax) &&
            HealthOf(dragger, dh, dmax) && IsDowned(th, tmax) && !IsDowned(dh, dmax) && PoseOf(target, tp) &&
            PoseOf(dragger, dp) && tp.space == dp.space;
        float dx = tp.x - dp.x, dy = tp.y - dp.y, dz = tp.z - dp.z;
        if (!ok || dx * dx + dy * dy + dz * dz > kReviveDistance * kReviveDistance)
        {
            LOG_INFO("Ignored a drag of player %u by %s (target down %d, dragger up %d, same place %d, %.0f apart, "
                "dragged already %d)", target, NameOf(dragger).c_str(), IsDowned(th, tmax), !IsDowned(dh, dmax),
                tp.space == dp.space, std::sqrt(dx * dx + dy * dy + dz * dz), it != m_drags.end());
            return;
        }
        m_drags[target] = dragger;
        m_dragSince[target] = GetTickCount();
        LOG_INFO("%s drags %s", NameOf(dragger).c_str(), NameOf(target).c_str());
        SendToJoined(Encode(net::Drag{dragger, target, true}), true);
    }

    void Coop::EndDrag(uint8_t target)
    {
        auto it = m_drags.find(target);
        if (it == m_drags.end())
            return;
        uint8_t dragger = it->second;
        m_drags.erase(it);
        auto since = m_dragSince.find(target);
        if (since != m_dragSince.end())
        {
            auto down = m_downedSince.find(target);
            if (down != m_downedSince.end())
                down->second += GetTickCount() - since->second;
            m_dragSince.erase(since);
        }
        LOG_INFO("%s lets go of %s", NameOf(dragger).c_str(), NameOf(target).c_str());
        SendToJoined(Encode(net::Drag{dragger, target, false}), true);
    }

    void Coop::CheckDowned(uint32_t now)
    {
        std::map<uint8_t, uint32_t> since;
        size_t players = 0;
        auto check = [&](uint8_t id, float health, float maxHealth) {
            if (!(maxHealth > 0))
                return;
            ++players;
            if (!IsDowned(health, maxHealth))
                return;
            auto it = m_downedSince.find(id);
            if (it == m_downedSince.end())
                LOG_INFO("%s is down", NameOf(id).c_str());
            since[id] = it != m_downedSince.end() ? it->second : now;
        };
        check(m_myId, m_myHealth, m_myMaxHealth);
        for (auto& [id, remote] : m_remotes)
            check(id, remote.health, remote.maxHealth);
        m_downedSince = std::move(since);

        std::vector<uint8_t> ended;
        for (const auto& [target, dragger] : m_drags)
        {
            float h = 0, max = 0;
            if (!m_downedSince.count(target) || !HealthOf(dragger, h, max) || IsDowned(h, max) || !IsPlayer(target))
                ended.push_back(target);
        }
        for (uint8_t target : ended)
            EndDrag(target);

        if (m_downedSince.empty())
            m_gameOverSent = false;
        if (m_gameOverSent || m_downedSince.empty())
            return;
        for (auto& [id, at] : m_downedSince)
            if (now - at >= m_bleedOutMs && !m_drags.count(id))
                return EndGame(GameOverReason::BledOut, id);
        if (m_downedSince.size() == players)
            EndGame(GameOverReason::AllDown, 0);
    }

    void Coop::EndGame(GameOverReason reason, uint8_t playerId)
    {
        if (reason == GameOverReason::BledOut)
            LOG_INFO("Game over: %s bled out", NameOf(playerId).c_str());
        else if (reason == GameOverReason::Killed)
            LOG_INFO("Game over: %s was killed", NameOf(playerId).c_str());
        else
            LOG_INFO("Game over: everyone is down");
        m_gameOverSent = true;
        m_gameOver = true;
        m_gameOverInfo = GameOver{reason, playerId};
        SendToJoined(Encode(m_gameOverInfo), true);
    }

    void Coop::SendMyGear()
    {
        if (!m_gearToSend || !InGame())
            return;
        m_gearToSend = false;
        if (m_role == Role::Host)
            SendToJoined(Encode(PlayerGear{m_myId, m_myGear}), true);
        else
            m_net->Send(1, Encode(PlayerGear{m_myId, m_myGear}), true);
    }

    void Coop::SetRemoteGear(uint8_t id, std::vector<uint32_t> items)
    {
        RemotePlayer& remote = m_remotes[id];
        if (remote.gear == items)
            return;
        remote.gear = std::move(items);
        ++remote.gearChanges;
        LOG_DEBUG("%s now has %zu items equipped", remote.name.c_str(), remote.gear.size());
    }

    void Coop::AnswerJoin(uint32_t askId, bool yes)
    {
        for (auto& [peer, p] : m_players)
            if (p.awaitingApproval && p.askId == askId)
            {
                if (!yes)
                {
                    LOG_INFO("The host didn't let %s in", p.name.c_str());
                    return Kick(peer, "The host said no");
                }
                LOG_INFO("The host let %s in", p.name.c_str());
                m_approved.insert(p.name + "@" + p.from);
                p.awaitingApproval = false;
                return Admit(peer, p);
            }
    }

    bool Coop::StillAsking(uint32_t askId) const
    {
        for (const auto& [peer, p] : m_players)
            if (p.awaitingApproval && p.askId == askId)
                return true;
        return false;
    }

    void Coop::Admit(PeerId peer, Player& player)
    {
        if (player.wantsFiles)
        {
            player.fixingMods = true;
            player.lastAsk = GetTickCount();
            Writer w(MsgType::ModsDiffer);
            m_net->Send(peer, w.Take());
            return;
        }
        WelcomePlayer(peer, player);
    }

    void Coop::WelcomePlayer(PeerId peer, Player& player)
    {
        size_t joined = 0;
        for (auto& [_, p] : m_players)
            joined += p.joined;
        if (joined >= kMaxFriends)
            return Kick(peer, "The game is full");

        player.id = FreePlayerId();
        player.joined = true;

        ++m_joins;
        m_net->Send(peer, Encode(Welcome{player.id, m_myName, m_questMods, m_mode, m_myXp}));
        LOG_INFO("%s joined as player %u", player.name.c_str(), player.id);

        m_net->Send(peer, Encode(PlayerJoined{kHostPlayerId, m_myName}));
        for (auto& [otherPeer, other] : m_players)
            if (other.joined && otherPeer != peer)
                m_net->Send(peer, Encode(PlayerJoined{other.id, other.name}));
        SendToJoined(Encode(PlayerJoined{player.id, player.name}), true, peer);
        m_remotes[player.id].name = player.name;
        if (m_hasMyLooks)
        {
            net::PlayerLooks mine = m_myLooks;
            mine.player = kHostPlayerId;
            m_net->Send(peer, Encode(mine));
        }
        for (const auto& [id, looks] : m_looks)
            m_net->Send(peer, Encode(looks));
        if (!m_myAnim.empty())
            m_net->Send(peer, Encode(PlayerAnim{kHostPlayerId, m_myAnim}));
        if (!m_myModAnims.empty())
            m_net->Send(peer, Encode(PlayerModAnims{kHostPlayerId, m_myModAnims}));
        for (const auto& [id, paths] : m_modAnims)
            m_net->Send(peer, Encode(PlayerModAnims{id, paths}));
        for (const auto& [id, path] : m_anims)
            m_net->Send(peer, Encode(PlayerAnim{id, path}));
        m_lastPoseSent = 0;
        m_sentNpcs.clear();
        SendTalk(peer);
        for (auto& [_, g] : m_npcGear)
            g.changed = true;
        m_net->Send(peer, Encode(PlayerGear{kHostPlayerId, m_myGear}));
        for (auto& [id, remote] : m_remotes)
            if (id != player.id)
                m_net->Send(peer, Encode(PlayerGear{id, remote.gear}));
        if (m_myMaxHealth > 0)
            m_net->Send(peer, Encode(PlayerHealth{kHostPlayerId, m_myHealth, m_myMaxHealth}));
        for (auto& [id, remote] : m_remotes)
            if (id != player.id && remote.maxHealth > 0)
                m_net->Send(peer, Encode(PlayerHealth{id, remote.health, remote.maxHealth}));
        if (m_hasPose)
            SendTravel(player.id, kHostPlayerId, m_myPose);
    }

    void Coop::SetQuestMods(const std::vector<std::string>& mods)
    {
        m_questMods.clear();
        for (const std::string& mod : mods)
            if (!IsPlainModFileName(mod, ModKind::Plugin) || m_questMods.size() >= kMaxQuestMods)
                LOG_WARN("Quests: \"%s\" isn't a mod file name (.esp or .esm), or too many; left out", mod.c_str());
            else
            {
                m_questMods.push_back(mod);
                LOG_INFO("Quests: our game runs %s's quests for everyone", mod.c_str());
            }
    }

    void Coop::SendModList(PeerId peer)
    {
        for (size_t i = 0; i < m_mods.size(); i += kModFilesPerMessage)
        {
            ModFiles page;
            size_t end = i + kModFilesPerMessage < m_mods.size() ? i + kModFilesPerMessage : m_mods.size();
            page.files.assign(m_mods.begin() + i, m_mods.begin() + end);
            page.last = end == m_mods.size();
            m_net->Send(peer, Encode(page));
        }
    }

    void Coop::CheckHostMods()
    {
        ModDifference d = CompareMods(m_hostMods, m_mods);
        m_modsDiffer = d.Blocks();
        if (!d.Any())
            return LOG_INFO("Mods: ours match the host's (%zu files)", m_mods.size());
        LOG_WARN("Mods: ours differ from the host's (%s):", m_modsDiffer ? "we can't join" : "xNVSE plugins only");
        for (const std::string& line : DescribeForLog(d))
            LOG_WARN("  %s", line.c_str());
        if (!m_modsDiffer)
            m_modReport = DescribeForScreen(d, "");
    }

    void Coop::OfferModFix()
    {
        ModDifference d = CompareMods(m_hostMods, m_mods);
        std::string text = DescribeForScreen(d, "");
        if (!m_modFolders.manager.empty())
        {
            LOG_WARN("Mods: %s sets our load order, so it isn't fixed from here", m_modFolders.manager.c_str());
            m_modReport = text + "\n" + m_modFolders.manager + " sets your mods: match the host's there (the host can "
                "send their profile's plugins.txt and loadorder.txt, and any mods you're missing), then join again.";
            return Stop();
        }
        m_modsWanted.clear();
        m_modsExtra = d.extra;
        std::string unsendable, ttw;
        uint64_t bytes = 0;
        for (size_t i = 0; i < m_hostMods.size(); ++i)
        {
            const ModFile& f = m_hostMods[i];
            bool needed = f.kind != ModKind::Dll &&
                (std::find(d.missing.begin(), d.missing.end(), f.name) != d.missing.end() ||
                    std::find(d.different.begin(), d.different.end(), f.name) != d.different.end());
            if (needed && f.hashed)
            {
                m_modsWanted.push_back({static_cast<uint16_t>(i), f});
                bytes += f.size;
            }
            else if (needed)
            {
                std::string& list = IsMadeFromOwnGame(f.name) ? ttw : unsendable;
                list += (list.empty() ? "" : ", ") + f.name;
            }
        }
        if (!unsendable.empty() || !ttw.empty())
        {
            LOG_WARN("Mods: the game's own files differ (%s%s%s): not fixable from here", unsendable.c_str(),
                !unsendable.empty() && !ttw.empty() ? ", " : "", ttw.c_str());
            m_modReport = text;
            if (!unsendable.empty())
                m_modReport += "\nThe game's own files differ (" + unsendable +
                    "): check them in Steam (the game's Properties, Installed Files, Verify).";
            if (!ttw.empty())
                m_modReport += "\nTale of Two Wastelands' files differ (" + ttw + "): install the same TTW version as "
                    "the host (its installer makes them from your own Fallout 3).";
            return Stop();
        }
        char size[32];
        std::snprintf(size, sizeof(size), "%.1f MB", bytes / 1048576.0);
        ULARGE_INTEGER available{};
        if (bytes && GetDiskFreeSpaceExA(m_modFolders.data.c_str(), &available, nullptr, nullptr) &&
            bytes > available.QuadPart)
        {
            char room[32];
            std::snprintf(room, sizeof(room), "%.1f MB", available.QuadPart / 1048576.0);
            LOG_WARN("Mods: the host's files need %s and the disk has %s free: not fetching them", size, room);
            m_modReport = text + "\nThe host's files need " + size + " and your disk has " + room + " free: make room, "
                "then join again.";
            return Stop();
        }
        if (m_modsWanted.empty())
            text += "\n\nFix your mods to match the host's? (Nothing is deleted; changes are backed up.)";
        else
            text += "\n\nGet " + std::to_string(m_modsWanted.size()) + (m_modsWanted.size() == 1 ? " file (" : " files (") +
                size + ") from the host and fix your mods to match? Only accept files from people you trust. "
                "(Nothing is deleted; replaced files are backed up.)";
        LOG_INFO("Mods: asking whether to fix ours (%zu files to fetch, %s)", m_modsWanted.size(), size);
        m_modQuestion = text;
    }

    void Coop::AnswerModFix(bool yes)
    {
        if (m_role != Role::Friend || m_friendState != FriendState::FixingMods || m_downloading)
            return;
        if (!yes)
        {
            LOG_INFO("Mods: not fixing ours; leaving");
            return Stop();
        }
        LOG_INFO("Mods: fixing ours (%zu files to fetch)", m_modsWanted.size());
        m_downloading = true;
        m_download.Start(m_modsWanted, m_modFolders.downloads);
    }

    void Coop::UpdateDownload()
    {
        std::vector<ModFileAsk> asks;
        m_download.Asks(asks);
        for (const ModFileAsk& ask : asks)
            m_net->Send(1, Encode(ask));
        if (!m_download.Done() && !m_download.Failed())
            return;
        std::string error;
        if (m_download.Failed())
            m_modFixResult = "Couldn't get the host's mods: " + m_download.Error() + ". Nothing in Data was changed.";
        else if (!ApplyModFix(m_modFolders, m_hostMods, m_download.Finished(), m_modsExtra, error))
            m_modFixResult = "Couldn't fix your mods: " + error + ". Older files are in " + m_modFolders.backup;
        else
        {
            m_modFixResult = "Your mods now match the host's. Restart the game, then join again.";
            LOG_INFO("Mods: older files and our old plugins.txt are in %s", m_modFolders.backup.c_str());
        }
        LOG_INFO("Mods: %s", m_modFixResult.c_str());
        Stop();
    }

    void Coop::Kick(PeerId peer, const std::string& reason)
    {
        LOG_WARN("Removing connection %u: %s", peer, reason.c_str());
        m_net->Send(peer, Encode(Reject{reason}));
        m_net->Disconnect(peer);
        RemovePlayer(peer);
    }

    void Coop::RemovePlayer(PeerId peer)
    {
        auto it = m_players.find(peer);
        if (it == m_players.end())
            return;
        m_modServer.Forget(peer);
        bool wasJoined = it->second.joined;
        uint8_t id = it->second.id;
        if (wasJoined)
            LOG_INFO("%s left", it->second.name.c_str());
        m_players.erase(it);
        if (wasJoined)
        {
            m_remotes.erase(id);
            m_looks.erase(id);
            HeardAnim(id, "");
            HeardModAnims(id, {});
            m_views.erase(id);
            if (m_talk.Busy() && m_talk.Talker() == id)
            {
                m_talk.Left(id);
                SendTalk(0);
            }
            SendToJoined(Encode(PlayerLeft{id}), true);
            if (m_wait.IsOpen() && m_wait.Requester() == id)
            {
                m_wait.Left(id);
                SendWaitResult(WaitResult{id, false, 0, false, kNobody});
            }
            else
                m_wait.Left(id);
            m_loads.Left(id);
        }
    }

    bool SamePlace(const Pose& a, const Pose& b)
    {
        return Within(a, b, kSamePlaceDistance);
    }

    bool Within(const Pose& a, const Pose& b, float distance)
    {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return a.space == b.space && dx * dx + dy * dy + dz * dz <= distance * distance;
    }

    bool Coop::TakeTravel(Pose& out, uint8_t& leader)
    {
        if (!m_hasTravel)
            return false;
        out = m_travel;
        leader = m_travelLeader;
        m_hasTravel = false;
        return true;
    }

    void Coop::CheckMyTravel(uint32_t now)
    {
        if (!m_hasPose)
            return;
        Pose last = m_place;
        bool had = m_hasPlace;
        m_place = m_myPose;
        m_hasPlace = true;
        if (!had)
        {
            bool regroup = m_regroup;
            m_regroup = false;
            if (m_role == Role::Friend)
            {
                if (regroup)
                    m_net->Send(1, net::EncodeRegroup());
            }
            else if (regroup || !m_hasGroupPlace)
            {
                m_groupPlace = m_myPose;
                m_groupLeader = kHostPlayerId;
                m_hasGroupPlace = true;
                if (regroup)
                {
                    m_groupMovedAt = now;
                    LOG_INFO("Loaded a save; bringing everyone here");
                    ExpectLoads(kHostPlayerId, now);
                    for (auto& [peer, p] : m_players)
                        if (p.joined)
                            SendTravel(p.id, kHostPlayerId, m_myPose);
                }
            }
            return;
        }
        float dx = m_myPose.x - last.x, dy = m_myPose.y - last.y, dz = m_myPose.z - last.z;
        if (m_myPose.space == last.space && dx * dx + dy * dy + dz * dz <= kTeleportDistance * kTeleportDistance)
            return;
        if (m_expecting && SamePlace(m_myPose, m_expect))
        {
            m_expecting = false;
            LOG_DEBUG("Arrived where the group went (place %08X)", m_myPose.space);
            SendArrived();
            return;
        }
        if (m_expecting)
            SendArrived();
        m_expecting = false;
        LOG_INFO("We went to place %08X (%.0f, %.0f, %.0f)", m_myPose.space, m_myPose.x, m_myPose.y, m_myPose.z);
        if (m_role == Role::Host)
            GroupTravels(kHostPlayerId, m_myPose, now);
        else
            m_net->Send(1, Encode(net::Travel{m_myId, m_myPose.space, m_myPose.x, m_myPose.y, m_myPose.z,
                m_myPose.heading}));
    }

    void Coop::GroupTravels(uint8_t leader, const Pose& to, uint32_t now)
    {
        if (m_hasGroupPlace && now - m_groupMovedAt < m_travelCooldownMs)
        {
            if (!SamePlace(to, m_groupPlace))
            {
                LOG_INFO("%s went elsewhere just after the group moved; bringing them back", NameOf(leader).c_str());
                if (leader == kHostPlayerId)
                    PullMe(m_groupLeader, m_groupPlace);
                else
                    SendTravel(leader, m_groupLeader, m_groupPlace);
            }
            return;
        }
        m_groupPlace = to;
        m_groupLeader = leader;
        m_groupMovedAt = now;
        m_hasGroupPlace = true;
        LOG_INFO("%s led the group to place %08X (%.0f, %.0f, %.0f)", NameOf(leader).c_str(), to.space, to.x, to.y,
            to.z);
        ExpectLoads(leader, now);
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id != leader)
                SendTravel(p.id, leader, to);
        if (leader != kHostPlayerId)
            PullMe(leader, to);
    }

    void Coop::SendTravel(uint8_t toPlayer, uint8_t leader, const Pose& to)
    {
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id == toPlayer)
                m_net->Send(peer, Encode(net::Travel{leader, to.space, to.x, to.y, to.z, to.heading}));
    }

    void Coop::PullMe(uint8_t leader, const Pose& to)
    {
        if (m_hasPose && SamePlace(m_myPose, to))
        {
            m_hasTravel = m_expecting = false;
            SendArrived();
            return;
        }
        m_travel = m_expect = to;
        m_travelLeader = leader;
        m_hasTravel = m_expecting = true;
    }

    void Coop::SendToJoined(const Bytes& data, bool reliable, PeerId except)
    {
        for (auto& [peer, p] : m_players)
            if (p.joined && peer != except)
                m_net->Send(peer, data, reliable);
    }

    void Coop::SendMyPose(uint32_t now)
    {
        if (!m_hasPose || !InGame())
            return;
        const Pose& a = m_myPose;
        const Pose& b = m_sentPose;
        bool moved = a.space != b.space || std::fabs(a.x - b.x) > 1 || std::fabs(a.y - b.y) > 1 ||
            std::fabs(a.z - b.z) > 1 || std::fabs(a.heading - b.heading) > 0.01f || a.flags != b.flags ||
            a.moves != b.moves || std::fabs(a.pitch - b.pitch) > 0.01f || a.runSpeed != b.runSpeed;
        if (m_lastPoseSent != 0 && now - m_lastPoseSent < (moved ? kPoseIntervalMs : kIdlePoseIntervalMs))
            return;

        PlayerState state{m_myId, now, a.space, a.x, a.y, a.z, a.heading, a.flags, a.moves, a.pitch, a.runSpeed};
        if (m_role == Role::Host)
            SendToJoined(Encode(state), false);
        else
            m_net->Send(1, Encode(state), false);
        m_sentPose = m_myPose;
        m_lastPoseSent = now;
    }

    void Coop::SetNpcs(uint32_t space, std::vector<NpcState> npcs)
    {
        if (npcs.size() > kMaxNpcs)
            npcs.resize(kMaxNpcs);
        if (space != m_npcSpace)
            m_sentNpcs.clear();
        m_npcSpace = space;
        m_myNpcs = std::move(npcs);
    }

    void Coop::SendNpcs(uint32_t now)
    {
        if (now - m_lastNpcsSent < kPoseIntervalMs)
            return;
        bool anyone = false;
        for (auto& [_, p] : m_players)
            anyone |= p.joined;
        if (!anyone || m_npcSpace == 0)
        {
            m_npcShots.clear();
            return;
        }
        m_lastNpcsSent = now;

        for (auto it = m_sentNpcs.begin(); it != m_sentNpcs.end();)
        {
            bool still = std::any_of(m_myNpcs.begin(), m_myNpcs.end(),
                [&](const NpcState& n) { return n.ref == it->first; });
            it = still ? std::next(it) : m_sentNpcs.erase(it);
        }
        NpcStates msg{now, m_npcSpace, {}};
        for (const NpcState& n : m_myNpcs)
        {
            auto it = m_sentNpcs.find(n.ref);
            if (it != m_sentNpcs.end())
            {
                const NpcState& b = it->second.state;
                bool moved = std::fabs(n.x - b.x) > 1 || std::fabs(n.y - b.y) > 1 || std::fabs(n.z - b.z) > 1 ||
                    std::fabs(n.heading - b.heading) > 0.01f || n.flags != b.flags || n.moves != b.moves ||
                    n.health != b.health || n.maxHealth != b.maxHealth || n.idle != b.idle || n.weapon != b.weapon ||
                    n.base != b.base;
                if (!moved && now - it->second.at < kIdlePoseIntervalMs)
                    continue;
            }
            m_sentNpcs[n.ref] = SentNpc{n, now};
            msg.npcs.push_back(n);
        }
        if (!msg.npcs.empty() || now - m_lastNpcListSent >= kIdlePoseIntervalMs)
        {
            m_lastNpcListSent = now;
            Bytes data = Encode(msg);
            m_npcUpdatesSent += static_cast<uint32_t>(msg.npcs.size());
            m_npcBytesSent += static_cast<uint32_t>(data.size());
            SendToJoined(data, false);
        }
        for (auto it = m_npcGear.begin(); it != m_npcGear.end();)
        {
            bool still = std::any_of(m_myNpcs.begin(), m_myNpcs.end(),
                [&](const NpcState& n) { return n.ref == it->first; });
            if (!still)
            {
                it = m_npcGear.erase(it);
                continue;
            }
            NpcGearSent& g = it->second;
            if (g.changed || now - g.sentAt >= kNpcGearResendMs)
            {
                Bytes data = Encode(NpcGear{it->first, g.items});
                m_npcBytesSent += static_cast<uint32_t>(data.size());
                SendToJoined(data, true);
                g.changed = false;
                g.sentAt = now;
            }
            ++it;
        }
        if (!m_npcShots.empty())
        {
            NpcAttacks attacks;
            for (auto& [ref, attack] : m_npcShots)
                attacks.attacks.push_back(attack);
            m_npcShots.clear();
            Bytes data = Encode(attacks);
            m_npcBytesSent += static_cast<uint32_t>(data.size());
            SendToJoined(data, true);
        }
    }

    void Coop::SetNpcGear(uint32_t ref, std::vector<WornItem> items)
    {
        if (m_role != Role::Host || !IsSharedRef(ref))
            return;
        std::sort(items.begin(), items.end(), [](const WornItem& a, const WornItem& b) {
            return a.item != b.item ? a.item < b.item : a.health < b.health; });
        items.erase(std::unique(items.begin(), items.end(),
            [](const WornItem& a, const WornItem& b) { return a.item == b.item; }), items.end());
        items.erase(std::remove_if(items.begin(), items.end(), [](const WornItem& i) {
            return i.item == 0 || IsMadeWhilePlaying(i.item) || (i.health > 100 && i.health != kNoCondition); }),
            items.end());
        if (items.size() > kMaxGearItems)
            items.resize(kMaxGearItems);
        auto it = m_npcGear.find(ref);
        if (it == m_npcGear.end())
        {
            if (m_npcGear.size() >= kMaxNpcs)
                return;
            it = m_npcGear.emplace(ref, NpcGearSent{}).first;
        }
        else if (it->second.items == items)
            return;
        it->second.items = std::move(items);
        it->second.changed = true;
    }

    void Coop::NpcAttacked(uint32_t ref, uint8_t shots, bool reloaded, bool released)
    {
        if (m_role != Role::Host || !IsSharedRef(ref) || (shots == 0 && !reloaded && !released))
            return;
        auto it = m_npcShots.find(ref);
        if (it == m_npcShots.end())
        {
            if (m_npcShots.size() >= kMaxNpcs)
                return;
            it = m_npcShots.emplace(ref, NpcAttack{ref, 0, false, false}).first;
        }
        int total = it->second.shots + shots;
        it->second.shots = static_cast<uint8_t>(total < kMaxShots ? total : kMaxShots);
        it->second.reload |= reloaded;
        it->second.release |= released;
    }

    void Coop::HeardNpcs(const NpcStates& m)
    {
        uint32_t now = GetTickCount();
        m_npcListAt = now;
        m_npcListSpace = m.space;
        for (const NpcState& n : m.npcs)
        {
            auto it = m_npcs.find(n.ref);
            if (it == m_npcs.end())
            {
                if (m_npcs.size() >= kMaxKnownNpcs)
                {
                    if (!m_warnedNpcs)
                        LOG_WARN("The host shares more than %zu NPCs; ignoring the rest", kMaxKnownNpcs);
                    m_warnedNpcs = true;
                    continue;
                }
                it = m_npcs.emplace(n.ref, RemoteNpc{}).first;
            }
            RemoteNpc& npc = it->second;
            uint8_t poseFlags = n.flags & kKnownPlayerStateFlags;
            npc.motion.Add(m.timeMs, Pose{m.space, n.x, n.y, n.z, n.heading, poseFlags, n.moves}, now);
            npc.heardAt = now;
            if (static_cast<int32_t>(m.timeMs - npc.stateTime) >= 0 || npc.maxHealth == 0)
            {
                npc.stateTime = m.timeMs;
                npc.dead = (n.flags & kNpcDead) != 0;
                npc.limbs = n.limbs;
                npc.pile = n.pile;
                npc.down = (n.flags & kNpcDown) != 0;
                npc.sitting = (n.flags & kNpcSitting) != 0;
                npc.health = n.health;
                npc.maxHealth = n.maxHealth;
                npc.idle = n.idle;
                npc.weapon = n.weapon;
                npc.base = n.base;
            }
        }
    }

    void Coop::ForgetOldNpcs(uint32_t now)
    {
        for (auto it = m_npcs.begin(); it != m_npcs.end();)
            it = now - it->second.heardAt > kNpcTimeoutMs ? m_npcs.erase(it) : std::next(it);
    }

    uint8_t Coop::FreePlayerId() const
    {
        for (uint8_t id = 1;; ++id)
        {
            bool used = false;
            for (auto& [_, p] : m_players)
                used |= p.joined && p.id == id;
            if (!used)
                return id;
        }
    }

    void Coop::SetDoors(std::vector<DoorState> doors)
    {
        if (m_role == Role::Host)
            m_myDoors = std::move(doors);
    }

    void Coop::SendDoors(uint32_t now)
    {
        if (m_players.empty())
            return;
        bool all = now - m_lastDoorsSent >= kDoorResendMs;
        if (all)
            m_lastDoorsSent = now;
        DoorStates msg;
        auto flush = [&]()
        {
            if (!msg.doors.empty())
                SendToJoined(Encode(msg), true);
            msg.doors.clear();
        };
        for (const DoorState& d : m_myDoors)
        {
            auto it = m_sentDoors.find(d.ref);
            bool changed = it == m_sentDoors.end() || it->second != d;
            if (!changed && !all)
                continue;
            if (changed && it != m_sentDoors.end())
                LOG_DEBUG("Door %08X now %s, %s", d.ref, d.flags & kDoorOpen ? "open" : "closed",
                    d.flags & kDoorLocked ? "locked" : "unlocked");
            m_sentDoors[d.ref] = d;
            if (std::any_of(msg.doors.begin(), msg.doors.end(), [&](const DoorState& o) { return o.ref == d.ref; }))
                continue;
            msg.doors.push_back(d);
            if (msg.doors.size() == kMaxDoorsPerMessage)
                flush();
        }
        flush();
        if (m_sentDoors.size() > 4 * kMaxKnownDoors)
            m_sentDoors.clear();
    }

    std::vector<DoorState> Coop::ObserveDoors(uint32_t now, const std::vector<LocalDoor>& local)
    {
        std::vector<DoorState> uses;
        if (m_role != Role::Friend || m_friendState != FriendState::Joined)
            return {};
        std::vector<DoorState> fixes = m_friendDoors.Observe(now, local, uses);
        for (const DoorState& f : fixes)
            m_desyncs.Saw({DesyncKind::Door, f.ref, DesyncAspect::Door, std::string("should be ") +
                (f.flags & kDoorOpen ? "open" : "closed") + (f.flags & kDoorLocked ? ", locked" : ", unlocked")}, now);
        for (const DoorState& d : uses)
        {
            LOG_DEBUG("I used door %08X: %s, %s", d.ref, d.flags & kDoorOpen ? "open" : "closed",
                d.flags & kDoorLocked ? "locked" : "unlocked");
            m_net->Send(1, EncodeDoorUse(d), true);
        }
        return fixes;
    }

    std::vector<uint32_t> Coop::ViewedContainers() const
    {
        std::vector<uint32_t> refs;
        for (const auto& [id, view] : m_views)
            if (view.ref && std::find(refs.begin(), refs.end(), view.ref) == refs.end())
                refs.push_back(view.ref);
        return refs;
    }

    void Coop::SetContainerContents(uint32_t ref, const std::vector<ItemCount>& items)
    {
        for (auto& [id, view] : m_views)
        {
            if (view.ref != ref || (view.sent && view.sentSeen == view.seen && view.sentItems == items))
                continue;
            auto peer = std::find_if(m_players.begin(), m_players.end(),
                [&](const auto& p) { return p.second.id == id && p.second.joined; });
            if (peer == m_players.end())
                continue;
            m_net->Send(peer->first, Encode(ContainerContents{ref, view.seen, items}), true);
            view.sent = true;
            view.sentSeen = view.seen;
            view.sentItems = items;
        }
    }

    void Coop::ContainerChangeDone(uint8_t playerId, uint32_t seq, const ItemCount& refund)
    {
        auto view = m_views.find(playerId);
        if (view != m_views.end() && static_cast<int32_t>(seq - view->second.seen) > 0)
            view->second.seen = seq;
        if (refund.count == 0)
            return;
        auto peer = std::find_if(m_players.begin(), m_players.end(),
            [&](const auto& p) { return p.second.id == playerId && p.second.joined; });
        if (peer != m_players.end())
            m_net->Send(peer->first, EncodeItemRefund(refund), true);
    }

    void Coop::ViewContainer(uint32_t hostRef, bool open)
    {
        if (m_role == Role::Friend && InGame() && m_friendContainer.View(hostRef, open))
            m_net->Send(1, EncodeContainerView(hostRef), true);
    }

    std::vector<ItemCount> Coop::LookAtContainer(const std::vector<ItemCount>& local)
    {
        if (m_role != Role::Friend || !InGame())
            return {};
        std::vector<ContainerChange> changes;
        std::vector<ItemCount> fixes = m_friendContainer.Look(local, changes);
        for (const ContainerChange& c : changes)
        {
            LOG_DEBUG("I %s %d of %08X %s container %08X", c.change.count < 0 ? "took" : "put",
                c.change.count < 0 ? -c.change.count : c.change.count, c.change.item,
                c.change.count < 0 ? "from" : "into", c.ref);
            m_net->Send(1, Encode(c), true);
        }
        return fixes;
    }

    void Coop::SetWorldItems(const std::vector<ItemState>& items)
    {
        if (m_role == Role::Host)
        {
            m_myItems = items;
            m_itemsNew = true;
        }
    }

    void Coop::SendItems(uint32_t now)
    {
        if (m_players.empty())
            return;
        bool all = now - m_lastItemsSent >= kItemResendMs;
        if (!all && !m_itemsNew)
            return;
        m_itemsNew = false;
        if (all)
            m_lastItemsSent = now;
        ItemStates msg;
        auto flush = [&]()
        {
            if (!msg.items.empty())
                SendToJoined(Encode(msg), true);
            msg.items.clear();
        };
        auto add = [&](const ItemState& it)
        {
            msg.items.push_back(it);
            if (msg.items.size() == kMaxItemsPerMessage)
                flush();
        };
        std::map<uint32_t, ItemState> now_;
        for (const ItemState& it : m_myItems)
        {
            if (!now_.emplace(it.ref, it).second)
                continue;
            auto sent = m_sentItems.find(it.ref);
            bool changed = sent == m_sentItems.end() || sent->second.present != it.present ||
                sent->second.base != it.base || sent->second.count != it.count || sent->second.held != it.held;
            if (!changed)
                changed = ItemPose{it.x, it.y, it.z, it.rx, it.ry, it.rz}.Differs({sent->second.x, sent->second.y,
                    sent->second.z, sent->second.rx, sent->second.ry, sent->second.rz}, kItemSendMoveDistance,
                    kItemSendTurn);
            if (changed || all)
                add(it);
            else
                now_[it.ref] = sent->second;
        }
        for (const auto& [ref, it] : m_sentItems)
            if (now_.find(ref) == now_.end() && IsMadeWhilePlaying(ref) && it.present)
            {
                ItemState gone = it;
                gone.present = false;
                add(gone);
            }
        flush();
        m_sentItems = std::move(now_);
    }

    void Coop::RefundItems(uint8_t playerId, const ItemCount& refund)
    {
        if (refund.count == 0)
            return;
        auto peer = std::find_if(m_players.begin(), m_players.end(),
            [&](const auto& p) { return p.second.id == playerId && p.second.joined; });
        if (peer != m_players.end())
            m_net->Send(peer->first, EncodeItemRefund(refund), true);
    }

    ItemActions Coop::LookAtItems(const std::vector<LocalItem>& local, bool sameArea)
    {
        if (m_role != Role::Friend || !InGame())
            return {};
        ItemActions actions = m_friendItems.Look(local, sameArea, GetTickCount());
        for (const Disagreement& d : actions.differ)
            m_desyncs.Saw(d, GetTickCount());
        uint32_t now = GetTickCount();
        for (const ItemMove& m : actions.moved)
        {
            auto last = m_lastMoveSent.find(m.ref);
            if (last == m_lastMoveSent.end() || now - last->second > 3000)
                LOG_DEBUG("Items: our game moved %08X (to %.0f, %.0f, %.0f%s)", m.ref, m.x, m.y, m.z,
                    m.held ? ", carried" : "");
            m_lastMoveSent[m.ref] = now;
        }
        if (m_lastMoveSent.size() > 256)
            m_lastMoveSent.clear();
        for (size_t i = 0; i < actions.moved.size(); i += kMaxItemMovesPerMessage)
        {
            ItemMoves msg;
            msg.moves.assign(actions.moved.begin() + i,
                actions.moved.begin() + std::min(actions.moved.size(), i + kMaxItemMovesPerMessage));
            m_net->Send(1, Encode(msg), true);
        }
        for (const ItemTake& t : actions.takes)
        {
            if (t.health != kNoCondition || t.mods)
                LOG_DEBUG("I picked up %08X (%d of %08X at %d%% condition, mods %d)", t.ref, t.count, t.base, t.health,
                    t.mods);
            else
                LOG_DEBUG("I picked up %08X (%d of %08X)", t.ref, t.count, t.base);
            m_net->Send(1, Encode(t), true);
        }
        for (const ItemDrop& d : actions.drops)
        {
            if (d.health != kNoCondition || d.mods)
                LOG_DEBUG("I dropped %d of %08X at %d%% condition, mods %d", d.count, d.base, d.health, d.mods);
            else
                LOG_DEBUG("I dropped %d of %08X", d.count, d.base);
            m_net->Send(1, Encode(d), true);
        }
        return actions;
    }

    void Coop::SendQuestStates(const std::vector<QuestState>& quests)
    {
        if (m_role != Role::Host)
            return;
        QuestStates msg;
        for (const QuestState& q : quests)
        {
            msg.quests.push_back(q);
            if (msg.quests.size() == kMaxQuestsPerMessage)
            {
                SendToJoined(Encode(msg), true);
                msg.quests.clear();
            }
        }
        if (!msg.quests.empty())
            SendToJoined(Encode(msg), true);
    }

    void Coop::SendRefEnables(const std::vector<RefEnable>& changes)
    {
        if (m_role != Role::Host)
            return;
        for (size_t i = 0; i < changes.size(); i += kMaxEnablesPerMessage)
        {
            std::vector<RefEnable> part(changes.begin() + i,
                changes.begin() + (i + kMaxEnablesPerMessage < changes.size() ? i + kMaxEnablesPerMessage : changes.size()));
            SendToJoined(Encode(part), true);
        }
    }

    void Coop::ShareProgress(int32_t xp, int32_t karma)
    {
        if (!InGame())
            return;
        xp = xp < 0 ? 0 : xp > kMaxXpGain ? kMaxXpGain : xp;
        karma = karma < -kMaxKarmaChange ? -kMaxKarmaChange : karma > kMaxKarmaChange ? kMaxKarmaChange : karma;
        if (!xp && !karma)
            return;
        Progress m{m_myId, xp, karma};
        if (m_role == Role::Host)
            SendToJoined(Encode(m), true);
        else
            m_net->Send(1, Encode(m), true);
    }

    void Coop::SendReputations(const std::vector<Reputation>& reps)
    {
        if (m_role != Role::Host)
            return;
        for (size_t i = 0; i < reps.size(); i += kMaxReputationsPerMessage)
        {
            std::vector<Reputation> part(reps.begin() + i,
                reps.begin() + (i + kMaxReputationsPerMessage < reps.size() ? i + kMaxReputationsPerMessage : reps.size()));
            SendToJoined(Encode(part), true);
        }
    }

    bool Coop::IsPlayer(uint8_t id) const
    {
        if (id == m_myId || id == kHostPlayerId)
            return true;
        if (m_role == Role::Host)
        {
            for (const auto& [peer, p] : m_players)
                if (p.joined && p.id == id)
                    return true;
            return false;
        }
        return m_remotes.count(id) != 0;
    }

    void Coop::AskTeleport(uint8_t mover, uint8_t target)
    {
        if (!InGame() || mover == target || !IsPlayer(mover) || !IsPlayer(target))
            return;
        if (mover == m_myId)
        {
            if (m_teleports.size() < 8)
                m_teleports.push_back(target);
            return;
        }
        if (m_role == Role::Friend)
        {
            m_net->Send(1, Encode(Teleport{mover, target}), true);
            return;
        }
        for (const auto& [peer, p] : m_players)
            if (p.joined && p.id == mover)
                m_net->Send(peer, Encode(Teleport{mover, target}), true);
    }

    void Coop::ShareTravelMarkers(const std::vector<uint32_t>& refs)
    {
        if (!InGame())
            return;
        for (size_t i = 0; i < refs.size(); i += kMaxMarkersPerMessage)
        {
            std::vector<uint32_t> part(refs.begin() + i,
                refs.begin() + (i + kMaxMarkersPerMessage < refs.size() ? i + kMaxMarkersPerMessage : refs.size()));
            if (m_role == Role::Host)
                SendToJoined(EncodeTravelMarkers(part), true);
            else
                m_net->Send(1, EncodeTravelMarkers(part), true);
        }
    }

    void Coop::ShareReputationGains(const std::vector<ReputationGain>& gains)
    {
        if (m_role != Role::Friend || !InGame())
            return;
        for (size_t i = 0; i < gains.size(); i += kMaxReputationsPerMessage)
            m_net->Send(1, EncodeGains(std::vector<ReputationGain>(gains.begin() + i,
                gains.begin() + (i + kMaxReputationsPerMessage < gains.size() ? i + kMaxReputationsPerMessage :
                gains.size()))), true);
    }

    void Coop::ShareProgressFor(uint8_t playerId, int32_t xp, int32_t karma)
    {
        if (m_role != Role::Host || !InGame())
            return;
        xp = xp < 0 ? 0 : xp > kMaxXpGain ? kMaxXpGain : xp;
        karma = karma < -kMaxKarmaChange ? -kMaxKarmaChange : karma > kMaxKarmaChange ? kMaxKarmaChange : karma;
        if (!xp && !karma)
            return;
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id != playerId)
                m_net->Send(peer, Encode(Progress{playerId, xp, karma}), true);
    }

    void Coop::SetMyConversation(bool talking, uint32_t npc)
    {
        if (m_role == Role::Friend)
        {
            if (InGame())
                m_net->Send(1, Encode(TalkState{m_myId, talking, talking ? npc : 0}), true);
            return;
        }
        if (m_role != Role::Host)
            return;
        bool wasBusy = m_talk.Busy();
        uint8_t wasTalker = m_talk.Talker();
        if (!m_talk.Report(m_myId, talking, npc))
            return;
        if (m_talk.Busy() != wasBusy || m_talk.Talker() != wasTalker || talking)
            SendTalk(0);
    }

    void Coop::ShareSave(const std::string& file, bool load)
    {
        if (m_role != Role::Host)
            return;
        std::string name = SaveNameOf(file);
        if (name.empty())
        {
            LOG_WARN("Saves: \"%s\" can't be linked (only letters, digits, spaces and - _ . , ( ) ' are)", file.c_str());
            return;
        }
        SendToJoined(Encode(SaveOrder{load, name}), true);
        if (load)
            ExpectLoads(kHostPlayerId, GetTickCount());
    }

    void Coop::ExpectLoads(uint8_t except, uint32_t now)
    {
        std::vector<uint8_t> players;
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id != except)
                players.push_back(p.id);
        if (except != kHostPlayerId)
            players.push_back(kHostPlayerId);
        m_loads.Expect(players, now);
        UpdateFreeze(now);
    }

    void Coop::UpdateFreeze(uint32_t now)
    {
        std::vector<uint8_t> late;
        bool frozen = m_loads.Waiting(now, &late);
        for (uint8_t id : late)
            LOG_WARN("Loading: %s didn't arrive within %u s; going on without them", NameOf(id).c_str(),
                LoadGate::kMaxWaitMs / 1000);
        uint8_t waitingFor = m_loads.WaitingFor();
        if (frozen == m_frozen && waitingFor == m_frozenFor)
            return;
        m_frozen = frozen;
        m_frozenFor = waitingFor;
        LOG_INFO("Loading: %s", frozen ? ("waiting for " + NameOf(waitingFor)).c_str() : "everyone is here");
        SendToJoined(Encode(Freeze{frozen, waitingFor}), true);
    }

    void Coop::SendArrived()
    {
        if (m_role == Role::Friend && InGame())
        {
            Writer w(MsgType::Arrived);
            m_net->Send(1, w.Take(), true);
        }
        else if (m_role == Role::Host)
        {
            m_loads.Arrived(kHostPlayerId);
            UpdateFreeze(GetTickCount());
        }
    }

    void Coop::AskHostToSave(const std::string& ourSave)
    {
        std::string name = SaveNameOf(ourSave);
        if (m_role == Role::Friend && InGame() && !name.empty())
            m_net->Send(1, Encode(SaveOrder{false, name}), true);
    }

    void Coop::AskHostToLoad(const std::string& hostSave)
    {
        std::string name = SaveNameOf(hostSave);
        if (m_role == Role::Friend && InGame() && !name.empty())
            m_net->Send(1, Encode(SaveOrder{true, name}), true);
    }

    void Coop::RefuseLoad(uint8_t playerId)
    {
        if (m_role != Role::Host)
            return;
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id == playerId)
            {
                Writer w(MsgType::LoadRefused);
                m_net->Send(peer, w.Take(), true);
            }
    }

    void Coop::AskToWait(uint8_t hours, bool sleep)
    {
        hours = hours > kMaxWaitHours ? kMaxWaitHours : hours;
        if (m_role == Role::Friend)
        {
            if (InGame())
                m_net->Send(1, Encode(WaitAsk{hours, sleep}), true);
            return;
        }
        if (m_role != Role::Host)
            return;
        if (hours == 0)
        {
            if (m_wait.IsOpen() && m_wait.Requester() == m_myId)
            {
                m_wait.Cancel(m_myId);
                LOG_INFO("Wait: we called it off");
                SendWaitResult(WaitResult{m_myId, false, 0, false, kNobody});
            }
            return;
        }
        StartWaitVote(m_myId, hours, sleep, 0);
    }

    void Coop::AnswerWait(bool yes)
    {
        if (m_role == Role::Friend)
        {
            if (InGame())
                m_net->Send(1, Encode(WaitAnswer{yes}), true);
            return;
        }
        if (m_role != Role::Host)
            return;
        m_wait.Answer(m_myId, yes);
        CheckWaitVote(GetTickCount());
    }

    void Coop::StartWaitVote(uint8_t requester, uint8_t hours, bool sleep, PeerId from)
    {
        std::vector<uint8_t> voters{m_myId};
        for (auto& [peer, p] : m_players)
            if (p.joined)
                voters.push_back(p.id);
        if (!m_wait.Start(requester, hours, sleep, voters, GetTickCount()))
        {
            LOG_INFO("Wait: %s asked while another vote is open", NameOf(requester).c_str());
            WaitResult no{requester, false, 0, false, kNobody};
            if (from)
                m_net->Send(from, Encode(no), true);
            else if (m_waitEvents.size() < kMaxQueuedItemChanges)
                m_waitEvents.push_back(WaitEvent{WaitEvent::Kind::Refused, requester, 0, false, kNobody});
            return;
        }
        LOG_INFO("Wait: %s asks to %s %u hours", NameOf(requester).c_str(), sleep ? "sleep" : "wait", hours);
        net::WaitVote vote{requester, hours, sleep};
        for (auto& [peer, p] : m_players)
            if (p.joined && p.id != requester)
                m_net->Send(peer, Encode(vote), true);
        if (requester != m_myId && m_waitEvents.size() < kMaxQueuedItemChanges)
            m_waitEvents.push_back(WaitEvent{WaitEvent::Kind::Asked, requester, hours, sleep, kNobody});
        CheckWaitVote(GetTickCount());
    }

    void Coop::CheckWaitVote(uint32_t now)
    {
        uint8_t requester = m_wait.Requester(), hours = m_wait.Hours();
        bool sleep = m_wait.Sleep();
        switch (m_wait.Check(now))
        {
        case WaitVote::Outcome::Open:
            return;
        case WaitVote::Outcome::Passed:
            LOG_INFO("Wait: everyone agreed; %s %u hours", sleep ? "sleeping" : "waiting", hours);
            SendWaitResult(WaitResult{requester, true, hours, sleep, kNobody});
            return;
        case WaitVote::Outcome::Refused:
            if (m_wait.RefusedBy() != kNobody)
                LOG_INFO("Wait: refused (%s said no)", NameOf(m_wait.RefusedBy()).c_str());
            else
                LOG_INFO("Wait: refused (no answer in time)");
            SendWaitResult(WaitResult{requester, false, 0, sleep, m_wait.RefusedBy()});
            return;
        }
    }

    void Coop::SendWaitResult(const WaitResult& m)
    {
        SendToJoined(Encode(m), true);
        if (m_waitEvents.size() < kMaxQueuedItemChanges)
            m_waitEvents.push_back(WaitEvent{m.passed ? WaitEvent::Kind::Start : WaitEvent::Kind::Refused, m.requester,
                m.hours, m.sleep, m.refusedBy});
    }

    void Coop::SendTalk(PeerId to)
    {
        TalkState m{m_talk.Talker(), m_talk.Busy(), m_talk.Npc()};
        if (to)
            m_net->Send(to, Encode(m), true);
        else
            SendToJoined(Encode(m), true);
    }

    void Coop::SendDialogueLine(const DialogueLine& line)
    {
        if (m_role == Role::Friend && InGame())
            m_net->Send(1, Encode(line), true);
    }

    void Coop::ShareSubtitle(Subtitle m)
    {
        if (!InGame() || !IsSharedRef(m.speaker) || (!m.stop && m.text.empty()))
            return;
        m.playerId = m_myId;
        for (char& c : m.text)
            if (static_cast<unsigned char>(c) < 32)
                c = ' ';
        if (m.text.size() > kMaxSubtitleLength)
            m.text.resize(kMaxSubtitleLength);
        if (m_role == Role::Host)
            SendToJoined(Encode(m), true);
        else
            m_net->Send(1, Encode(m), true);
    }
}
