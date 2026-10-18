// The multiplayer client (net/mp_client.cpp): connects to an mp::Server, sends the local car's state and keeps, for
// every other player, a short history from which RemoteCar() gives the pose to draw now (interpolated, then dead
// reckoned when packets are late). No game code in here; game/mp_adapter ties it to the game's vehicles.
#pragma once
#include "mp_proto.h"
#include <functional>
#include <map>
#include <memory>

namespace mp
{
    struct Remote
    {
        uint8_t id = 0;
        std::string name;
        bool hasCar = false;
        CarState latest;   // newest state received
    };

    class Client
    {
    public:
        Client();
        ~Client();
        bool Connect(const std::string& host, uint16_t port, const std::string& name);
        void Disconnect();
        // Pump the network (non-blocking). Call every frame.
        void Service();
        bool Connected() const;     // Welcome received
        bool Failed() const;        // refused or lost
        uint8_t MyId() const;
        void SendCar(const CarState& c);   // unreliable; call at the send rate (about 30 Hz)
        void SendChat(const std::string& text);
        const std::map<uint8_t, Remote>& Remotes() const;
        // The pose of a remote car for the local clock `nowMs` (ms since the client started, see NowMs()), delayed by
        // the interpolation delay and extrapolated by velocity if the data is late. False if nothing was received.
        bool RemoteCar(uint8_t id, uint32_t nowMs, CarState& out) const;
        uint32_t NowMs() const;
        uint32_t RttMs() const;
        std::function<void(uint8_t, const std::string&)> onChat;
    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };

    // Blend two states (position, velocity, steering linearly, rotation by slerp); t in 0..1.
    CarState Lerp(const CarState& a, const CarState& b, float t);
}
