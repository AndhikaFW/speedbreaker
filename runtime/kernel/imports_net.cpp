// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The console's network stack (xnet / winsock, the NetDll_* imports and XamAlloc) on the host's sockets, so the game's own online code runs: the game talks to
// peers over UDP and TCP with its own protocol (docs/MULTIPLAYER.md), and this layer only carries the packets.
//
// Addresses: the console's "secure" addresses are plain host addresses here. XNetGetTitleXnAddr answers with this machine's address (NFSMW_NET_ADDR=<ip>
// picks it, else the address of the interface that reaches 8.8.8.8, else 127.0.0.1) and the UDP port the game bound, and XNetXnAddrToInAddr gives back the
// address in the XNADDR, so a peer's XNADDR (sent by the peer to the game over the lobby) leads straight to its real address.
//
// Everything the game asks is logged once with NFSMW_NET_LOG=1.
#include <stdafx.h>
#include "dispatcher.h"
#include "function.h"
#include "memory.h"
#include "xam.h"
#include <cpu/ppc_context.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <format>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

using namespace dispatcher;

uint32_t ExAllocatePoolWithTag(uint32_t size, uint32_t tag);
void ExFreePool(uint32_t address);
uint32_t NtClose(uint32_t handle);

namespace
{
    const bool s_log = std::getenv("NFSMW_NET_LOG") != nullptr;
#define NETLOG(...) do { if (s_log) { fprintf(stderr, "[net] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

    constexpr uint32_t INVALID_SOCKET_ = 0xFFFFFFFF;
    constexpr int SOCKET_ERROR_ = -1;
    // Winsock error numbers the game may test for.
    constexpr uint32_t WSAEWOULDBLOCK = 10035, WSAEINPROGRESS = 10036, WSAENOTSOCK = 10038, WSAEINVAL = 10022, WSAECONNRESET = 10054,
                       WSAECONNREFUSED = 10061, WSAEADDRINUSE = 10048, WSAETIMEDOUT = 10060, WSAENOTCONN = 10057, WSAEMSGSIZE = 10040,
                       WSAEOPNOTSUPP = 10045, WSAEFAULT = 10014, WSAEAFNOSUPPORT = 10047, WSAENETUNREACH = 10051, WSAEHOSTUNREACH = 10065;

    // NFSMW_NET_LOG=1: the guest call chain of the importing function (its return address, then each caller's), to see which game code makes a network call.
    void LogChain(const char* what)
    {
        if (!s_log)
            return;
        PPCContext* ctx = GetPPCContext();
        if (!ctx)
            return;
        uint8_t* base = g_memory.base;
        std::string chain = std::format("{:08X}", uint32_t(ctx->lr));
        uint32_t frame = ctx->r1.u32;
        for (int depth = 0; depth < 12 && frame; depth++)
        {
            const uint32_t older = PPC_LOAD_U32(frame);
            if (older <= frame || older - frame > 0x100000)
                break;
            chain += std::format(" {:08X}", PPC_LOAD_U32(older - 8));
            frame = older;
        }
        fprintf(stderr, "[net]   chain of %s: %s\n", what, chain.c_str());
    }

    thread_local uint32_t t_lastError = 0;

    uint32_t FromErrno(int e)
    {
        switch (e)
        {
        case EWOULDBLOCK: return WSAEWOULDBLOCK;
        case EINPROGRESS: return WSAEINPROGRESS;
        case EBADF: case ENOTSOCK: return WSAENOTSOCK;
        case EINVAL: return WSAEINVAL;
        case ECONNRESET: return WSAECONNRESET;
        case ECONNREFUSED: return WSAECONNREFUSED;
        case EADDRINUSE: return WSAEADDRINUSE;
        case ETIMEDOUT: return WSAETIMEDOUT;
        case ENOTCONN: return WSAENOTCONN;
        case EMSGSIZE: return WSAEMSGSIZE;
        case EOPNOTSUPP: return WSAEOPNOTSUPP;
        case EFAULT: return WSAEFAULT;
        case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
        case ENETUNREACH: return WSAENETUNREACH;
        case EHOSTUNREACH: return WSAEHOSTUNREACH;
        default: return e;
        }
    }

    int Fail(int e = -1)
    {
        t_lastError = FromErrno(e < 0 ? errno : e);
        return SOCKET_ERROR_;
    }

    // ---- sockets: the guest's handle is a small number, the host's is a file descriptor
    struct Sock
    {
        int fd = -1;
        int type = 0;           // SOCK_STREAM or SOCK_DGRAM
        bool nonblocking = false;
        bool listening = false;
        uint32_t eventHandle = 0;  // WSAEventSelect: the guest event to set
        uint32_t eventMask = 0;
    };
    std::mutex s_socketsMutex;
    std::map<uint32_t, Sock> s_sockets;
    uint32_t s_nextSocket = 0x100;

    Sock* Find(uint32_t s)
    {
        auto it = s_sockets.find(s);
        return it == s_sockets.end() ? nullptr : &it->second;
    }

    int HostFd(uint32_t s)
    {
        std::lock_guard lock(s_socketsMutex);
        Sock* sock = Find(s);
        return sock ? sock->fd : -1;
    }

