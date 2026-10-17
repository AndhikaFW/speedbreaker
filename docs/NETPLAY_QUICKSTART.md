# Two-machine test of online play (x86-64 Linux as the second machine)

Status: **untested between two real machines.** The relay, the client and a scripted second player were tested on one machine (docs/MULTIPLAYER.md).
Building on x86-64 Linux outside SteamOS is, per the README, "untested so far". This is the shortest path, to be corrected by what actually happens.

## 0. Prerequisites on the x86 machine
- Your own copy of the game: the USA disc image (or this machine's `game/files` folder, your own extracted copy, copied over). The executable must have
  SHA-256 `aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c` (`sha256sum game/files/default.xex`). Never commit or share it.
- Packages (Fedora names; Debian/Arch equivalents): `clang lld cmake ninja-build git python3 patch curl nasm SDL3-devel vulkan-headers
  vulkan-loader-devel glslang spirv-tools mesa-vulkan-drivers pkgconf-pkg-config make diffutils pulseaudio-libs-devel alsa-lib-devel libdecor-devel`.
  Clang 18 or newer. A GPU with Vulkan 1.2 and BC (DXT) texture support. The CPU needs AVX2 (the build uses `-march=x86-64-v3`).
- About 8 GB of free disk for the build and the game, and 8 GB of RAM or more.

## 1. Get the fork and build it
```sh
git clone https://github.com/AndhikaFW/speedbreaker.git
cd speedbreaker
scripts/setup_tools.sh          # XenonRecomp (with the project's patches), extract-xiso, FFmpeg's XMA decoder (with patches/ffmpeg)
mkdir -p game/files
tools/extract-xiso/build/extract-xiso -d game/files -x /path/to/your-backup.iso    # or copy this machine's game/files here
sha256sum game/files/default.xex                                                    # must match the hash above
mkdir -p ppc
tools/XenonRecomp/build/XenonRecomp/XenonRecomp config/nfsmw.toml tools/XenonRecomp/XenonUtils/ppc_context.h
cmake -S . -B build/main -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -j4 -C build/main SpeedBreaker      # use -j2 if the machine runs short of memory (renderer.cpp is large)
```
Do not copy `tools/` or `build/` from the ARM machine (they are aarch64 builds); `ppc/` is generated C++ and could be copied, but regenerating it is
the supported way. Run the game from the repository root: `build/main/runtime/SpeedBreaker` (with the disc extracted in `game/files` it plays from there).
Check it starts and plays on its own before trying online play.

## 2. The relay server (on either machine, or any host both can reach)
```sh
python3 scripts/netplay/server.py --port 7777
```
Open UDP port 7777 to the other machine (Fedora: `sudo firewall-cmd --add-port=7777/udp`, temporary until reboot; behind a router over the internet:
forward UDP 7777 to the host). On the LAN, the server machine's address is shown by `hostname -I`.

## 3. Start both games (same room name)
- Machine A (player 1, makes the menu choices): `NFSMW_NETPLAY=<server-ip>:7777/room1/0 build/main/runtime/SpeedBreaker`
- Machine B (player 2): `NFSMW_NETPLAY=<server-ip>:7777/room1/1 build/main/runtime/SpeedBreaker`
Each log (the session log, or the terminal) should show `[netplay] room "room1": 2 players` and `the other player's controller is arriving`.
Settings that help on the slower machine: Internal Resolution 1x, Shadow Resolution 800.

## 4. Start a race
On A: Quick Race, then Split Screen, then a mode, track, options, and player 1's car and transmission, as in single player. Both machines show the same menus
(A's pad is player 1 on both). At "Select player 2: Player 2 press START", **machine B presses START** on its own keyboard or pad, then picks a car and a
transmission. B's keyboard and pad are player 2 on both machines. Each machine draws both viewports (the top is player 1, the bottom is player 2).

## What to look for, and report
- Do both machines get to the race, and do the cars on the two screens stay together? The game runs the whole race on each machine from the two
  players' inputs, with nothing keeping the two simulations in step: expect them to drift apart over time (not shown deterministic). A screenshot of both
  screens at the same moment, 30 and 60 seconds into a race, tells us how fast.
- The `[netplay] last 10 s: sent ..., received ...` lines: about 300 each way per 10 s (30 a second) means the link is healthy.
- Anything in the log starting with `[crash]` or `[netplay]`.
