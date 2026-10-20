// The multiplayer layer end to end on loopback (net/mp_*): a server and two clients; each sends cars moving at a known
// speed, and the other must see them join, move smoothly (interpolation), and leave; a wrong protocol version is refused.
// Built by tests/CMakeLists.txt (links mp_net); no game files.
#include "mp_client.h"
#include "mp_math.h"
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

    // A player that stops sending is given up on after two seconds (the stand-in goes back to the game's own driver).
    {
        mp::CarState seen;
        assert(b.RemoteCar(a.MyId(), b.NowMs(), seen));
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2300);
        while (std::chrono::steady_clock::now() < until) { server.Service(1); a.Service(); b.Service(); }
        assert(!b.RemoteCar(a.MyId(), b.NowMs(), seen));
    }

    // Orientation: a matrix goes to a quaternion and back unchanged, for rotations about every axis and mixes of them.
    for (int k = 0; k < 500; ++k)
    {
        const float yaw = 0.0137f * float(k) * 6.f, pitch = 0.9f * std::sin(float(k)), roll = 0.7f * std::cos(float(k) * 1.3f);
        const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
        // rows are the body's axes: R = Rz(yaw) * Rx(pitch) * Ry(roll) applied as rows
        float rz[3][3] = {{cy, sy, 0}, {-sy, cy, 0}, {0, 0, 1}};
        float rx[3][3] = {{1, 0, 0}, {0, cp, sp}, {0, -sp, cp}};
        float ry[3][3] = {{cr, 0, -sr}, {0, 1, 0}, {sr, 0, cr}};
        float t[3][3], m[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { t[i][j] = 0; for (int l = 0; l < 3; ++l) t[i][j] += rz[i][l] * rx[l][j]; }
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { m[i][j] = 0; for (int l = 0; l < 3; ++l) m[i][j] += t[i][l] * ry[l][j]; }
        float q[4], back[3][3];
        mp::MatToQuat(m, q);
        mp::QuatToMat(q, back);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) assert(std::fabs(back[i][j] - m[i][j]) < 1e-4f);
    }

    // The car a player races in is announced to the others, also to one that joins later, and cleared again.
    {
        a.SetLobby(0x0145D39E6027E317ull, true);
        Pump(server, a, b, 300);
        assert(b.Remotes().at(a.MyId()).carKey == 0x0145D39E6027E317ull && b.Remotes().at(a.MyId()).lobbyReady);
        mp::Client late;
        assert(late.Connect("127.0.0.1", 47124, "cora"));
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        while (std::chrono::steady_clock::now() < until) { server.Service(1); a.Service(); b.Service(); late.Service(); }
        assert(late.Connected() && late.Remotes().count(a.MyId()) && late.Remotes().at(a.MyId()).carKey == 0x0145D39E6027E317ull);
        a.SetLobby(0, false);
        until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        while (std::chrono::steady_clock::now() < until) { server.Service(1); a.Service(); b.Service(); late.Service(); }
        assert(b.Remotes().at(a.MyId()).carKey == 0 && !b.Remotes().at(a.MyId()).lobbyReady && late.Remotes().at(a.MyId()).carKey == 0);
        late.Disconnect();
        Pump(server, a, b, 200);
    }

    // Hostile or damaged states are not accepted anywhere.
    {
        mp::CarState good;
        assert(mp::Sane(good));
        mp::CarState bad = good; bad.pos[1] = std::nanf("");  assert(!mp::Sane(bad));
        bad = good; bad.vel[0] = INFINITY;                   assert(!mp::Sane(bad));
        bad = good; bad.pos[2] = 1.0e9f;                     assert(!mp::Sane(bad));
        bad = good; bad.rot[3] = 0; /* quaternion of length 0 */ assert(!mp::Sane(bad));
        bad = good; bad.angVel[1] = 1000.f;                  assert(!mp::Sane(bad));
        assert(mp::SanitizeName("a\x01" "b\xff") == "a?b?" && mp::SanitizeName("") == "player" && mp::SanitizeName(std::string(60, 'x')).size() == 24);
        // A client that sends garbage is not relayed: ben's view of anna stays at her last sane state.
        mp::CarState s1; s1.pos[0] = 11.f; a.SendCar(s1);
        Pump(server, a, b, 300);
        mp::CarState seen;
        assert(b.RemoteCar(a.MyId(), b.NowMs(), seen));
        mp::CarState nan = good; nan.pos[0] = std::nanf(""); a.SendCar(nan);
        Pump(server, a, b, 300);
        assert(b.RemoteCar(a.MyId(), b.NowMs(), seen) && std::isfinite(seen.pos[0]));
    }

    b.Disconnect();
    Pump(server, a, b, 300);
    assert(server.PlayerCount() == 1 && a.Remotes().empty());
    puts("mp net test ok");
    return 0;
}