    // A guest sockaddr_in (big-endian family) to the host's.
    bool ToHost(const uint8_t* guest, int length, sockaddr_in& out)
    {
        if (!guest || length < 8)
            return false;
        out = sockaddr_in{};
        out.sin_family = AF_INET;
        memcpy(&out.sin_port, guest + 2, 2);   // network order already, in both
        memcpy(&out.sin_addr, guest + 4, 4);
        return guest[0] == 0 && guest[1] == 2;
    }

    void ToGuest(const sockaddr_in& host, uint8_t* guest)
    {
        memset(guest, 0, 16);
        guest[1] = 2;
        memcpy(guest + 2, &host.sin_port, 2);
        memcpy(guest + 4, &host.sin_addr, 4);
    }

    // The address the game reports for this machine.
    uint32_t LocalAddress()  // as the integer the guest holds: the four bytes read big-endian
    {
        static const uint32_t addr = [] {
            in_addr a{};
            if (const char* v = std::getenv("NFSMW_NET_ADDR"); v && inet_pton(AF_INET, v, &a) == 1)
                return ntohl(a.s_addr);
            // the interface a packet to the outside would leave from (a UDP connect sends nothing)
            int s = socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons(9);
            inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
            uint32_t result = 0x7F000001;
            if (s >= 0 && connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0)
            {
                sockaddr_in me{};
                socklen_t len = sizeof(me);
                if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0)
                    result = ntohl(me.sin_addr.s_addr);
            }
            if (s >= 0)
                close(s);
            return result;
        }();
        return addr;
    }

    // ---- the poller behind WSAEventSelect
    std::once_flag s_pollerOnce;
    void PollerLoop()
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::vector<std::pair<uint32_t, uint32_t>> toSet;  // event handle, mask bits that fired
            {
                std::lock_guard lock(s_socketsMutex);
                std::vector<pollfd> fds;
                std::vector<Sock*> socks;
                for (auto& [id, s] : s_sockets)
                    if (s.eventHandle && s.eventMask)
                    {
                        fds.push_back({ s.fd, short(POLLIN | POLLOUT), 0 });
                        socks.push_back(&s);
                    }
                if (!fds.empty() && poll(fds.data(), nfds_t(fds.size()), 0) > 0)
                    for (size_t i = 0; i < fds.size(); i++)
                    {
                        uint32_t fired = 0;
                        if (fds[i].revents & POLLIN)
                            fired |= socks[i]->listening ? 8u : 1u;  // FD_ACCEPT : FD_READ
                        if (fds[i].revents & POLLOUT)
                            fired |= 2u;  // FD_WRITE
                        if (fds[i].revents & (POLLHUP | POLLERR))
                            fired |= 32u;  // FD_CLOSE
                        if (fired & socks[i]->eventMask)
                            toSet.emplace_back(socks[i]->eventHandle, fired);
                    }
            }
            for (auto& [handle, bits] : toSet)
                if (IsKernelObject(handle))
                    if (auto* e = dynamic_cast<EventObject*>(GetKernelObject(handle)))
                        e->Set();
        }
    }
}

void NetLogChain(const char* what) { LogChain(what); }  // for other import files (NFSMW_NET_LOG=1)

// ---- XamAlloc -------------------------------------------------------------------

uint32_t XamAlloc(uint32_t flags, uint32_t size, be<uint32_t>* out)
{
    const uint32_t address = ExAllocatePoolWithTag(size ? size : 1, 0);
    NETLOG("XamAlloc(flags %08X, size %u) = %08X", flags, size, address);
    if (out)
        *out = address;
    return address ? 0 : 0xC0000017;  // STATUS_NO_MEMORY
}

uint32_t XamFree(uint32_t address)
{
    ExFreePool(address);
    return 0;
}

// ---- xnet -------------------------------------------------------------------------

int NetDll_XNetStartup(uint32_t caller, uint32_t params)
{
    NETLOG("XNetStartup");
    return 0;
}

int NetDll_XNetCleanup(uint32_t caller)
{
    return 0;
}

int NetDll_WSAStartup(uint32_t caller, uint32_t version, uint8_t* data)
{
    NETLOG("WSAStartup(version %04X)", version);
    if (data)
    {
        memset(data, 0, 8 + 257 + 129 + 4 + 4);
        data[0] = uint8_t(version & 0xFF);  // wVersion (big-endian u16 holding a little-endian WORD value: the guest stores it as given)
        data[1] = uint8_t(version >> 8);
        uint16_t v = uint16_t(version);
        memcpy(data, &v, 2);
        memcpy(data + 2, &v, 2);
    }
    return 0;
}

int NetDll_WSACleanup(uint32_t caller)
{
    return 0;
}

int NetDll_WSAGetLastError()
{
    return int(t_lastError);
}

void NetDll_WSASetLastError(uint32_t error)
{
    t_lastError = error;
}

