# Memory budget (measured baseline)

> **Decision (2026-10-29): the 64 MB version is dropped; the only target is 128 MB.**
> What is fixed on Xbox (kernel, the XBE's code and BSS, framebuffers and GPU buffers) takes about 36 MB,
> so 64 MB would leave ~28 MB for a map's data, and a large map such as m1l1 already uses
> ~75 MB after all the cuts. The 64 MB sections below are kept as history.

## Measured state on Xbox (128 MB, Cerbios BIOS, 2026-10-28)

| Map | Free after loading | State |
|---|---:|---|
| training | ~11 MB | playable (controls and weapons tested) |
| m1l1 | ~17 MB | playable |
| m4l3 (the worst) | ~6.1 MB | playable; saves and loads games (~3.3 MB free after the truck scene) |

Cuts applied relative to OpenMoHAA on PC (all under `#ifdef NXDK`):

| What | How | Approximate saving |
|---|---|---|
| Sound | 22 kHz maximum + IMA ADPCM 4:1 in memory (`xbox_ima4.c`, mixer in `xbox_al.c`) | 30 → 4 MB |
| Textures | DXT1/DXT5 (compressor in pbgl `dxt.c`), lightmaps in DXT1, binary alpha in DXT1, 256 px maximum for world/models (`r_xbox_maxtexsize`) | 33 → 17 MB |
| Animations | One block per animation; 16-bit rotations (x, y, z; w is rebuilt) and 16-bit positions with a per-channel scale (`Skeletor_XboxPackAnim`) | 20 → ~15 MB (in m4l3: rotations 5.0 → 4.2 MB, positions 1.5 → 1.0 MB) |
| Terrain | `ter_maxtris` 8192 | 2.5 MB |
| Dynamic lights | `r_fastdlights` always (no copy of the lightmap lump) | ~2 MB |
| BSS and buffers | LZ77 and video on the heap, renderer backend at 640x480, pbgl push buffer and vertices at 1 MB | ~4 MB |
| Saving | Games saved in 256 KB chunks, uncompressed | avoids 4-6 MB peaks |
| Save thumbnail | pbgl did not return `GL_PACK_ALIGNMENT` and `RB_ReadPixels` reserved a random size (~4 MB) | avoids the failure after an autosave |
| `playmp3` | Streamed, like the music | avoids a 4+ MB peak |
| AI navigation | Exact size of the node block (upstream reserved 4 times the children) | 3.8 → 1.3 MB in m4l3 |
| MP3 dialogue | Converted on load to 22 kHz mono IMA ADPCM, decoding in chunks | no whole PCM and no copy in OpenAL |
| Streamed saving | Only the last 2 chunks of 256 KB stay in memory; the rest goes to the file (sizes are patched with `FS_Seek`) | ~1.5 MB per save |
| cgame state on save | The buffer grows by 25% instead of doubling | ~0.5 MB of peak |
| Map change | The previous map's images are freed when loading starts, not when it ends (`R_Xbox_FreeLevelImages`) | ~7 MB of peak in m1l1 → m1l2a |
| Briefing slides | DXT1 (54 images of 512x512 at 32 bits) | ~13 MB per briefing → ~2 MB |
| Single-player BSS | Temporary connection `client_t` on the heap; bans, server queries and network statistics reduced to one element | ~830 KB |

Measured on 2026-10-23 with OpenMoHAA v0.82.1 **x86** (32-bit pointers, as on Xbox), with the data from the original CD
plus patch 1.11. All 35 single-player maps were loaded and `meminfo` and `imagelist` were run after about 4000 frames.

Raw data: [memory-pc.csv](memory-pc.csv), [textures-projection.csv](textures-projection.csv),
[budget-xbox-64mb.csv](budget-xbox-64mb.csv), [budget-xbox-128mb.csv](budget-xbox-128mb.csv). To regenerate them:

```powershell
.\scripts\measure-maps.ps1                 # ~40 min, 35 maps
python scripts/analyze-memory.py docs      # texture projection
python scripts/budget.py docs --ram 64     # per-map budget (64 or 128)
```

## Memory pools on PC (MB, 35 maps)

| Pool | min | mean | max | worst map | What it is |
|---|---:|---:|---:|---|---|
| sound | 21.8 | 26.2 | 30.0 | m4l2 | Whole WAVs in 16-bit PCM |
| skeletor | 5.0 | 18.3 | 23.8 | m3l1a | Skeletal models and animations (`.skd/.skc`) |
| static renderer | 4.8 | 9.5 | 13.4 | m6l1a | BSP geometry for rendering |
| game | 7.6 | 9.4 | 13.8 | m3l3 | Entities, AI, script VM |
| tiki | 3.4 | 7.4 | 11.7 | m3l1a | TIKI definitions (models, anims, sounds) |
| static | 2.1 | 4.1 | 6.8 | m6l1a | Collision (CM) and others |
| general | 4.4 | 4.4 | 4.5 | | |
| renderer | 3.7 | 3.7 | 3.7 | | |
| cgame | 0.8 | 0.8 | 0.8 | | |
| **pool total** | 64.1 | 83.9 | 99.4 | m4l3 | |
| RGBA8 textures (PC) | 13.2 | 26.0 | 37.9 | m4l3 | |
| DXT textures (Xbox projection) | 4.3 | 7.7 | 9.8 | m3l3 | Lightmaps and UI uncompressed |

Textures are **not** the problem: with DXT1/DXT5 the worst map takes about 10 MB, and lowering the resolution
hardly helps, because most of them are already 256 px or less.

## Budget on Xbox (64 MB)

Estimated fixed costs: kernel 1.9 (measured) + XBE code 7 + framebuffers 3.6 + push buffer 1 + margin 3
= **16.5 MB → 47.5 MB left for data**.

Reduction factors (estimated, see `scripts/budget.py`):

| Pool | Moderate | Aggressive | Technique |
|---|---:|---:|---|
| sound | ×0.25 | ×0.125 | Xbox ADPCM (decoded in hardware); aggressive also uses 11 kHz for effects |
| skeletor | ×0.60 | ×0.40 | Quantized animations + LRU cache |
| static renderer | ×0.75 | ×0.60 | Compact vertices, 16-bit indices |
| tiki / game / static | ×0.85–0.9 | ×0.7–0.8 | Data compaction |

| Scenario | Maps that fit | Worst map |
|---|---:|---|
| Moderate | 2 / 35 | m4l3: 71.1 MB (23.6 MB short) |
| Aggressive | 20 / 35 | m4l3: 55.8 MB (8.3 MB short) |
| 128 MB console (moderate) | 35 / 35 | m4l3: 87.6 MB in total |

## Conclusions

1. **64 MB is viable, but very tight.** Even with the aggressive reduction, 15 maps
   are between 0.5 and 8.3 MB short. The largest are m4l3, m3l3, m6l2a, m1l2a, m3l2 and m6l3a.
2. The two levers that matter are **sound** (from 30 to 4–8 MB) and **skeletor** (from 24 to 10 MB).
   They are the first things to attack and really measure.
3. Levers to close the rest: streamed dialogue (already MP3 on disc),
   per-map unloading of unused TIKIs and animations, less `game` memory (preallocated pools)
   and a smaller XBE (no multiplayer, GameSpy or bots).
4. Alternative: a 128 MB version without cuts (it already fits), plus targeted cuts
   in the 15 large maps for the 64 MB version.

## Correction (2026-10-23): the XBE's static memory

The budget above assumed about 7 MB for the binary. The real XBE takes **31.1 MB** in memory:
5.5 MB of code, 1.0 MB of read-only data and 24.6 MB of static data (BSS). Those 24.6 MB
are what remains after a first cut; they were 33.1 MB before. The engine, like Quake 3, reserves
huge static arrays: client state, cinematics, collision, fonts and effects.

With the measured figure (`scripts/budget.py` already uses it):

| Console | Available for data | Moderate | Aggressive |
|---|---:|---:|---:|
| 64 MB | 23.4 MB | 0 / 35 | 0 / 35 (the worst is 32.4 MB short) |
| 128 MB | 87.4 MB | 35 / 35 | 35 / 35 (31.6 MB to spare in the worst) |

For the 64 MB version, slimming the BSS becomes the first priority, along with sound and
skeletor. The largest remaining: the client's `clc`/`cl`/`cls` (~5 MB), `cin` (cinematics, 2.3 MB),
cgame's `cg_entities`/`cg` (~2.9 MB), the renderer's `backEnd`/`tr` (~2.7 MB) and `TIKI_Skel_Bones` (1.2 MB).
Much of this can move to dynamic memory, reserved only when needed, or be shrunk for
single player.

## Bugs found in OpenMoHAA

- `R_CountTextureMemory` (renderergl1/tr_image.c) uses `total_bytes =` instead of `+=`,
  so `meminfo` only reports the last texture. That is why the CSV uses the data from `imagelist`.

## Performance (m1l1, truck ride, 128 MB)

Measured with `output/mohaa-perf.iso` (mean of `PERF` lines 5 to 25) and with the sampler
`xbox/mohaa/tools/gdb-sampler.py`, which reads EIP through xemu's GDB stub (`run-xemu.ps1 -GdbStub`).
A test on the real console felt about as fast as xemu; there are no console figures yet.

| Change | ms per frame |
|---|---:|
| Start | ~180 |
| Vertex ring at 2 MB, `r_fastentlight 1` | ~82 |
| Our own `memcpy`/`memset`, GPU notifications every 32 KB, MP3 at half rate, `high` profile | 85.0 (first reliable mean) |
| Audio thread every 10 ms, bone table, `-fno-math-errno` | 80.0 |
| SSE floating point | 75.8 |
| SSE skinning | 69.0 |
| Normals only if the shader uses them (with `r_fastentlight`) | 59.1 |
| Fast path for single-weight vertices | 58.7 |
| `fabs`/`sqrt`/`memcpy`... as instructions (nxdk compiles with `-fno-builtin`) | 44-49 (~21-22 fps; the same binary varies ±4 ms between runs) |

PC settings lowered to the game's `high` profile (OpenMoHAA ships values above it):
`r_lodscale`/`r_lodcap` 0.55, `r_subdivisions` 4, `ter_error` 9, `ter_maxlod` 4, `cg_effectdetail` 0.8.

m1l1 load (from "Server: m1l1" to `CL_InitCGame`, with the profiler attached): 51.5 s -> 34.4 s. The main changes: our own `fread` (pdclib did one `ReadFile` per KB), TIKI without Huffman, table-driven Huffman, loading screen 4 times per second, no CRC in the pk3s. About 44% is still in the kernel (disc reads) and about 10% compressing textures to DXT.

Saving (m1l2a): the game stalled 5-6 s per save, with 89% of the time in the kernel. pdclib's `fwrite` did one `WriteFile` per KB, each growing the file; `xbox_fwrite` writes big blocks directly. Now under 0.5 s (autosave ~0.4 s, manual save ~0.5 s, including the thumbnail). A save was loaded back to check the file.

Sky boxes (`env/*`) stay at 512 px in DXT1, outside the `r_xbox_maxtexsize` cap: at 256 the stars became blotches and the DXT blocks showed as a mosaic. It costs ~0.6 MB per map that has a sky. The moving clouds in the night maps are the shader's own (`tcMod scroll` in `textures/sky/africanight`).

Dark shapes in m1l1's sky (2026-11-05): they were the truck path helpers (`gmctruckpath_opening.tik`, `truck2path_opening.tik`), one triangle up to ~3800 units across that the script never hides. `TIKI_LoadTikiModel` leaves surfaces named `material<n>` without a shader and the renderer drew them with shader 0, the error shader. `R_AddSkelSurfaces` now skips surfaces with an empty shader name.

That was only part of it. Most of the shapes were polygons of the cloud layer that never reached the screen. The sky is drawn with `glDepthRange(1, 1)`, and pbgl mapped that zero range to one depth value with a clip window of +/-4 units; a float has 1-2 units of precision at that depth, so rounding clipped pieces of the big triangles. pbgl no longer clips the zero range. Measured in a 1100 frame scan of m1l1's town with opaque clouds over a magenta clear: 33 frames with holes before, 0 after. Turning off culling for the sky made no difference (36 frames).

Streamed music and ambience (2026-11-04): 93 MP3s converted to IMA ADPCM (`tools/convert_music.py`, played by `xbox_imastream.c`; 229 MB vs 158 MB of MP3, and the MP3s stay as a fallback). Alternating runs, 4 each: 51.2 ms with MP3, 48.0 ms with IMA, about 3.2 ms. SNR against the decoded MP3: about 34 dB.

Skeletal model cull (2026-11-04): upstream's `R_AddSkelSurfaces` only counted its sphere test, and `R_CullSkelModel` always answered "visible", so every model got bones, skinning and draw calls even behind the camera. Now skipped, with a margin (radius x 1.5 + 48), never for first person models or a radius with no active animation. Alternating runs, 3 each: 38.0 -> 32.2 ms; draws 232 -> 137, skinned surfaces 186 -> 80. A temporary check tested the bones of every culled model against the view in m1l1, m1l2a, m4l3 and m5l1a: none was visible.

GPU wait moved (2026-11-04): `pbgl_swap_buffers` no longer waits for the GPU; `pbgl_frame_sync` does it before the next frame's first draw, texture change or read, so the game and cgame run while the GPU finishes. No added latency. Alternating runs, 2 each: 35.8 -> 34.0 ms, gpu wait 1.7 -> 0.2 ms.

Flicker from that change (2026-11-05): with the GPU still working at the swap, a flip can miss the vblank the swap counted on, and pbkit's rotation through 3 buffers then lands on the buffer still on screen. Frames quicker than the refresh showed while being drawn: in m1l2a looking at the sky, 14 of 40 captures had no HUD, some only the sky. `pbgl_frame_sync` now waits for the buffer it will draw to to leave the screen (it reads `PCRTC_START`), and `glClear` syncs too. Same view: 0 of 60, at 60 fps. In the truck scene the wait is about 0 ms; a first version of the fix waited in the swap and cost up to 8 ms per frame.

CPU batch (2026-11-05): binary search for animation key frames (they are dense and in order: `Compress` returns false), box rejects before brush and patch traces, plane tests worked out when needed in point traces through patches, box reject for grid cells in marks, SSE `myGlMultMatrix` and `memcpy`, non temporal copy into the vertex ring, one copy for the two sets of texture coordinates, 16 bit indices in the push buffer, `glDrawRangeElements` instead of scanning indices, one color for grid-lit models, a plain skinning loop for surfaces without morphs. Alternating runs, 2 each: 27.0 -> 24.6 ms; with the flicker fix, 25.1 and 25.8 ms against 27.5 and 32.2 ms (that one with the host busy). In the profile, collision went from 9.3% to 4.6%, and memory and string functions from 5.4% to 1.7%. Now skinning is 23%, the renderer's front end 11.9%, pbgl 10.9%, bones 10.8%. Not measured on a console.

Alignment (2026-11-05): `Q_ALIGN` is empty with nxdk's compiler (`q_shared.h` defines it only for `__GNUC__`), and `TIKI_Skel_Bones` declared no alignment, so the SSE code worked by luck until a layout change moved `tess` to 12 bytes past a multiple of 16 and a `movaps` crashed. `tr_local.h` now defines `Q_ALIGN` for the renderer, `tr_model.cpp` checks it at compile time, and the bone table is declared aligned.

Level change crash (2026-11-05): going from m1l1 to m1l2a stopped with "Z_Free: freed a pointer without ZONEID". `Skeletor_XboxPackAnim` puts all the channels of an animation in one block, and a position channel starts with its scale: when the first channel was one, `DeallocAnimData` freed the block 4 bytes past its start. It only shows when the animation cache is emptied, at the first level change. Reproduced and checked in xemu with `gamemap m1l2a` after loading m1l1; `Z_Free`'s error now gives the caller's address.

Reading a crash in xemu: the serial log fills with a 973 byte block repeating without end that starts with `0000`, a kernel debugger state change (the normal block at boot starts the same way, so go by the log's size). From its start: +0x28 EIP, +0x30 exception code, +0xDC the instructions at EIP, +0x194 the registers from Edi on. Look EIP up in `build/main.map`.

Marks (2026-11-05): temporary marks are rebuilt every frame; triangles entirely behind a bounding plane are now dropped before the normal test, which no longer needs a square root. Same result. In the profile, marks plus `VectorNormalize` went from 4.3% to under 2% of the CPU; the timed runs were too noisy to show it in the total.

Boot (2026-11-05): listing the paks' directories took a seek and several small reads per file. A 256 KB read window during `FS_LoadZipFile` (`qcommon/ioapi.c`) took the time until "Current search path" from 12.3 s to 7.8 s, and the whole boot in xemu from 18.0 s to 13.2 s (one run each). About 6 s of it comes before the game's first log line.

Free memory with the menus open: `m4l3` showed 1212 KB free after opening the Options, Advanced and Audio menus during a game, against about 3.3 MB measured earlier without them. It is worth watching.

xemu's speed depends heavily on the host: the same binary gave 35.8 and 47 ms on the same day.

Tried and removed (2026-11-04), measured gain none: real `glLockArrays` (only 3-4 of ~170 position copies per frame could be reused, nearly everything is one pass) and `-O3` on the hottest files (48.0 ms with it, 47.1 ms without). Same binary, same day, earlier runs varied 48-57 ms: compare builds only with alternating runs.

`xbox_perf 2` (`mohaa-perf.iso`) draws the frame times on screen, to read them on a console. Its panel covers the top right of the picture (about 300 x 80 of the 640 x 480), so compare scenes without it. xemu's menu bar, which appears when the mouse moves, covers the top 10 rows.

GPU skinning dropped for now: ~29,000 vertices per frame, an estimated gain of ~10 ms in exchange for duplicating meshes in memory and rewriting fog, textures and lighting in vertex shaders.
