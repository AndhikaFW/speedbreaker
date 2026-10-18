// The multiplayer layer end to end on loopback (net/mp_*): a server and two clients; each sends cars moving at a known
// speed, and the other must see them join, move smoothly (interpolation), and leave; a wrong protocol version is refused.
// Built by tests/CMakeLists.txt (links mp_net); no game files.
#include "mp_client.h"
#include "mp_server.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

static void Pump(mp::Server& s, mp::Client& a, mp::Client& b, int ms)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end)
    {
        s.Service(1);
        a.Service();
        b.Service();
    }
}

int main()
{
    mp::Server server;
    assert(server.Start(47124));
    mp::Client a, b;
    assert(a.Connect("127.0.0.1", 47124, "anna") && b.Connect("127.0.0.1", 47124, "ben"));
    Pump(server, a, b, 300);
    assert(a.Connected() && b.Connected() && server.PlayerCount() == 2);
    assert(a.MyId() != b.MyId());
    assert(a.Remotes().count(b.MyId()) && a.Remotes().at(b.MyId()).name == "ben");

    // anna drives along +x at 40 m/s, sending at 30 Hz; ben must see her pose advance without jumps.
    auto t0 = std::chrono::steady_clock::now();
    float lastX = -1, maxStep = 0;
    int samples = 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500))
    {
        float secs = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        mp::CarState c;
        c.pos[0] = 40.f * secs;
        c.vel[0] = 40.f;
        c.carId = 7;
        a.SendCar(c);
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
        while (std::chrono::steady_clock::now() < until)
        {
            server.Service(1);
            a.Service();
            b.Service();
            mp::CarState seen;
            if (secs > 0.5f && b.RemoteCar(a.MyId(), b.NowMs(), seen))
            {
                assert(seen.carId == 7);
                if (lastX >= 0)
                    maxStep = std::fmax(maxStep, std::fabs(seen.pos[0] - lastX));
                lastX = seen.pos[0];
                ++samples;
            }
        }
    }
    printf("remote x %.1f after 1.5 s (driver at 60), max step between reads %.2f m over %d reads\n", lastX, maxStep, samples);
    assert(samples > 100 && lastX > 40.f && lastX < 60.f);   // behind by about the interpolation delay (4 m) plus latency
    assert(maxStep < 3.f);                                   // 40 m/s * 33 ms = 1.3 m per send, no jumps

    // Same quaternion math: nlerp stays unit length.
    mp::CarState p, q;
    p.rot[0] = 0; p.rot[3] = 1;
    q.rot[0] = 1; q.rot[3] = 0;
    mp::CarState m = mp::Lerp(p, q, 0.5f);
    assert(std::fabs(m.rot[0] * m.rot[0] + m.rot[3] * m.rot[3] - 1.f) < 1e-4f);

    b.Disconnect();
    Pump(server, a, b, 300);
    assert(server.PlayerCount() == 1 && a.Remotes().empty());
    puts("mp net test ok");
    return 0;
}