// XNADDR: ina (IN_ADDR, 4), inaOnline (4), wPortOnline (2), abEnet (6), abOnline (20): 36 bytes.
uint32_t NetDll_XNetGetTitleXnAddr(uint32_t caller, uint8_t* xnaddr)
{
    {
        static std::atomic<uint32_t> calls{ 0 };
        const uint32_t n = calls.fetch_add(1) + 1;
        if (n == 10 || n == 100 || n == 1000 || n % 5000 == 0)
            NETLOG("XNetGetTitleXnAddr: %u calls so far", n);
    }
    // NFSMW_FAKE_LIVE=1: the game's NetConn state machine waits for the Live notifications (connection changed, link state) once it has an address.
    if (std::getenv("NFSMW_FAKE_LIVE"))
        XamNotifyLiveConnected();
    {
        static std::atomic<int> n{ 0 };
        if (n.fetch_add(1) < 2)
            LogChain("XNetGetTitleXnAddr");
    }
    if (xnaddr)
    {
        memset(xnaddr, 0, 36);
        const uint32_t ina = LocalAddress();
        xnaddr[0] = uint8_t(ina >> 24); xnaddr[1] = uint8_t(ina >> 16); xnaddr[2] = uint8_t(ina >> 8); xnaddr[3] = uint8_t(ina);
        // a made-up but fixed hardware address derived from the address, so peers can tell machines apart
        xnaddr[10] = 0x00; xnaddr[11] = 0x22; xnaddr[12] = 0x48; xnaddr[13] = xnaddr[1]; xnaddr[14] = xnaddr[2]; xnaddr[15] = xnaddr[3];
        // the online (Xbox Live) address: the same, and a made-up online id
        memcpy(xnaddr + 4, xnaddr, 4);   // inaOnline
        for (int i = 0; i < 20; i++)
            xnaddr[16 + i] = uint8_t(0x40 + i + xnaddr[3]);  // abOnline
    }
    static bool logged = false;
    if (!std::exchange(logged, true))
        NETLOG("XNetGetTitleXnAddr: %u.%u.%u.%u", LocalAddress() >> 24, (LocalAddress() >> 16) & 255, (LocalAddress() >> 8) & 255, LocalAddress() & 255);
    // XNET_GET_XNADDR_ETHERNET 0x02 | STATIC 0x04 | GATEWAY 0x20 | DNS 0x40 | ONLINE 0x80. The game's NetConn state machine (sub_82819018) maps the bits to
    // a status: GATEWAY or DNS present is "connected to an ISP" ('+isp') and lets it go on; with fewer it keeps polling ('~eth', '~dns', '~sta').
    return 0x02 | 0x04 | 0x20 | 0x40 | 0x80;
}

uint32_t NetDll_XNetGetDebugXnAddr(uint32_t caller, uint8_t* xnaddr)
{
    return NetDll_XNetGetTitleXnAddr(caller, xnaddr);
}

uint32_t NetDll_XNetGetEthernetLinkStatus(uint32_t caller)
{
    return 0x03;  // XNET_ETHERNET_LINK_ACTIVE | XNET_ETHERNET_LINK_100MBPS
}

uint32_t NetDll_XNetGetBroadcastVersionStatus(uint32_t caller, uint32_t reset)
{
    return 0;
}

int NetDll_XNetGetOpt(uint32_t caller, uint32_t option, uint8_t* buffer, be<uint32_t>* size)
{
    NETLOG("XNetGetOpt(%u)", option);
    return 0;
}

int NetDll_XNetSetOpt(uint32_t caller, uint32_t option, const uint8_t* buffer, uint32_t size)
{
    NETLOG("XNetSetOpt(%u)", option);
    return 0;
}

uint32_t NetDll_XNetGetConnectStatus(uint32_t caller, uint32_t ina)
{
    return 2;  // XNET_CONNECT_STATUS_CONNECTED
}

int NetDll_XNetConnect(uint32_t caller, uint32_t ina)
{
    LogChain("XNetConnect");
    NETLOG("XNetConnect(%08X)", ina);
    return 0;
}

int NetDll_XNetCreateKey(uint32_t caller, uint8_t* kid, uint8_t* key)
{
    static std::atomic<uint32_t> counter{ 1 };
    if (kid)
    {
        memset(kid, 0, 8);
        kid[0] = 0x80;  // a title-created key id (XNET_XNKID_SYSTEM_LINK flag)
        const uint32_t n = counter.fetch_add(1);
        memcpy(kid + 4, &n, 4);
    }
    if (key)
        for (int i = 0; i < 16; i++)
            key[i] = uint8_t(0x30 + i);
    return 0;
}

int NetDll_XNetRegisterKey(uint32_t caller, const uint8_t* kid, const uint8_t* key)
{
    return 0;
}

int NetDll_XNetUnregisterKey(uint32_t caller, const uint8_t* kid)
{
    return 0;
}

int NetDll_XNetXnAddrToInAddr(uint32_t caller, const uint8_t* xnaddr, const uint8_t* kid, uint8_t* ina)
{
    LogChain("XnAddrToInAddr");
    if (!xnaddr || !ina)
        return WSAEINVAL;
    memcpy(ina, xnaddr, 4);  // the secure address is the machine's real address
    NETLOG("XNetXnAddrToInAddr -> %u.%u.%u.%u", ina[0], ina[1], ina[2], ina[3]);
    return 0;
}

int NetDll_XNetInAddrToXnAddr(uint32_t caller, uint32_t ina, uint8_t* xnaddr, uint8_t* kid)
{
    if (xnaddr)
    {
        memset(xnaddr, 0, 36);
        xnaddr[0] = uint8_t(ina >> 24); xnaddr[1] = uint8_t(ina >> 16); xnaddr[2] = uint8_t(ina >> 8); xnaddr[3] = uint8_t(ina);
        xnaddr[10] = 0x00; xnaddr[11] = 0x22; xnaddr[12] = 0x48; xnaddr[13] = xnaddr[1]; xnaddr[14] = xnaddr[2]; xnaddr[15] = xnaddr[3];
    }
    if (kid)
        memset(kid, 0, 8);
    return 0;
}

