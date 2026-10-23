// The open world race rules (net/mp_race.cpp): opening by stopping in the start marker, joining during the window, the countdown, checkpoints and laps from reported positions,
// places and finish times, a player who is not in the race, and a teleport that does not pass anything. No game files.
#include "mp_race.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace mp;

// A driver: moves along the square at `speed` m/s, reporting every 33 ms.
struct Driver
{
    uint8_t id;
    Vec3 pos;
    float speed;
    size_t target = 1;
};

int main()
{
    RaceManager rm;
    RaceDef d;
    d.name = "square"; d.laps = 2; d.radius = 20.f;
    d.points = { { 0, 0, 0 }, { 200, 0, 0 }, { 200, 0, 200 }, { 0, 0, 200 } };
    rm.SetRaces({ d });
    rm.gatherMs = 3000; rm.countdownMs = 2000; rm.finishedHoldMs = 4000; rm.holdStillMs = 500;

    uint32_t t = 1000;
    auto step = [&](uint32_t ms) { t += ms; rm.Tick(t); };

    // Nothing happens while a car passes the start marker without stopping, or someone is outside it.
    for (int i = 0; i < 30; ++i) { rm.Report(9, { float(i) * 2, 0, 0 }, 40.f, t); step(33); }
    assert(rm.Phase() == RacePhase::Idle);

    // A stops in the start marker for half a second: the race opens (gathering).
    for (int i = 0; i < 30; ++i) { rm.Report(1, { 5, 0, 5 }, 0.f, t); step(33); }
    assert(rm.Phase() == RacePhase::Gathering && rm.View(t).racers.size() == 1);
    // B joins 1 s later the same way; C (9) drives about and is not part of it.
    for (int i = 0; i < 30; ++i) { rm.Report(1, { 5, 0, 5 }, 0.f, t); rm.Report(2, { -5, 0, 3 }, 0.f, t); rm.Report(9, { 500.f + i, 0, 500 }, 20.f, t); step(33); }
    assert(rm.View(t).racers.size() == 2);
    // The window closes, the countdown runs, then the race.
    while (rm.Phase() == RacePhase::Gathering) { rm.Report(1, { 5, 0, 5 }, 0.f, t); rm.Report(2, { -5, 0, 3 }, 0.f, t); step(33); }
    assert(rm.Phase() == RacePhase::Countdown);
    // Somebody who stops in the marker now cannot join.
    for (int i = 0; i < 30; ++i) { rm.Report(3, { 0, 0, -4 }, 0.f, t); step(33); }
    assert(rm.View(t).racers.size() == 2);
    while (rm.Phase() == RacePhase::Countdown) { rm.Report(1, { 5, 0, 5 }, 0.f, t); rm.Report(2, { -5, 0, 3 }, 0.f, t); step(33); }
    assert(rm.Phase() == RacePhase::Running);

    // Drive: A at 50 m/s, B at 40 m/s, round the square twice; the checkpoints are passed in order. C drives through all of them: nothing.
    const Vec3 corners[4] = { { 0, 0, 0 }, { 200, 0, 0 }, { 200, 0, 200 }, { 0, 0, 200 } };
    Driver a{ 1, { 5, 0, 5 }, 50.f }, b{ 2, { -5, 0, 3 }, 40.f }, c{ 9, { 0, 0, 0 }, 60.f };
    auto advance = [&](Driver& dr, float dt) {
        const Vec3 tg = corners[dr.target % 4];
        float dx = tg.x - dr.pos.x, dz = tg.z - dr.pos.z, len = std::sqrt(dx * dx + dz * dz);
        const float move = dr.speed * dt;
        if (len <= move) { dr.pos = tg; ++dr.target; }
        else { dr.pos.x += dx / len * move; dr.pos.z += dz / len * move; }
    };
    bool lapSeen = false;
    for (int i = 0; i < 6000 && rm.Phase() == RacePhase::Running; ++i)
    {
        advance(a, 0.033f); advance(b, 0.033f); advance(c, 0.033f);
        rm.Report(1, a.pos, a.speed, t); rm.Report(2, b.pos, b.speed, t); rm.Report(9, c.pos, c.speed, t);
        step(33);
        const RaceView v = rm.View(t);
        if (v.racers.size() == 2 && v.racers[0].lap == 1) lapSeen = true;
        // A is ahead of B throughout.
        if (v.racers.size() == 2 && i > 50 && !v.racers[0].finished) assert(v.racers[0].id == 1);
    }
    assert(lapSeen);
    const RaceView done = rm.View(t);
    assert(done.phase == RacePhase::Finished && done.racers.size() == 2);
    assert(done.racers[0].id == 1 && done.racers[0].place == 1 && done.racers[0].finished);
    assert(done.racers[1].id == 2 && done.racers[1].place == 2 && done.racers[1].finished);
    // 2 laps of 800 m: 1600 m at 50 and 40 m/s, about 32 s and 40 s (the start line is already under the car at the beginning of the first lap).
    printf("A %.1f s, B %.1f s\n", done.racers[0].finishMs / 1000.0, done.racers[1].finishMs / 1000.0);
    assert(done.racers[0].finishMs > 28000 && done.racers[0].finishMs < 36000);
    assert(done.racers[1].finishMs > 36000 && done.racers[1].finishMs < 46000);

    // After the hold it is idle again; a teleport of 500 m does not pass a checkpoint.
    for (int i = 0; i < 200; ++i) { rm.Report(1, { 5, 0, 5 }, 0.f, t); step(33); }
    assert(rm.Phase() == RacePhase::Gathering || rm.Phase() == RacePhase::Idle);
    RaceManager rm2;
    rm2.SetRaces({ d }); rm2.gatherMs = 500; rm2.countdownMs = 500; rm2.holdStillMs = 200;
    uint32_t t2 = 0;
    for (int i = 0; i < 20; ++i) { rm2.Report(1, { 0, 0, 0 }, 0, t2); t2 += 33; rm2.Tick(t2); }
    while (rm2.Phase() != RacePhase::Running) { rm2.Report(1, { 0, 0, 0 }, 0, t2); t2 += 33; rm2.Tick(t2); }
    rm2.Report(1, { 0, 0, 0 }, 0, t2);
    rm2.Report(1, { 500, 0, 0 }, 50, t2 + 33);   // 500 m in one report (past the first checkpoint): a jump, ignored
    t2 += 66; rm2.Tick(t2);
    assert(rm2.View(t2).racers.size() == 1 && rm2.View(t2).racers[0].next == 1);

    // The file format.
    FILE* f = fopen("/tmp/mp_race_test_races.txt", "w");
    fputs("# a test\nrace loop laps=3 radius=30\npoint 0 0 0\npoint 100 0 0 # a comment\npoint 100 0 100\nrace broken\npoint 1 2 3\n", f);
    fclose(f);
    std::vector<RaceDef> loaded;
    assert(LoadRaces("/tmp/mp_race_test_races.txt", loaded));
    assert(loaded.size() == 1 && loaded[0].name == "loop" && loaded[0].laps == 3 && loaded[0].radius == 30.f && loaded[0].points.size() == 3);
    puts("mp race test ok");
    return 0;
}
