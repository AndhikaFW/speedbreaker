# Asahi / Apple M1 tuning (fork notes)

Notes on the graphics, memory and latency work in this fork, on top of SpeedBreaker v0.1.4.
Target machine for all measurements: an Apple M1 MacBook Air (13", fanless) running Fedora Asahi
(Linux, aarch64), the Apple GPU through the Asahi Vulkan driver, 7.5 GB of RAM.

Every claim below is labelled **measured** (numbers), **seen** (a screenshot or hands-on use), or
**not verified**. Timing on this machine drifts by up to about 18 % between identical runs, so small
differences are not visible in whole-run comparisons; where it mattered, effects were compared by
switching them live inside one run in a still scene (a paused race).

No game files are in this repository, and none should be committed (`game/`, `ppc/`, `tools/` and
`build/` are git-ignored). The build recompiles your own `default.xex` (SHA-256
`aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c`).

## Where the changes live

Almost all changes are edits to **source files**. There is exactly **one new file in `patches/`**,
which patches FFmpeg (rebuilt by `scripts/build_ffmpeg_xma.sh`). Nothing was changed in the
XenonRecomp patches.

| Path | Kind | What it does |
|---|---|---|
| `patches/ffmpeg/0002-xma-one-stream-per-context.patch` | **patch** (new) | XMA decoder: one stream per decoder instead of 8 (memory) |
| `runtime/game/colour_grade.cpp` | source (new) | Overrides recompiled game functions: orange tint, native bloom, Rich Blacks, shadow map size, a guest-patch test hook |
| `runtime/gpu/renderer.cpp` | source | Tone stage injected into the game's grade shaders; motion blur; reflection refresh; shader-translation tools; memory pool sizing |
| `runtime/gpu/shader_translator.cpp` | source | `NFSMW_XMUL` switch for the shader multiply helper |
| `runtime/kernel/write_watch.cpp` | source | Lazy readback of small GPU-resolved buffers (removes CPU stalls) |
| `runtime/hid/input.cpp`, `input.h` | source | Exposes the accelerator and brake triggers for the motion blur |
| `runtime/user/settings.cpp`, `settings.h` | source | New settings |
| `runtime/video/display_tone.h`, `display_gamma.cpp`, `presenter.cpp` | source | Presenter-side colour code, mostly moved into the game's own shaders and code |

Changes to the game's behaviour are made by overriding **recompiled guest functions** (the generated
ones are weak symbols, so no regeneration of `ppc/` is needed) and by editing the **GLSL the shader
translator produces**, not by post-processing the finished picture, wherever that was possible.

After pulling this, rebuild the FFmpeg library (the patch changes it) and reconfigure CMake:

```sh
scripts/build_ffmpeg_xma.sh tools/ffmpeg-src/ffmpeg-7.1.1.tar.xz tools/ffmpeg-xma
cmake -S . -B build/main -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -j2 -C build/main SpeedBreaker      # -j2: renderer.cpp is large; on 7.5 GB, more parallelism ran out of memory
```

## New and changed settings

| Setting (menu) | Key | Default | Env override | Notes |
|---|---|---|---|---|
| Remove Orange Tint (Display) | `remove_orange_tint` | 1.0 | `NFSMW_ORANGE_TINT` | live |
| Modern Look (Display) | `modern_look` | 0.7 | `NFSMW_MODERN_LOOK` | live |
| Rich Blacks (Display) | `rich_blacks` | 0.6 | `NFSMW_RICH_BLACKS` | **applies at the next start** |
| Bloom (Graphics) | `bloom` | 0.4 | `NFSMW_BLOOM` | scales the game's own bloom; 0.4 is the game's value |
| Modern Lighting (Graphics) | `modern_lighting` | 0.7 | `NFSMW_LIGHTING` | strength of the tonemapper |
| Tonemapper (Graphics) | `tonemapper` | AgX | `NFSMW_TONEMAPPER` | `filmic`, `agx`, `off` (names, not numbers) |
| Motion Blur (Graphics) | `motion_blur` | 0.6 | `NFSMW_MOTION_BLUR` | strength |
| Ambient Occlusion (Graphics) | `ambient_occlusion` | 0.0 | `NFSMW_SSAO` | built; off by default, no visible benefit in use |
| Shadow Resolution (Graphics) | `shadow_resolution` | 1600 | `NFSMW_SHADOW_SIZE` | 1600 / 1200 / 800; **applies at the next start** |
| Reflection Refresh (Graphics) | `reflection_refresh` | every 3rd frame | `NFSMW_REFLECTION_REFRESH` | `every_frame`, `every_2`, `every_3`, `every_6` |