int NetDll_XNetUnregisterInAddr(uint32_t caller, uint32_t ina)
{
    return 0;
}

int NetDll_XNetServerToInAddr(uint32_t caller, uint32_t ina, uint32_t serviceId, uint8_t* out)
{
    if (out)
    {
        out[0] = uint8_t(ina >> 24); out[1] = uint8_t(ina >> 16); out[2] = uint8_t(ina >> 8); out[3] = uint8_t(ina);
    }
    return 0;
}

int NetDll_XNetInAddrToServer(uint32_t caller, uint32_t ina, uint8_t* out)
{
    return NetDll_XNetServerToInAddr(caller, ina, 0, out);
}

int NetDll_XNetTsAddrToInAddr(uint32_t caller, const uint8_t* tsaddr, uint32_t serviceId, const uint8_t* kid, uint8_t* ina)
{
    if (ina && tsaddr)
        memcpy(ina, tsaddr, 4);
    return 0;
}

int NetDll_XNetInAddrToString(uint32_t caller, uint32_t ina, char* out, uint32_t length)
{
    if (out && length)
        snprintf(out, length, "%u.%u.%u.%u", ina >> 24, (ina >> 16) & 255, (ina >> 8) & 255, ina & 255);
    return 0;
}

int NetDll_XNetXnAddrToMachineId(uint32_t caller, const uint8_t* xnaddr, be<uint64_t>* id)
{
    if (id && xnaddr)
    {
        uint64_t v = 0;
        for (int i = 0; i < 6; i++)
            v = (v << 8) | xnaddr[10 + i];
        *id = v;
    }
    return 0;
}

int NetDll_XNetRandom(uint8_t* buffer, uint32_t length)
{
    for (uint32_t i = 0; i < length && buffer; i++)
        buffer[i] = uint8_t(rand());
    return 0;
}

// XNDNS: INT iStatus (WSAEINPROGRESS while pending, 0 on success, else a WSA error), UINT cina, IN_ADDR aina[8]. Every name resolves to the lobby host: NFSMW_NET_HOST=<ip> (default
// 127.0.0.1): the game's servers (EA's, long gone) are replaced by one of ours.
int NetDll_XNetDnsLookup(uint32_t caller, const char* host, uint32_t event, be<uint32_t>* result)
{
    in_addr a{};
    const char* want = std::getenv("NFSMW_NET_HOST");
    if (!want || inet_pton(AF_INET, want, &a) != 1)
        inet_pton(AF_INET, "127.0.0.1", &a);
    const uint32_t block = ExAllocatePoolWithTag(48, 0);
    uint8_t* dns = reinterpret_cast<uint8_t*>(g_memory.Translate(block));
    memset(dns, 0, 48);
    dns[3] = 0;  // iStatus = 0: success (WSAEINPROGRESS while pending)
    dns[7] = 1;  // cina = 1
    memcpy(dns + 8, &a.s_addr, 4);
    NETLOG("XNetDnsLookup(\"%s\") -> %s", host ? host : "", inet_ntoa(a));
    if (result)
        *result = block;
    if (event && IsKernelObject(event))
        if (auto* e = dynamic_cast<EventObject*>(GetKernelObject(event)))
            e->Set();
    return 0;
}

int NetDll_XNetDnsRelease(uint32_t caller, uint32_t dns)
{
    NETLOG("XNetDnsRelease(%08X)", dns);
    return 0;
}

int NetDll_XNetQosListen(uint32_t caller, const uint8_t* kid, const uint8_t* data, uint32_t size, uint32_t bitsPerSec, uint32_t flags)
{
    NETLOG("XNetQosListen: not supported");
    return WSAEOPNOTSUPP;
}

int NetDll_XNetQosLookup(uint32_t caller, uint32_t count, const uint32_t* xnaddrs, const uint32_t* kids, const uint32_t* keys, uint32_t numGateways,
    const uint32_t* gatewayAddrs, const uint32_t* serviceIds, uint32_t probes, uint32_t bitsPerSec, uint32_t flags, uint32_t event, be<uint32_t>* result)
{
    NETLOG("XNetQosLookup: not supported");
    if (result)
        *result = 0;
    return WSAEOPNOTSUPP;
}

int NetDll_XNetQosServiceLookup(uint32_t caller, uint32_t flags, uint32_t event, be<uint32_t>* result)
{
    if (result)
        *result = 0;
    return WSAEOPNOTSUPP;
}

int NetDll_XNetQosRelease(uint32_t caller, uint32_t qos)
{
    return 0;
}

int NetDll_XNetQosGetListenStats(uint32_t caller, const uint8_t* kid, uint8_t* stats)
{
    return WSAEOPNOTSUPP;
}

// ---- winsock ------------------------------------------------------------------------

uint32_t NetDll_socket(uint32_t caller, int af, int type, int protocol)
{
    LogChain("socket");
    if (af != 2 || (type != 1 && type != 2))
    {
        NETLOG("socket(af %d, type %d, proto %d): refused", af, type, protocol);
        t_lastError = WSAEAFNOSUPPORT;
        return INVALID_SOCKET_;
    }
    const int fd = socket(AF_INET, type == 1 ? SOCK_STREAM : SOCK_DGRAM, 0);  // IPPROTO_VDP (254) is plain UDP here
    if (fd < 0)
    {
        t_lastError = FromErrno(errno);
        return INVALID_SOCKET_;
    }
    std::call_once(s_pollerOnce, [] { std::thread(PollerLoop).detach(); });
    std::lock_guard lock(s_socketsMutex);
    const uint32_t id = s_nextSocket++;
    s_sockets[id] = Sock{ fd, type == 1 ? SOCK_STREAM : SOCK_DGRAM };
    NETLOG("socket(type %d, proto %d) = %u (fd %d)", type, protocol, id, fd);
    return id;
}

