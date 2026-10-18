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
        };
        ENetHost* host = nullptr;
        uint16_t port = 0;
        std::map<uint8_t, Player> players;
        uint32_t nextTick = 0;

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
                std::string name = r.str();
                if (!r.ok() || pl)
                    return;
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
                if (r.ok() && (!pl->hasCar || int32_t(c.seq - pl->car.seq) > 0))
                {
                    pl->car = c;
                    pl->hasCar = true;
                }
            }
            else if (t == Msg::Chat)
            {
                std::string text = r.str();
                if (!r.ok()) return;
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
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
                m->OnDisconnect(ev.peer);
        }
        uint32_t now = NowMs();
        if (int32_t(now - m->nextTick) >= 0)
        {
            m->Tick();
            m->nextTick = now + kTickMs;
        }
    }
    int Server::PlayerCount() const { return int(m->players.size()); }
    uint16_t Server::Port() const { return m->port; }
}
