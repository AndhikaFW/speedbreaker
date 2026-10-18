// Wire format of the multiplayer layer (net/mp_*). Little-endian, fixed layouts, no pointers: every message is a
// one-byte type followed by its body. Unreliable messages go on channel 0 (car states, snapshots), reliable ones on
// channel 1 (join, leave, chat, events). Bump kProtocolVersion on any change.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mp
{
    constexpr uint16_t kDefaultPort = 27960;
    constexpr uint32_t kProtocolVersion = 1;
    constexpr int kMaxPlayers = 16;
    constexpr int kChanState = 0;   // unreliable, latest wins
    constexpr int kChanEvent = 1;   // reliable, ordered
    constexpr int kChannels = 2;

    enum class Msg : uint8_t
    {
        Hello = 1,      // C->S reliable: version, name
        Welcome,        // S->C reliable: your id, server tick rate
        Reject,         // S->C reliable: reason code
        PlayerJoin,     // S->C reliable: id, name
        PlayerLeave,    // S->C reliable: id
        CarState,       // C->S unreliable: the sender's car
        Snapshot,       // S->C unreliable: every other player's latest car, one message per server tick
        Chat,           // both ways reliable
    };

    enum RejectReason : uint8_t { kRejectVersion = 1, kRejectFull = 2 };

    // One car's physical state as the game's own physics produced it on its owner's machine.
    struct CarState
    {
        uint32_t seq = 0;          // owner's counter, newer wins
        uint32_t ownerTimeMs = 0;  // owner's clock (for interpolation delay)
        float pos[3] = {};
        float rot[4] = {0, 0, 0, 1};   // quaternion x y z w
        float vel[3] = {};
        float angVel[3] = {};
        float steer = 0;           // -1..1
        float throttle = 0;        // 0..1
        float brake = 0;           // 0..1
        uint16_t carId = 0;        // which car model (game's own id), so the other side can spawn it
        uint16_t flags = 0;        // bit 0: handbrake, bit 1: nitrous, bit 2: horn
    };

    // Writer/reader over a byte vector.
    class Writer
    {
    public:
        explicit Writer(Msg t) { u8(uint8_t(t)); }
        void u8(uint8_t v) { b.push_back(v); }
        void u16(uint16_t v) { raw(&v, 2); }
        void u32(uint32_t v) { raw(&v, 4); }
        void f32(float v) { raw(&v, 4); }
        void str(const std::string& s)
        {
            u8(uint8_t(s.size() > 31 ? 31 : s.size()));
            b.insert(b.end(), s.begin(), s.begin() + (s.size() > 31 ? 31 : s.size()));
        }
        void car(const CarState& c)
        {
            u32(c.seq); u32(c.ownerTimeMs);
            for (float f : c.pos) f32(f);
            for (float f : c.rot) f32(f);
            for (float f : c.vel) f32(f);
            for (float f : c.angVel) f32(f);
            f32(c.steer); f32(c.throttle); f32(c.brake);
            u16(c.carId); u16(c.flags);
        }
        std::vector<uint8_t> b;
    private:
        void raw(const void* p, size_t n) { const uint8_t* s = (const uint8_t*)p; b.insert(b.end(), s, s + n); }
    };

    class Reader
    {
    public:
        Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
        bool ok() const { return !bad_; }
        Msg type() { return Msg(u8()); }
        uint8_t u8() { uint8_t v = 0; raw(&v, 1); return v; }
        uint16_t u16() { uint16_t v = 0; raw(&v, 2); return v; }
        uint32_t u32() { uint32_t v = 0; raw(&v, 4); return v; }
        float f32() { float v = 0; raw(&v, 4); return v; }
        std::string str()
        {
            size_t l = u8();
            if (l > n_ - p_ ) { bad_ = true; return {}; }
            std::string s((const char*)d_ + p_, l);
            p_ += l;
            return s;
        }
        CarState car()
        {
            CarState c;
            c.seq = u32(); c.ownerTimeMs = u32();
            for (float& f : c.pos) f = f32();
            for (float& f : c.rot) f = f32();
            for (float& f : c.vel) f = f32();
            for (float& f : c.angVel) f = f32();
            c.steer = f32(); c.throttle = f32(); c.brake = f32();
            c.carId = u16(); c.flags = u16();
            return c;
        }
    private:
        void raw(void* o, size_t n)
        {
            if (n > n_ - p_) { bad_ = true; return; }
            memcpy(o, d_ + p_, n);
            p_ += n;
        }
        const uint8_t* d_;
        size_t n_, p_ = 0;
        bool bad_ = false;
    };
}