int NetDll_closesocket(uint32_t caller, uint32_t s)
{
    NETLOG("closesocket(%u)", s);
    std::lock_guard lock(s_socketsMutex);
    Sock* sock = Find(s);
    if (!sock)
    {
        t_lastError = WSAENOTSOCK;
        return SOCKET_ERROR_;
    }
    close(sock->fd);
    s_sockets.erase(s);
    return 0;
}

int NetDll_shutdown(uint32_t caller, uint32_t s, int how)
{
    const int fd = HostFd(s);
    return fd < 0 ? (t_lastError = WSAENOTSOCK, SOCKET_ERROR_) : (shutdown(fd, how) == 0 ? 0 : Fail());
}

int NetDll_ioctlsocket(uint32_t caller, uint32_t s, uint32_t cmd, be<uint32_t>* arg)
{
    NETLOG("ioctlsocket(%u, %08X)", s, cmd);
    std::lock_guard lock(s_socketsMutex);
    Sock* sock = Find(s);
    if (!sock)
    {
        t_lastError = WSAENOTSOCK;
        return SOCKET_ERROR_;
    }
    if (cmd == 0x8004667E)  // FIONBIO
    {
        sock->nonblocking = arg && uint32_t(*arg) != 0;
        const int flags = fcntl(sock->fd, F_GETFL, 0);
        fcntl(sock->fd, F_SETFL, sock->nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
        return 0;
    }
    if (cmd == 0x4004667F)  // FIONREAD
    {
        int n = 0;
        ioctl(sock->fd, FIONREAD, &n);
        if (arg)
            *arg = uint32_t(n);
        return 0;
    }
    NETLOG("ioctlsocket(%08X): ignored", cmd);
    return 0;
}

int NetDll_setsockopt(uint32_t caller, uint32_t s, int level, int name, const uint8_t* value, int length)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    int v = 0;
    if (value && length >= 4)
        v = int((uint32_t(value[0]) << 24) | (uint32_t(value[1]) << 16) | (uint32_t(value[2]) << 8) | value[3]);
    else if (value && length >= 1)
        v = value[0];
    if (level == 0xFFFF)
    {
        switch (name)
        {
        case 0x0004: setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)); break;
        case 0x0020: setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &v, sizeof(v)); break;
        case 0x1001: setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)); break;
        case 0x1002: setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)); break;
        default: NETLOG("setsockopt(SOL_SOCKET, %04X): ignored", name);
        }
    }
    else if (level == 6 && name == 1)
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
    else
        NETLOG("setsockopt(level %d, %d): ignored", level, name);
    return 0;
}

int NetDll_getsockopt(uint32_t caller, uint32_t s, int level, int name, uint8_t* value, be<int32_t>* length)
{
    NETLOG("getsockopt(%u, level %d, %04X)", s, level, name);
    if (value && length && int32_t(*length) >= 4)
    {
        memset(value, 0, 4);
        if (level == 0xFFFF && name == 0x1007)  // SO_ERROR
        {
            int e = 0;
            socklen_t l = sizeof(e);
            if (getsockopt(HostFd(s), SOL_SOCKET, SO_ERROR, &e, &l) == 0 && e)
            {
                const uint32_t w = FromErrno(e);
                value[0] = uint8_t(w >> 24); value[1] = uint8_t(w >> 16); value[2] = uint8_t(w >> 8); value[3] = uint8_t(w);
            }
        }
        *length = 4;
    }
    return 0;
}

int NetDll_bind(uint32_t caller, uint32_t s, const uint8_t* name, int length)
{
    LogChain("bind");
    sockaddr_in a;
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    if (!ToHost(name, length, a))
        return (t_lastError = WSAEFAULT, SOCKET_ERROR_);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // the game's address in the XNADDR must be reachable: bind on all interfaces whatever address it names
    const uint16_t port = ntohs(a.sin_port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
        return Fail();
    NETLOG("bind(socket %u, port %u)", s, port);
    return 0;
}

int NetDll_connect(uint32_t caller, uint32_t s, const uint8_t* name, int length)
{
    LogChain("connect");
    sockaddr_in a;
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    if (!ToHost(name, length, a))
        return (t_lastError = WSAEFAULT, SOCKET_ERROR_);
    NETLOG("connect(socket %u, %s:%u)", s, inet_ntoa(a.sin_addr), ntohs(a.sin_port));
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
        return Fail();
    return 0;
}

int NetDll_listen(uint32_t caller, uint32_t s, int backlog)
{
    NETLOG("listen(%u)", s);
    std::lock_guard lock(s_socketsMutex);
    Sock* sock = Find(s);
    if (!sock)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    sock->listening = true;
    return listen(sock->fd, backlog) == 0 ? 0 : Fail();
}

uint32_t NetDll_accept(uint32_t caller, uint32_t s, uint8_t* addr, be<int32_t>* length)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, INVALID_SOCKET_);
    sockaddr_in a{};
    socklen_t l = sizeof(a);
    const int nfd = accept(fd, reinterpret_cast<sockaddr*>(&a), &l);
    if (nfd < 0)
        return (Fail(), INVALID_SOCKET_);
    if (addr && length && int32_t(*length) >= 16)
    {
        ToGuest(a, addr);
        *length = 16;
    }
    std::lock_guard lock(s_socketsMutex);
    const uint32_t id = s_nextSocket++;
    s_sockets[id] = Sock{ nfd, SOCK_STREAM };
    return id;
}

