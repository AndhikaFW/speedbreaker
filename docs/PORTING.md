# Carrying this fork onto a new SpeedBreaker release

The fork is upstream **v0.1.4** plus its own commits (see `docs/ASAHI_TUNING.md` for what they do).
Two scripts do the mechanical work:

| Script | Job |
| --- | --- |
| `scripts/fork/update-from-upstream.sh [tag] [--dry-run] [--push]` | Fetch upstream, forecast conflicts, rebase the fork's commits onto the new release in a branch `port/<tag>`. |
| `scripts/fork/check-fork-hooks.sh [ppc-dir] [xex]` | Confirm the guest functions this fork hooks are still what the hooks expect. |

## Steps

1. `scripts/fork/update-from-upstream.sh --dry-run` shows the newest upstream tag, the fork commits that will be
   carried, the files both sides changed, and a warning if upstream changed the recompilation config.
   It prints "nothing to port" when upstream has no newer release.
2. `scripts/fork/update-from-upstream.sh` makes `port/<tag>` and rebases. On a conflict it stops: fix the files,
   `git add`, `git rebase --continue`. `main` is never touched.
3. Regenerate `ppc/` if upstream changed the recompilation inputs or tools (follow upstream's own instructions),
   then run `scripts/fork/check-fork-hooks.sh`.
4. Rebuild (the order matters):
   ```
   scripts/build_ffmpeg_xma.sh tools/ffmpeg-src/ffmpeg-7.1.1.tar.xz tools/ffmpeg-xma
   cmake -S . -B build/main        # re-run after a system update: the SDL3 path can change
   ninja -j2 -C build/main SpeedBreaker   # -j2: bigger values ran the 7.5 GB machine out of memory
   ```
   If upstream moved the FFmpeg version, `patches/ffmpeg/0002-xma-one-stream-per-context.patch` may need a rebase
   too (it edits the XMA decoder: `XMA_MAX_STREAMS 8 -> 1`).
5. Test (checklist below), then `git push -u origin port/<tag>`, and when satisfied fast-forward `main` to it.
   (`--push` on the script pushes the branch for you.)

## Where conflicts usually happen

* `runtime/gpu/renderer.cpp`: the biggest edit (native tone/grade shader injection, motion blur, reflection refresh,
  pool sizing, diagnostics). Re-apply by intent, not by line.
* `runtime/user/settings.cpp` / `settings.h`: new settings. The table order must match the enum order
  (a `static_assert` fails the build otherwise).
* `runtime/video/presenter.cpp`, `display_gamma.cpp`, `display_tone.h`.
* `runtime/kernel/write_watch.cpp` (lazy readback) and `runtime/hid/input.cpp/.h` (trigger state).
* `runtime/gpu/shader_translator.cpp` (`NFSMW_XMUL`).

New files (`runtime/game/colour_grade.cpp`, the FFmpeg patch, `docs/`, `scripts/fork/`) cannot conflict;
list `colour_grade.cpp` in the build file if upstream restructures CMake sources.

## Guest-function hooks

`runtime/game/colour_grade.cpp` overrides recompiled functions of the supported `default.xex`
(SHA-256 `aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c`). The addresses are only valid for it.

| Function | Role | Fingerprint checked |
| --- | --- | --- |
| `sub_82449820` | Sends the colour-grade constant block. Hooked on `lr == 0x82223CB0` | `sub_82223BB8` calls it four times |
| `sub_824491F8` | Sends the bloom scale. `lr == 0x8222441C` | a call with that return address exists |
| `sub_82448FE0` | Parameter-name logger (diagnostic only) | exists |
| `sub_82458C20` | Rich Blacks ramp exponent | exists |
| `sub_82454B58` / `B60` | Shadow-map size getters | body is `li r3,1600` |
| `sub_82589EA8`, `sub_8245D1B8` | Shadow size at `lr` `0x82454CB8` / `0x82454C7C` | `sub_82454C18` has both return addresses |

If upstream supports a different executable, the check prints FAIL and every row must be re-found:

### Re-finding a hook
* Grade block: search the generated `ppc/` for the function that writes the four cubic-gain `vec4` constants
  (gains about 1.403 : 1.237 : 0.412 in the original, orange-tinted data) and find the call that sends them;
  hook the sender, not the function that computes the block (it recomputes before sending).
* Shadow size: find the two tiny functions that return the shadow resolution (`li r3,<size>; blr`).
* Use `NFSMW_LOG_GRADE*` and the logging switches listed in `docs/ASAHI_TUNING.md` to confirm the new addresses.
  `NFSMW_GUEST_PATCH=addr:old=new` can test a value change without a rebuild.

## Test checklist
1. Menu and a race start: no orange cast; Rich Blacks visible (try `NFSMW_RICH_BLACKS=0` to compare).
2. A cutscene: the tint must not return (two shader variants exist).
3. Motion blur: builds up gradually from standstill, fades on releasing the throttle, car body stays sharp.
4. Shadow size and reflection refresh take effect (`NFSMW_SHADOW_SIZE`, `NFSMW_REFLECTION_REFRESH=every_3`; enum
   settings take names, not numbers).
5. Memory (`/proc/<pid>/status` VmRSS) and frame pacing in a still race scene, compared by toggling effects live in
   one scene because separate runs drift by up to about 18 %.
6. Do not run the game while building; do not commit `game/`, `ppc/`, `tools/` or `build/`.
