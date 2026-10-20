#include "mp_server.h"
#include <enet/enet.h>
#include <chrono>
#include <cstdio>
#include <map>

namespace mp
{
    namespace
    {
        uint32_t NowMs()
        {
            using namespace std::chrono;
            return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }
        constexpr uint32_t kTickMs = 33;
    }

    struct Server::Impl
    {
        struct Player
        {
            uint8_t id = 0;
            std::string name;
            ENetPeer* peer = nullptr;
            CarState car;
            bool hasCar = false;
            uint32_t windowStart = 0, inWindow = 0;
            uint64_t carKey = 0;
        };
        ENetHost* host = nullptr;
        uint16_t port = 0;
        std::map<uint8_t, Player> players;
        std::map<ENetPeer*, uint32_t> pending;   // connected, no Hello yet (when it connected)
        uint32_t nextTick = 0, nextSweep = 0;

        void Send(ENetPeer* p, int ch, const Writer& w, bool reliable)
        {
            enet_peer_send(p, ch, enet_packet_create(w.b.data(), w.b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED));
        }
        void Broadcast(int ch, const Writer& w, bool reliable, uint8_t except = 0xFF)
        {
            for (auto& [id, pl] : players)
                if (id != except)
                    Send(pl.peer, ch, w, reliable);
        }
        Player* FindByPeer(ENetPeer* p)
        {
            for (auto& [id, pl] : players)
                if (pl.peer == p)
                    return &pl;
            return nullptr;
        }
        void OnReceive(ENetPeer* peer, const uint8_t* data, size_t n)
        {
            Reader r(data, n);
            Msg t = r.type();
            Player* pl = FindByPeer(peer);
            if (t == Msg::Hello)
            {
                uint32_t ver = r.u32();
                std::string name = SanitizeName(r.str());
                if (!r.ok() || pl)
                    return;
                pending.erase(peer);
                if (ver != kProtocolVersion)
                {
                    Writer w(Msg::Reject); w.u8(kRejectVersion);
                    Send(peer, kChanEvent, w, true);
                    enet_peer_disconnect_later(peer, 0);
                    return;
                }
                uint8_t id = 1;
                while (players.count(id) && id < 255) ++id;
                if (players.size() >= size_t(kMaxPlayers))
                {
                    Writer w(Msg::Reject); w.u8(kRejectFull);
                    Send(peer, kChanEvent, w, true);
                    enet_peer_disconnect_later(peer, 0);
                    return;
                }
                Player np; np.id = id; np.name = name; np.peer = peer;
                Writer w(Msg::Welcome); w.u8(id); w.u8(uint8_t(1000 / kTickMs));
                Send(peer, kChanEvent, w, true);
                for (auto& [oid, op] : players)   // tell the newcomer who is here
                {
                    Writer j(Msg::PlayerJoin); j.u8(oid); j.str(op.name);
                    Send(peer, kChanEvent, j, true);
                    if (op.carKey)
                    {
                        Writer inf(Msg::PlayerInfo); inf.u8(oid); inf.u32(uint32_t(op.carKey >> 32)); inf.u32(uint32_t(op.carKey));
                        Send(peer, kChanEvent, inf, true);
                    }
                }
                Writer j(Msg::PlayerJoin); j.u8(id); j.str(name);
                Broadcast(kChanEvent, j, true);
                players[id] = np;
                fprintf(stderr, "[mp-server] %s joined as %u (%zu players)\n", name.c_str(), id, players.size());
            }
            else if (!pl)
                return;
            else if (t == Msg::CarState)
            {
                CarState c = r.car();
                // At most 120 states a second from one player (a game sends 30), and only plausible ones.
                const uint32_t now = NowMs();
                if (now - pl->windowStart >= 1000) { pl->windowStart = now; pl->inWindow = 0; }
                if (++pl->inWindow > 120 || !Sane(c))
                    return;
                if (r.ok() && (!pl->hasCar || int32_t(c.seq - pl->car.seq) > 0))
                {
                    pl->car = c;
                    pl->hasCar = true;
                }
            }
            else if (t == Msg::PlayerInfo)
            {
                const uint32_t hi = r.u32(), lo = r.u32();
                if (!r.ok()) return;
                pl->carKey = (uint64_t(hi) << 32) | lo;
                Writer w(Msg::PlayerInfo); w.u8(pl->id); w.u32(hi); w.u32(lo);
                Broadcast(kChanEvent, w, true, pl->id);
            }
            else if (t == Msg::Chat)
            {
                std::string text = r.str();
                if (!r.ok()) return;
                for (char& ch : text) if (ch < 32 || ch >= 127) ch = '?';
                Writer w(Msg::Chat); w.u8(pl->id); w.str(text);
                Broadcast(kChanEvent, w, true);
            }
        }
        void OnDisconnect(ENetPeer* peer)
        {
            Player* pl = FindByPeer(peer);
            if (!pl) return;
            uint8_t id = pl->id;
            fprintf(stderr, "[mp-server] %s (%u) left\n", pl->name.c_str(), id);
            players.erase(id);
            Writer w(Msg::PlayerLeave); w.u8(id);
            Broadcast(kChanEvent, w, true);
        }
        void Tick()
        {
            for (auto& [id, pl] : players)
            {
                Writer w(Msg::Snapshot);
                uint8_t count = 0;
                size_t countAt = w.b.size();
                w.u8(0);
                for (auto& [oid, op] : players)
                {
                    if (oid == id || !op.hasCar) continue;
                    w.u8(oid);
                    w.car(op.car);
                    ++count;
                }
                if (count)
                {
                    w.b[countAt] = count;
                    Send(pl.peer, kChanState, w, false);
                }
            }
        }
    };

    Server::Server() : m(new Impl) {}
    Server::~Server()
    {
        if (m->host)
            enet_host_destroy(m->host);
    }
    bool Server::Start(uint16_t port, int maxPlayers)
    {
        if (enet_initialize() != 0) return false;
        ENetAddress a{};
        a.host = ENET_HOST_ANY;
        a.port = port;
        m->host = enet_host_create(&a, size_t(maxPlayers) + 4, kChannels, 0, 0);
        m->port = port;
        m->nextTick = NowMs();
        return m->host != nullptr;
    }
    void Server::Service(int timeoutMs)
    {
        ENetEvent ev;
        int wait = timeoutMs;
        while (enet_host_service(m->host, &ev, wait) > 0)
        {
            wait = 0;
            if (ev.type == ENET_EVENT_TYPE_RECEIVE)
            {
                m->OnReceive(ev.peer, ev.packet->data, ev.packet->dataLength);
                enet_packet_destroy(ev.packet);
            }
            else if (ev.type == ENET_EVENT_TYPE_CONNECT)
                m->pending[ev.peer] = NowMs();
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                m->pending.erase(ev.peer);
                m->OnDisconnect(ev.peer);
            }
        }
        uint32_t now = NowMs();
        if (int32_t(now - m->nextSweep) >= 0)
        {
            // A connection that has not said Hello within five seconds is dropped (it holds a slot and a socket for nothing).
            m->nextSweep = now + 1000;
            for (auto it = m->pending.begin(); it != m->pending.end();)
            {
                if (now - it->second > 5000)
                {
                    enet_peer_disconnect_now(it->first, 0);
                    it = m->pending.erase(it);
                }
                else
                    ++it;
            }
        }
        if (int32_t(now - m->nextTick) >= 0)
        {
            m->Tick();
            m->nextTick = now + kTickMs;
        }
    }
    int Server::PlayerCount() const { return int(m->players.size()); }
    uint16_t Server::Port() const { return m->port; }
}