int NetDll_getsockname(uint32_t caller, uint32_t s, uint8_t* name, be<int32_t>* length)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    sockaddr_in a{};
    socklen_t l = sizeof(a);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&a), &l) != 0)
        return Fail();
    if (name && length && int32_t(*length) >= 16)
    {
        ToGuest(a, name);
        // the address the game should publish for itself, not 0.0.0.0
        const uint32_t ina = LocalAddress();
        name[4] = uint8_t(ina >> 24); name[5] = uint8_t(ina >> 16); name[6] = uint8_t(ina >> 8); name[7] = uint8_t(ina);
        *length = 16;
    }
    return 0;
}

int NetDll_getpeername(uint32_t caller, uint32_t s, uint8_t* name, be<int32_t>* length)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    sockaddr_in a{};
    socklen_t l = sizeof(a);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&a), &l) != 0)
        return Fail();
    if (name && length && int32_t(*length) >= 16)
    {
        ToGuest(a, name);
        *length = 16;
    }
    return 0;
}

int NetDll_send(uint32_t caller, uint32_t s, const uint8_t* buffer, int length, int flags)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    const ssize_t n = send(fd, buffer, size_t(length), MSG_NOSIGNAL);
    if (s_log)
    {
        static std::atomic<uint32_t> count{ 0 };
        if (count.fetch_add(1) < 12)
        {
            std::string hex;
            for (int i = 0; i < std::min(length, 96); i++)
            {
                char b[4];
                snprintf(b, sizeof(b), "%02X ", buffer[i]);
                hex += b;
            }
            NETLOG("send(socket %u, %d bytes) = %zd: %s", s, length, n, hex.c_str());
        }
    }
    return n < 0 ? Fail() : int(n);
}

int NetDll_recv(uint32_t caller, uint32_t s, uint8_t* buffer, int length, int flags)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    const ssize_t n = recv(fd, buffer, size_t(length), flags & 2 ? MSG_PEEK : 0);
    return n < 0 ? Fail() : int(n);
}

int NetDll_sendto(uint32_t caller, uint32_t s, const uint8_t* buffer, int length, int flags, const uint8_t* to, int toLength)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    sockaddr_in a;
    if (!ToHost(to, toLength, a))
        return (t_lastError = WSAEFAULT, SOCKET_ERROR_);
    const ssize_t n = sendto(fd, buffer, size_t(length), MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    if (s_log)
    {
        static std::atomic<uint32_t> count{ 0 };
        if (count.fetch_add(1) < 20)
            NETLOG("sendto(socket %u, %d bytes) -> %s:%u = %zd", s, length, inet_ntoa(a.sin_addr), ntohs(a.sin_port), n);
    }
    return n < 0 ? Fail() : int(n);
}

int NetDll_recvfrom(uint32_t caller, uint32_t s, uint8_t* buffer, int length, int flags, uint8_t* from, be<int32_t>* fromLength)
{
    const int fd = HostFd(s);
    if (fd < 0)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    sockaddr_in a{};
    socklen_t l = sizeof(a);
    const ssize_t n = recvfrom(fd, buffer, size_t(length), flags & 2 ? MSG_PEEK : 0, reinterpret_cast<sockaddr*>(&a), &l);
    if (n < 0)
        return Fail();
    if (from && fromLength && int32_t(*fromLength) >= 16)
    {
        ToGuest(a, from);
        *fromLength = 16;
    }
    return int(n);
}

// fd_set: u_int fd_count, SOCKET fd_array[64] (big-endian); timeval: long tv_sec, long tv_usec.
int NetDll_select(uint32_t caller, int nfds, uint8_t* readSet, uint8_t* writeSet, uint8_t* exceptSet, const uint8_t* timeout)
{
    auto be32 = [](const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; };
    auto put32 = [](uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); };
    struct Entry { uint8_t* set; short events; };
    std::vector<pollfd> fds;
    std::vector<std::pair<uint8_t*, uint32_t>> owners;  // the set and the guest socket for each pollfd
    for (Entry e : { Entry{ readSet, POLLIN }, Entry{ writeSet, POLLOUT } })
    {
        if (!e.set)
            continue;
        const uint32_t count = std::min<uint32_t>(be32(e.set), 64);
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t id = be32(e.set + 4 + 4 * i);
            const int fd = HostFd(id);
            if (fd >= 0)
            {
                fds.push_back({ fd, e.events, 0 });
                owners.emplace_back(e.set, id);
            }
        }
    }
    int waitMs = -1;
    if (timeout)
        waitMs = int(int32_t(be32(timeout)) * 1000 + int32_t(be32(timeout + 4)) / 1000);
    const int n = poll(fds.data(), nfds_t(fds.size()), waitMs);
    if (n < 0)
        return Fail();
    int ready = 0;
    uint32_t kept[2] = { 0, 0 };
    uint32_t keptSockets[2][64];
    uint8_t* sets[2] = { readSet, writeSet };
    for (size_t i = 0; i < fds.size(); i++)
    {
        const int which = owners[i].first == readSet ? 0 : 1;
        const short want = which == 0 ? POLLIN : POLLOUT;
        if (fds[i].revents & (want | POLLHUP | POLLERR))
        {
            if (kept[which] < 64)
                keptSockets[which][kept[which]++] = owners[i].second;
            ready++;
        }
    }
    for (int w = 0; w < 2; w++)
        if (sets[w])
        {
            put32(sets[w], kept[w]);
            for (uint32_t k = 0; k < kept[w]; k++)
                put32(sets[w] + 4 + 4 * k, keptSockets[w][k]);
        }
    if (exceptSet)
        put32(exceptSet, 0);
    return ready;
}