Enum settings accept the names above in environment variables, not numbers.

## What each change does, and the evidence

### Memory (measured)
- Process RAM in a race: about **1,680 MB -> about 810 MB**.
- Cause: the XMA audio decoder context embeds a full WMA Pro decoder state for each of 8 possible
  streams (about 3.2 MB, zeroed so resident) and the game keeps about 290 decoders open (about 913 MB
  of live allocations, found with an allocation-tracking `LD_PRELOAD` shim). The game uses exactly one
  stream per context, so the patch sets `XMA_MAX_STREAMS` to 1 and allocates audio FIFOs only for
  streams that exist.
- The scaled-resolve pool was a flat 512 MB, committed up front; a 1.5x race used about 35 MB a frame
  with no evictions at 128 MB. It is now sized from the internal resolution (192 MB at 1.5x, 256 MB
  above 2.5x area, 512 MB above 4.5x). System shared memory dropped by about 340 MB.
  `NFSMW_SCALED_MB` still overrides it.
- Audio: the XMA statistics show frames decoding normally. **Not verified by ear.**

### Colour: made inside the game, not on top of the picture
- **Orange tint** (measured): the game's final grade multiplies red, green and blue by a cubic gain
  curve from four 16-byte constants, `c9..c12` in one shader variant and `c6..c9` in another. At full
  brightness the gains were 1.403 : 1.237 : 0.412 (the orange cast). The game rebuilds that block
  every frame in `sub_82223BB8`, so patching it there does nothing; the block is replaced where it is
  sent, in `sub_82449820` (the "set shader constant" call), from the first of that function's four
  sends (return address `0x82223CB0`). Both shader variants (`ps_74c891...` with depth of field and
  `ps_d52a5e...` without) then receive gains of 1.213 : 1.213 : 1.213. A first version patched one
  shader by hash, which is why the tint came back in cutscenes.
- **Bloom**: `g_fBloomScale` (the game's own, 4.0) is sent from `sub_82224380` through
  `sub_824491F8` (return address `0x8222441C`); it is scaled by the Bloom setting.
- **Tonemapper (AgX / Filmic / Off) and Modern Look**: a function is inserted into the translated
  GLSL of the two grade shaders, applied to the colour just before it is written. Its settings travel
  in `drawInfo[0]` of the constant block.
- **Rich Blacks**: `sub_82458C20` builds the display ramp, `pow(k/255, exponent)` into three 16-bit
  tables, from an exponent derived from the game's brightness. The exponent is moved toward 1.4 by
  the setting (measured: 64 maps to 25.9 instead of 68.0 at 0.85). The game builds the ramp at start,
  so the setting applies at the next start.
- Looks: **seen** on one frame per feature; no side-by-side across scenes.

### CPU stalls (measured)
The game's CPU thread waited for the GPU every time it read the auto-exposure buffers (two 64x64
luminance targets and two 320x180 targets): 180-208 stalls (4.3-4.9 s) per 100-second race run.
Lazy readback serves those reads from the GPU's current copy without waiting (small `resolve`
buffers up to 256 KB only; `NFSMW_LAZY_READBACK=0` restores the wait). Result: **0-3 stalls**.
**The frame rate did not rise**: the remaining spikes are GPU-bound heavy scenes. Brightness is
unchanged in screenshots. A first version crashed at startup (it read tag tables that only existed
with guard logging on); fixed.

### GPU work
- **Reflection Refresh** (measured, paused scene): the game redraws all six faces of the car
  reflection cube map (pitch-280 targets) every frame, roughly a fifth of a heavy frame. Drawing 2 of 6
  per frame: median GPU **23.9 -> 21.1 ms** (about -12 %), draws 3,240 -> 3,000. Skipped faces keep the
  previous picture and still run their clears. Not checked while driving.
