// mp_server [port]: the multiplayer dedicated server as a stand-alone program (no game files needed).
#include "mp_server.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    uint16_t port = argc > 1 ? uint16_t(atoi(argv[1])) : mp::kDefaultPort;
    mp::Server s;
    if (!s.Start(port))
    {
        fprintf(stderr, "[mp-server] cannot listen on UDP %u\n", port);
        return 1;
    }
    fprintf(stderr, "[mp-server] listening on UDP %u\n", port);
    for (;;)
        s.Service(5);
}