int NetDll___WSAFDIsSet(uint32_t caller, uint32_t s, const uint8_t* set)
{
    if (!set)
        return 0;
    auto be32 = [](const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; };
    const uint32_t count = std::min<uint32_t>(be32(set), 64);
    for (uint32_t i = 0; i < count; i++)
        if (be32(set + 4 + 4 * i) == s)
            return 1;
    return 0;
}

uint32_t NetDll_inet_addr(const char* text)
{
    in_addr a{};
    if (!text || inet_pton(AF_INET, text, &a) != 1)
        return 0xFFFFFFFF;
    return ntohl(a.s_addr);  // the four bytes read as the guest reads them
}

// ---- winsock events and overlapped calls ------------------------------------------------

uint32_t NetDll_WSACreateEvent()
{
    const uint32_t handle = GetKernelHandle(CreateKernelObject<EventObject>(true, false));
    return handle;
}

int NetDll_WSACloseEvent(uint32_t handle)
{
    NtClose(handle);
    return 1;
}

int NetDll_WSASetEvent(uint32_t handle)
{
    if (IsKernelObject(handle))
        if (auto* e = dynamic_cast<EventObject*>(GetKernelObject(handle)))
            return e->Set(), 1;
    return 0;
}

int NetDll_WSAResetEvent(uint32_t handle)
{
    if (IsKernelObject(handle))
        if (auto* e = dynamic_cast<EventObject*>(GetKernelObject(handle)))
            return e->Reset(), 1;
    return 0;
}

int NetDll_WSAEventSelect(uint32_t caller, uint32_t s, uint32_t event, uint32_t mask)
{
    std::lock_guard lock(s_socketsMutex);
    Sock* sock = Find(s);
    if (!sock)
        return (t_lastError = WSAENOTSOCK, SOCKET_ERROR_);
    sock->eventHandle = event;
    sock->eventMask = mask;
    // an event-selected socket is non-blocking
    const int flags = fcntl(sock->fd, F_GETFL, 0);
    if (mask)
        fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK);
    NETLOG("WSAEventSelect(socket %u, event %08X, mask %02X)", s, event, mask);
    return 0;
}