- **Shadow Resolution**: the game's code uses 1600 in four places (two size getters,
  `sub_82454B58`/`B60`, and the creation in `sub_82454C18` through `sub_82589EA8` and `sub_8245D1B8`).
  All are overridden together. Created at 800x800 and run; the user found it looks decent and lowers
  latency. The speed-up itself was **not timed**.

### Motion blur
- A radial blur that grows toward the screen edges and on the road, skipped for the player's car
  (depth plus a screen area around where the chase camera puts it; the car's colour is also kept out
  of the blur of its surroundings).
- Speed source: the first version read the car's speed from fixed guest addresses. Those are heap
  objects that **move between launches**: a live value in 1 of 5 launches, otherwise `-0.0`, junk, or a
  stale number, which made the blur stick. It is now an estimate from the accelerator and brake the
  game reads: rising while the accelerator is held (about 0-100 km/h in 4 s), falling in about 6 s from
  150 km/h when released, faster under the brake. It costs nothing when stopped or in menus.
- Limits: it follows the pedals, not the car's true speed (no reaction to collisions, nothing during a
  cutscene where the game drives). Rival cars are blurred (see below).
- **Not verified** in play by me; the car mask and the new fade were built after the last in-game test.

## Tried and not kept
- Plain multiplies in the translated shaders (`NFSMW_XMUL=fast`): no speed-up. Switch kept, default exact.
- Cheaper shadow filtering (collapsing the 3x3 taps onto the centre), after removing time drift:
  +1.1 ms +/- 0.5 (corners) and +0.3 ms +/- 0.5 (all): no gain.
- Heat: the machine peaked at 62 C with the CPU at full clock; throttling is not the cause.
- Ambient occlusion and a temporal colour-stability filter: no visible benefit (the flicker went away
  with the filter off). The filter was removed; the occlusion is off by default.
- Anisotropic filtering: in a live A/B inside one still scene, 4x was never slower than 16x, but
  results were inconsistent (two 16x windows at about 50 ms against about 16 ms), and on this setup the
  setting does not change much. Not a lever worth tuning.

## Where the time goes (heavy city frames, 720p)
Profiled frame (timings inflated about 2.5x by the profiler, so read as shares): sun shadow pass about
18 %, reflection cube faces about 20 %, rear-view mirror about 2 %, main world passes about 35-40 %.
The CPU is mostly idle (79 % across cores; the busiest thread about 38 % of a core): the game is
**GPU-bound**.

Stubbing the family of lit world shaders (a dozen pixel shaders that share a 3x3 shadow filter)
live in a still scene: median GPU **25.9 -> 18.0 ms**, so they account for about **30 %** of the GPU
time. That is an upper bound, not a saving.

## What can still be improved (by the evidence)
1. **Internal resolution** is the biggest lever: 1x ran about 3-4x faster than 1.5x in an early, noisy
   comparison. A user setting, not code.
2. **The lit world shaders** (about 30 %): the cost is not the shadow taps; candidates are their ALU
   work and the translator's branch-per-predicate structure. High effort, gain unproven.
3. **The sun shadow pass** (about 18 %): a lower size is already available (800); cascades are not.
4. **Motion blur**: a true speed source would need the game's own speed code found (not found), and
   excluding rivals needs car pixels tagged while the scene is drawn.
5. **Unexplained run-to-run drift** in GPU time limits benchmarking to large effects.

## Diagnostics left in the code (all off unless their variable is set)
`NFSMW_SHADER_STUB` (live-skip listed pixel shaders, file `/tmp/ao/stub`), `NFSMW_PCF_TEST`
(`/tmp/ao/pcf`), `NFSMW_AF_TEST` (`/tmp/ao/af`), `NFSMW_GUEST_PATCH` (`addr:old=new` word patches
applied once the game's table holds `old`), `NFSMW_XMUL`, `NFSMW_LOG_SPEED`, `NFSMW_LOG_GRADE`,
`NFSMW_LOG_GRADE_CONSTS`, `NFSMW_LOG_GRADE_SENDS`, `NFSMW_FIND_SEND`, `NFSMW_LOG_PARAM_NAMES`,
`NFSMW_LOG_SCENE_PASSES`, `NFSMW_LOG_ALL_PASSES`, `NFSMW_LOG_FRAME_DRAWS`, `NFSMW_DUMP_GLSL`,
`NFSMW_LOG_RAMP_RAW`, `NFSMW_LOG_BUTTONS`.
