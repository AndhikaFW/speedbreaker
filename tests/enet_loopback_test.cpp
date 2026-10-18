// The multiplayer transport (vendored ENet, runtime/thirdparty/enet): a server and a client on loopback,
// one reliable message from each side and a burst of unreliable ones. Built by tests/CMakeLists.txt
// (links the "enet" target); needs no game files and no network beyond 127.0.0.1.
#include <enet/enet.h>
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    assert(enet_initialize() == 0);

    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = 47123;
    ENetHost* server = enet_host_create(&addr, 8, 2, 0, 0);
    ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0);
    assert(server && client);

    ENetAddress to{};
    enet_address_set_host(&to, "127.0.0.1");
    to.port = 47123;
    ENetPeer* peer = enet_host_connect(client, &to, 2, 0);
    assert(peer);

    ENetPeer* clientOnServer = nullptr;
    bool connected = false, gotHello = false, gotReply = false;
    int unreliable = 0;

    for (int i = 0; i < 400 && !gotReply; ++i)
    {
        ENetEvent ev;
        while (enet_host_service(server, &ev, 0) > 0)
        {
            if (ev.type == ENET_EVENT_TYPE_CONNECT)
            {
                clientOnServer = ev.peer;
                ENetPacket* p = enet_packet_create("welcome", 8, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(ev.peer, 0, p);
                for (int k = 0; k < 10; ++k)
                {
                    char buf[8];
                    snprintf(buf, sizeof buf, "s%d", k);
                    enet_peer_send(ev.peer, 1, enet_packet_create(buf, strlen(buf) + 1, 0));
                }
            }
            else if (ev.type == ENET_EVENT_TYPE_RECEIVE)
            {
                gotReply = !strcmp((char*)ev.packet->data, "thanks");
                enet_packet_destroy(ev.packet);
            }
        }
        while (enet_host_service(client, &ev, 1) > 0)
        {
            if (ev.type == ENET_EVENT_TYPE_CONNECT)
                connected = true;
            else if (ev.type == ENET_EVENT_TYPE_RECEIVE)
            {
                if (ev.channelID == 0 && !strcmp((char*)ev.packet->data, "welcome"))
                {
                    gotHello = true;
                    enet_peer_send(peer, 0, enet_packet_create("thanks", 7, ENET_PACKET_FLAG_RELIABLE));
                }
                else if (ev.channelID == 1)
                    ++unreliable;
                enet_packet_destroy(ev.packet);
            }
        }
    }

    assert(connected && clientOnServer && gotHello && gotReply);
    assert(unreliable > 0);   // loopback drops nothing, but unreliable is allowed to
    printf("enet loopback ok: %d unreliable of 10\n", unreliable);

    enet_host_destroy(client);
    enet_host_destroy(server);
    enet_deinitialize();
    return 0;
}