// (No caller argument, as for the other event calls.) WSAWaitForMultipleEvents(count, handles, waitAll, timeoutMs, alertable): 0xFFFFFFFF is no timeout; the answer is WSA_WAIT_EVENT_0 + index, or 0x102 on timeout.
uint32_t NetDll_WSAWaitForMultipleEvents(uint32_t count, const be<uint32_t>* handles, uint32_t waitAll, uint32_t timeoutMs, uint32_t alertable)
{
    std::vector<Waitable*> objects;
    for (uint32_t i = 0; i < count; i++)
    {
        Waitable* w = WaitableFromHandle(handles[i]);
        if (!w)
            return 0xFFFFFFFF;  // WSA_WAIT_FAILED
        objects.push_back(w);
    }
    const auto deadline = timeoutMs == 0xFFFFFFFF ? std::chrono::steady_clock::time_point::max()
                                                   : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true)
    {
        for (uint32_t i = 0; i < count; i++)
            if (objects[i]->Wait(0) == 0)  // signaled (a manual-reset event stays set)
                return i;  // WSA_WAIT_EVENT_0 + i
        if (std::chrono::steady_clock::now() >= deadline)
            return 0x102;  // WSA_WAIT_TIMEOUT
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int NetDll_WSAGetOverlappedResult(uint32_t caller, uint32_t s, uint32_t overlapped, be<uint32_t>* transferred, uint32_t wait, be<uint32_t>* flags)
{
    return 0;
}

int NetDll_WSACancelOverlappedIO(uint32_t caller, uint32_t s)
{
    return 0;
}

// WSABUF: u_long len, char* buf (big-endian guest pointer); the calls are done at once whether or not an OVERLAPPED is given.
int NetDll_WSASendTo(uint32_t caller, uint32_t s, const uint8_t* buffers, uint32_t count, be<uint32_t>* sent, uint32_t flags, const uint8_t* to, int toLength,
    uint32_t overlapped, uint32_t completion)
{
    NETLOG("WSASendTo: not supported yet");
    t_lastError = WSAEOPNOTSUPP;
    return SOCKET_ERROR_;
}

int NetDll_WSARecvFrom(uint32_t caller, uint32_t s, const uint8_t* buffers, uint32_t count, be<uint32_t>* received, be<uint32_t>* flags, uint8_t* from,
    be<int32_t>* fromLength, uint32_t overlapped, uint32_t completion)
{
    NETLOG("WSARecvFrom: not supported yet");
    t_lastError = WSAEOPNOTSUPP;
    return SOCKET_ERROR_;
}

int NetDll_WSASend(uint32_t caller, uint32_t s, const uint8_t* buffers, uint32_t count, be<uint32_t>* sent, uint32_t flags, uint32_t overlapped, uint32_t completion)
{
    NETLOG("WSASend: not supported yet");
    t_lastError = WSAEOPNOTSUPP;
    return SOCKET_ERROR_;
}

int NetDll_WSARecv(uint32_t caller, uint32_t s, const uint8_t* buffers, uint32_t count, be<uint32_t>* received, be<uint32_t>* flags, uint32_t overlapped,
    uint32_t completion)
{
    NETLOG("WSARecv: not supported yet");
    t_lastError = WSAEOPNOTSUPP;
    return SOCKET_ERROR_;
}

GUEST_FUNCTION_HOOK(__imp__XamAlloc, XamAlloc);
GUEST_FUNCTION_HOOK(__imp__XamFree, XamFree);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetStartup, NetDll_XNetStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetCleanup, NetDll_XNetCleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAStartup, NetDll_WSAStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACleanup, NetDll_WSACleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAGetLastError, NetDll_WSAGetLastError);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetTitleXnAddr, NetDll_XNetGetTitleXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetDebugXnAddr, NetDll_XNetGetDebugXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetEthernetLinkStatus, NetDll_XNetGetEthernetLinkStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetBroadcastVersionStatus, NetDll_XNetGetBroadcastVersionStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetOpt, NetDll_XNetGetOpt);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetSetOpt, NetDll_XNetSetOpt);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetConnectStatus, NetDll_XNetGetConnectStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetConnect, NetDll_XNetConnect);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetCreateKey, NetDll_XNetCreateKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetRegisterKey, NetDll_XNetRegisterKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetUnregisterKey, NetDll_XNetUnregisterKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetXnAddrToInAddr, NetDll_XNetXnAddrToInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetInAddrToXnAddr, NetDll_XNetInAddrToXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetUnregisterInAddr, NetDll_XNetUnregisterInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetServerToInAddr, NetDll_XNetServerToInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetInAddrToServer, NetDll_XNetInAddrToServer);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetTsAddrToInAddr, NetDll_XNetTsAddrToInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetInAddrToString, NetDll_XNetInAddrToString);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetXnAddrToMachineId, NetDll_XNetXnAddrToMachineId);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetRandom, NetDll_XNetRandom);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetDnsLookup, NetDll_XNetDnsLookup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetDnsRelease, NetDll_XNetDnsRelease);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosListen, NetDll_XNetQosListen);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosLookup, NetDll_XNetQosLookup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosServiceLookup, NetDll_XNetQosServiceLookup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosRelease, NetDll_XNetQosRelease);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosGetListenStats, NetDll_XNetQosGetListenStats);
GUEST_FUNCTION_HOOK(__imp__NetDll_socket, NetDll_socket);
GUEST_FUNCTION_HOOK(__imp__NetDll_closesocket, NetDll_closesocket);
GUEST_FUNCTION_HOOK(__imp__NetDll_shutdown, NetDll_shutdown);
GUEST_FUNCTION_HOOK(__imp__NetDll_ioctlsocket, NetDll_ioctlsocket);
GUEST_FUNCTION_HOOK(__imp__NetDll_setsockopt, NetDll_setsockopt);
GUEST_FUNCTION_HOOK(__imp__NetDll_getsockopt, NetDll_getsockopt);
GUEST_FUNCTION_HOOK(__imp__NetDll_bind, NetDll_bind);
GUEST_FUNCTION_HOOK(__imp__NetDll_connect, NetDll_connect);
GUEST_FUNCTION_HOOK(__imp__NetDll_listen, NetDll_listen);
GUEST_FUNCTION_HOOK(__imp__NetDll_accept, NetDll_accept);
GUEST_FUNCTION_HOOK(__imp__NetDll_getsockname, NetDll_getsockname);
GUEST_FUNCTION_HOOK(__imp__NetDll_getpeername, NetDll_getpeername);
GUEST_FUNCTION_HOOK(__imp__NetDll_send, NetDll_send);
GUEST_FUNCTION_HOOK(__imp__NetDll_recv, NetDll_recv);
GUEST_FUNCTION_HOOK(__imp__NetDll_sendto, NetDll_sendto);
GUEST_FUNCTION_HOOK(__imp__NetDll_recvfrom, NetDll_recvfrom);
GUEST_FUNCTION_HOOK(__imp__NetDll_select, NetDll_select);
GUEST_FUNCTION_HOOK(__imp__NetDll___WSAFDIsSet, NetDll___WSAFDIsSet);
GUEST_FUNCTION_HOOK(__imp__NetDll_inet_addr, NetDll_inet_addr);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACreateEvent, NetDll_WSACreateEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACloseEvent, NetDll_WSACloseEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASetEvent, NetDll_WSASetEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAResetEvent, NetDll_WSAResetEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAEventSelect, NetDll_WSAEventSelect);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAWaitForMultipleEvents, NetDll_WSAWaitForMultipleEvents);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAGetOverlappedResult, NetDll_WSAGetOverlappedResult);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACancelOverlappedIO, NetDll_WSACancelOverlappedIO);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASendTo, NetDll_WSASendTo);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSARecvFrom, NetDll_WSARecvFrom);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASend, NetDll_WSASend);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSARecv, NetDll_WSARecv);
