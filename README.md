# Crash Bandicoot 3: Warped Old Nintendo 3DS XL

Target: SCUS_942.44; native ARM11/PICA200 port. Main starts original title archive3C and executes its scripts; opcode48 and presentation remain unfinished. Gameplay cutover, physical O3DS and105% acceptance remain incomplete.

## Master Checklist

### Current integration batch - 2026-10-23

Work order: complete bindings in `main.c`; no new standalone engine modules or isolated tests before live cutover.

- [x] QA verification snapshot
- [x] Connect resumable selector13/F00 through actual19ACC search and1E42C mode3 script handoff
- [x] Bind GOOL sound41/42 to native3780C/378F8 through actor/factory providers
- [x] Add `native_runtime.c/.h` with actual eight-root world/object scheduling and fixed native timeline, plus...
- [x] Add concrete `gool_native_zone_retirement.c/.h`
- [x] Add original1FAC8 angle/motion controller with wrapped fixed-point frame-factor arithmetic
- [x] Git baseline33e0c32 captures the pre-cutover workspace; README completed items compacted to120 chars.
- [x] Main owns native globals,97-slot factory,HID,resource/zone jobs and creation/retirement callbacks.
- [x] Keep failed loads visible with stage/code/service logs; enter gameplay only after a native world update completes.
- [x] Reproduce missing emulator SD assets; install original/runtime banks and trace the next activation fault.
- [x] Bind22C9C/22DA0 to retained pages, cached native imports and real async completion/cancellation.
- [x] Bind21E60/3505C zone state,12EAC resource drain and24330/2437C native camera initialization.
- [x] Bind28740 camera/path sampling;192 MIPS comparisons cover fractional position,angles and auxiliary output.
- [x] Bind exact-time mode3 scalars,25C0C focal lookup and4F724 to the active PICA projection.
- [x] Restore21084 world flags and16CAC frame flags so native actor logic/render gates actually run.
- [x] Start original title archive3C; bind2FCA0 new-game reset and resumable3F child creation in main.
- [x] Bind opcode40 entry/texture loads to stopped dispatch, asynchronous residency and cancellation.
- [ ] Complete opcode40 capacity/readiness queries and original release/page-counter semantics.
- [ ] Bind opcode48/37A00 display-event queue27D6C, its consumers and original title actor presentation.
- [ ] Native gameplay cutover: complete SLST deltas,timed interpolation,player-entry,physics and presentation.
- [ ] Bind real admitted actors to model_stream/model_present; native transform/presentation callbacks remain unresolved.
- [x] Bind original1AD68 neighboring/current-sample activation into both215E8 traversal branches,...

- [x] Current work order (2026-10-23)
- [ ] Added native1AA98 eight-root postorder retirement/predicate and2157C exit cursors, registry/bank/player ownership adapter, originalFFFFFFFF zone sentinel handling and direct1DB38 engine signal entry. Direct delivery preserves native argument addresses, nested handoffs and actual tick ownership without fabricating an opcode caller. Both ARM variants compile and111/111 registered host checks pass. Concrete retirement/delivery composition is present; actual stop-audio backend and main activation remain incomplete.
- [ ] Added routed direct-engine delivery:resume actual nested SEND/STATE work before the parent1DB38 handler continues; preserve dedicated router/delivery identity and deepest-first cleanup. Added original33000 spatial-mode selection plus33050 reset/control entry points using actual engine level reads. Both ARM variants compile; native voice regression passes320 reset and2912 control cases. Full GOOL sound/INST/ADSR providers remain incomplete.
- [x] Translate original1AC74/1ACAC activation records in `gool_native_activation.c/.h`
- [x] Translate original215E8..21E5C zone-switch control flow in `gool_native_zone_transition.c/.h`
- [x] Phase2 source fixes
- [x] Bound ordered presentation to24999 aggregate vertices across queue drains, including both opaque/STP...
- [x] Implement selector15 packed nine-byte record reads/writes and original no-op return1 branches
- [ ] Complete original activation, world scheduling, transition215E8, retirement1AA98, F00 resumable handoff, motion/collision and audio providers before replacing prototype gameplay. Neither zero-filled renderer context nor idle cursors constitute main binding.
- [x] Correct GPU retirement
- [x] Write `model_packet_plan.c`
- [x] Write `model_present.c/.h`
- [ ] Bind ordered presentation and drain/retirement to the actual main frame, enforce aggregate frame vertices and FCRAM/VRAM limits, and verify full-game rendering/105% behavior on O3DS.
- [x] Connect decoded DMA triangles to the PICA recording queue with GPU signed11 screen coordinates, final...
- [ ] Implement ordered packet frame ownership, per-command material provenance, E1 state translation and opaque/STP passes; bind submission to main and verify PS1 raster/blend parity on O3DS.
- [x] Write `model_packets.c/.h`
- [ ] Connect ordered packet decoding to PICA texture residency, draw-state translation and main frame submission; verify PS1 blend/mask behavior and O3DS limits.
- [x] Write `model_stream.c/.h`
- [ ] Bind model stream controls and completed arena/FIFO publication to actual main frame ownership; interpret native ordered primitives for PICA presentation and verify full-game behavior on O3DS.
- [x] Capture command-point packed vertex inputs, including native LOAD_VERTEX scratch140/144 reads...
- [ ] Verify cache-aliasing and strip-carry behavior against original command streams; integrate retained command/projection/emission owners into the main renderer.
- [x] Write native mode1 depth fading and textured emission from original3FAC8..3FC14
- [ ] Verify mode1 native scratch/color/primitive behavior, connect primitive interpretation to PICA and the main frame owner, then complete full-game and O3DS hardware acceptance.
- [x] Add mode6 native signed(command<<6)>0 selection of actual F4/F8 depth bases, F0 publication and ordinary...
- [x] Add native mode3/4 normal primitive tails and retained emitter binding
- [x] Extend native emission with mode2 original3EAF4..3EBD4
- [x] Write `model_emit.c/.h`
- [x] Write `gool_native_primitive.c/.h` from original40B10..40BB8/3FC18
- [x] Connect command streaming projection
- [x] Write `model_visibility.c/.h` for original3CD38..3CDA4
- [x] Write `model_commands.c/.h`
- [x] Reconcile native UV convention against original3DC08..3DC44
- [x] Write native3CCDC..3CD34 animated-UV resolver
- [x] Correct attribute-only native3CDD4..3CE1C cache selection
- [x] Preserve STORED_COLORS attribute indices separately from raw TGEO words
- [x] Write `gool_native_shading.c/.h` from native3DAF4..3DC04/3E920
- [x] Write `model_clip.c/.h`
- [x] Write `model_project.c/.h`
- [x] Bind native render preparation to mapped source state
- [x] Write `gool_native_render.c/.h` memory-result preparation for all12 unique8005C1F0 handlers
- [x] Extend `gool_native_matrix.c/.h` with original41EF0 packed auxiliary/current rotation composition and...
- [x] Write `model_actor.c/.h`
- [x] Write `model_pose.c/.h` from original3F6D4..3F728/3E30C..3E344
- [x] Write `model_draw.c/.h`
- [x] Write `model_texture_cache.c/.h`
- [x] Write `model_gpu_texture.c/.h`
- [x] Write TGEO material accessors on the retained topology owner
- [x] Write `model_triangles.c/.h`
- [x] Write `model_vertices.c/.h`
- [x] Consolidate technical notes and remove secondary Markdown files (2026-10-21).
- [x] Write native creation orchestration, concrete hierarchy search and explicit readonly/mutable...
- [x] Write NSF-backed GOOL item/source resolution, retained fixed behavior bank and asynchronous...
- [x] Write retained native behavior/class-load mappings, source-resolved native PC/frame pointers, mutable...
- [ ] Bind native object addresses to VM object/stack/code handles, connect original activation/dispatch, finish unsupported services/opcodes and replace prototype gameplay. Both ARM variants compile and111/111 registered host checks pass. Main native bootstrap and provider binding remain incomplete.
- [x] Write original `0x8002049C..0x800205F4` pool bootstrap
- [x] Write `gool_state_native.c/.h` for original `0x8001D2D4..0x8001D584`
- [x] Write fixed262144-byte mutable native constant storage for loader EID-to-handle fixups
- [x] Write packed-frame import, raw native actor attachment and quiescent frame/flow synchronization
- [x] Write native operand-address provider and opcodes13/14/16/26, including STACK address pops, literal...
- [x] Write `gool_native_dispatch.c/.h`
- [ ] Finish original state-entry/activation ownership and scheduler integration using the new native bridge. Synchronized frame entry now imports stopped raw SP/FP and saves raw PC, including NULL during state setup, without fetching code or overwriting it with a stale VM PC. State services must use stopped actor/frame ownership and explicit flow import; raw register stores preserve native cached-dispatch semantics. Final checks must cover shared mapping lifetimes, host harness dependency updates, loading/shutdown order, original execution parity and physical memory/performance.
- [x] Write `zdat_native.c/.h` direct-index32710 mode2 selection with signed-key search, database+8 fallback,...
- [x] Write `zdat_resources.c/.h`
- [x] Write `zdat_path_native.c/.h` 20670 path
- [x] Write resumable initializer begin/step and bind its retained cursor into creation
- [x] Write concrete `gool_native_state.c/.h` owner
- [x] Write fixed97-slot `gool_native_factory`
- [x] Write original1DB38 native SEND cursor
- [x] Write fixed-depth `gool_native_router`
- [x] Replace single pending factory state storage with97 fixed shared request records
- [x] Write `gool_native_update.c/.h` for original1D82C..1D8D8 SCRIPT stage
- [x] Write `gool_native_mode.c/.h` for original1CA68..1CD24 post-script mode timers and light composition
- [x] Write native presentation admission in `gool_world.c/.h` for1D910..1DB18
- [x] Write concrete `gool_native_represent.c/.h` for1B630
- [x] Write opcode1C native39378 wrapper binding
- [x] Extend35D08 geometry with selectors1/6 vector distances (native approximate3D/XZ and wrapped square-root...
- [x] Write geometry selector5 normalization from35E94..36184
- [x] Write selector13 nearest-object search with original19ACC predicate
- [x] Write `gool_native_call.c/.h` for original1E42C script invocation
- [x] Write `zdat_lookup.c/.h` for original317C0/31868 half-open boxes and28D8C reverse flagged neighbor...
- [x] Write original3188C transformed point-box predicate in `gool_native_bounds.c/.h`
- [x] Write native2D8C8 reset/2D85C pop and2D0E8 non-SVTX default bounds
- [x] Write original3148C AABB transform and18DD4 wrapped integer matrix-vector arithmetic, including native...
- [x] Extend native bounds to nonoriented SVTX records through2D830
- [x] Write readonly native SVTX/TGEO model bank and bounded item-address API using shared NSF copy/lease...
- [x] Add retained TGEO binding atauxiliary+28 and native1B524 paired SVTX selection from real E8/EC/model...
- [x] Write protected-player1B1B0..1B300 bounds from the actual retained SVTX frame
- [x] Write `gool_native_model_prepare.c/.h`
- [ ] Bind creation to real resource/VM/path/retirement services and original zone activation; current main still uses prototype gameplay.
- [x] Phase2 verification (2026-10-23)
- [ ] CORE_MEM: asynchronous 64 KiB NSF/NSD pages, bounded pools, durable save recovery at `sdmc:/3ds/crash3/save.dat`.
- [ ] RENDER_3D: complete WGEO/TGEO/SVTX/RAWD, materials, animation, near clipping, culling, <25k vertices/frame.
- [x] Write conservative support-plane AABB culling for fallback WGEO using the actual projection/view...
- [ ] COLLISION_CAM: original ZDAT/OCTR collision, spline cameras, Bike/Pura/Jet Ski/Biplane/Sub/Baby T physics.
- [ ] STORY_GOOL: all opcodes/services, world scheduler, crates, five bosses/powers, hub/secret routes, time trials, 25 crystals/45 gems/30 relics, 105% playthrough.
- [ ] UI_CTRL: full bottom-screen HUD/inventory/touch UI and original artwork; top-screen gameplay acceptance.
- [ ] AUDIO_NDSP: original PS1 ADPCM SFX/voices and MIDI/SEP/INST sequencing; current runtime uses synthesis.
- [x] Write `audio_sequence.c/.h`
- [ ] QA_BUILD: both builds, all host regressions, original-data parity, physical O3DS <=64 MiB FCRAM/6 MiB VRAM and measured 60 FPS.

### Retained baseline and native integration facts

The latest complete pre-change regression was93/93. Earlier native initializer checks covered1536 original-instruction cases and both ARM builds; the later interrupted94-check run is not a pass. Phase2 compiles all production translation units in both ARM variants; the final expanded99-check host suite passed, including production emission/cache/presentation and GPU-retirement harnesses. This does not establish a complete native runtime or O3DS acceptance. Historical package hashes describe superseded builds and are omitted.

- Original data inventory:42 archives,2282 raw NSF pages,14778 entries,186126 items,334 texture pages;1717 zones,7761 entities,133728 series,2209337 elements and62811 path points. Resource identity is EID/CID-based, not a name substitution.
- Async NSF storage:4 linear64KiB pages,128KiB encoded staging and8KiB worker stack; generation tickets, distinct request/page borrows, cancellation and joined close. GOOL level bank:20 fixed64KiB slots;42 generated catalogs contain3..18 behaviors per level.
- Original ADPCM/NDSP bridge uses3 linear PCM buffers and10752 bytes per stream. Explicit ADIO EID playback exists; synthesized gameplay audio remains until original GOOL requests/voices/sequences are connected. Release queued PCM and borrowed pages only after stopping NDSP ownership.
- Original creation:`0x80019CE4..0x8001A428`; admission through`0x8001A084`, slot selection through`0x8001A154`, initialization`0x8001A5C8`. Concrete search`0x80019844` uses predicate`0x80019CC0`, skips type2 roots and snapshots siblings. Retirement wrapper`0x8001AA78` forces notify1 into`0x8001A844` even when its caller supplies0.
- Pool bootstrap:`0x8002049C..0x800205F4`,96 slots*`0x23C` and player`0x33C`; roots`0x80068EA0`, free root`0x80068EE0`, current object/tick`0x80068EE8/EC`, player/pool pointers`0x80068E98/9C`. Auxiliary storage:player`0x80069C48`, objects`0x80069CB4+i*0x30`. Native address handles are explicit port mappings, not host-pointer casts.
- Path service`0x80020670..0x80020EFC` implementation now includes selection/interpolation/flags/orientation and geometric projection; final original-execution verification remains pending. Position block`0x800207B0..0x800207F4` uses signed16 components, shift&31 and wrapping32-bit arithmetic. Player creation then overwrites position from its checkpoint; do not replace it with an entity-path spawn.
- Zone database links:`0x80021330..0x80021474`; startup`0x800212A4..0x800212D4` selects a database triple using checkpoint+`0x2E0`; +`0x2E4` remains opaque. NSD directory-end+8 is the level ID in all42 archives despite the inherited first_zone label.
- Adjacency predicate:`0x800214B8..0x80021530`, header+`0x190` count/+`0x194` references; odd neighbors are EIDs, even neighbors resolved entries. Entry initializer`0x80021530..0x8002157C` preserves header+`0x2BC` bit0, otherwise writes`0x80069210=0`,`0x80069218=0`,`0x80069214=0x19000`, then sets bit0. Full transition`0x800215E8` and exit retirement`0x8001AA98` remain open.
- Native unsigned timing source divides by34 at`0x8001D16C`; frame-factor source`0x80061A7C+0x18` clamps at102 in`0x8001AB64`. Units remain unproved; do not substitute presentation delta time. Conservative world side-plane culling and live morphed actor bounds are written; depth clipping and measured O3DS performance remain open.

### Build and verification

Current work order: complete native integration while compiling and testing each supported change. Both ARM variants, GOOL catalog export,113/113 registered host checks and SD packaging/CRC/payload verification pass for this snapshot. Main enters the native boot owner; prototype gameplay update and actor draws are disconnected. Resource/camera/creation, physics, native presentation and full O3DS gameplay remain incomplete. A successful ARM build proves translation/linking; host checks prove only their covered behavior. Neither establishes native gameplay or physical O3DS parity.

Emulator reproduction (2026-10-23): the installed SD folder lacked both `original/` and `runtime/`; restored all84 original files and84 runtime archives/catalogs. The reported `80022DA0` resource-list fault is fixed: main retains original pages by CID, applies native conditional/total pin counts, waits for asynchronous reads and imports cached entries into the existing relocated banks. The 32-page arena costs2097152 bytes; this is a port budget. The regression executes production main helpers against Toad Village's182-entry/25-page list, compares every retained page to the NSF, repeats pin/release across budgets, checks independent native imports and injects worker failure. The reported `80028740` failure is fixed: native camera sampling includes wrapped fixed-point position interpolation,12-bit shortest-angle interpolation, optional auxiliary output and final/empty path behavior. The production main helper matches192 original MIPS traces using explicit database-selector hooks. Exact integral-time mode3 scalar reads now serve native keys1AA/130; other timed intervals remain unsupported. Native25C0C/4F724 publishes focal length into the top-screen projection. Correction: the prior120 Toad Village updates measured hierarchy traversals with zero world flags; actor logic/rendering did not execute. Main now applies original21084 flags0C01FFFF and16CAC restoration. Original11650 starts archive3C; TitOC now calls2FCA0, creates childA0000000 through3F and loads Cr20T through resumable40. Texture residency retains original CPU bytes; GPU upload/presentation remains open. Both emulator variants stop explicitly at childPC212/op48801800: unbound37A00/27D6C display events. Regressions cover384 original2FCA0 traces,192 original188EC/18DA8 camera traces,512 original21084/16CAC flag traces, real texture residency and resource completion/cancellation. Failed native loads retain their diagnostics instead of returning to the prototype hub. Native activation and the first completed world update are required before gameplay. Original cinematics/menu and a playable native level remain unimplemented; this is not a working gameplay release.

Verified artifacts (2026-10-23):

- Embedded3DSX SHA256:`d5a00ceffa3b5da41fd79a0a63c106747cd1e2fcc81055e15b8f02819836d82b`.
- SD3DSX SHA256:`e367cc3dcc10b36fa7e6b7221e292fac3070a741155ed6ce2f1a5c38906bc62f`.
- `dist/crashwarped3ds-prototype-sd.zip`:125424128 bytes; SHA256:`507cb56746505744bfb04d811f8a7a485ca6dee422af493774b946813f4b6006`. This packages the current prototype, not a complete native port.
- ELF static sections: embedded 22365540 bytes, SD 6395884 bytes. These exclude runtime heap/linear buffers/VRAM and do not prove the64MiB/6MiB limits.

With devkitPro/devkitARM/libctru/citro3d tools on PATH:

```sh
make -j4
make LEVELS_FROM_SD=1 -j4
python tools/export_gool_catalog.py
python tests/run_checks.py
python tools/package_sd.py
```

Windows devkitPro shell: `C:/devkitPro/msys2/usr/bin/bash.exe`; source `/etc/profile.d/devkit-env.sh` if present. Host C checks require the installed `ziglang` Python package. SD assets reside beside the executable under `sdmc:/3ds/crashwarped3ds/`; save files use the separate `crash3` directory. Run these commands during integration and repeat after changes affecting their coverage. Hardware execution remains unverified.

### Technical contracts and retained evidence

- PS-X EXE SHA256: `1b93cc563c90966e1d79b750f7d19bc1662859cceeb991775e9e9a22656d71d1`; 333824 bytes, payload starts `0x800`, 331776 bytes mapped to `0x80010000`, entry `0x800489F8`. Raw disc sectors: 2352 bytes, Mode2/Form1.
- NSF pages: 65536 bytes; header `u16 magic, u16 type, u32 CID, u32 count, u32 checksum, u32 offsets[]`; magic `0x1234` raw / `0x1235` compressed. NSD indexes pages. Entry: `u32 magic=0x0100FFFF,EID,type,itemCount,offsets[]`; EIDs use `0-9a-zA-Z_!`. Page types 0 normal/1 TPAG/3 ADIO/4 INST/5 MIDI-SEP.
- Entry types: 1 SVTX, 2 TGEO, 3 WGEO, 4 SLST, 7 ZDAT, 11 GOOL, 12 ADIO, 13 MIDI, 14 INST, 15 VCOL, 17 RAWD, 19 PBAK, 21 VIDO. Parsers and reference sources remain in `tools/`.
- WGEO: origin plus signed 12-bit positions /16; packed XYZ order x/z/y, vertex records 6 bytes, triangles 6 bytes, quads 8 bytes, texture descriptors 12 bytes. Items: header/vertices/triangles/quads/regions/palette/texture animation.
- TGEO header: scale i32[3] at `0x00` /256; texture EIDs `0x0C`; UV count `0x34`, vertex count `0x38`, color count `0x3C`, texture count `0x40`, triangle count `0x44`, animation count `0x48`. UV stride 12: pairs at 0/4/8/10, CLUT at 2, texpage at 6. NEW_TRIANGLE `0x80000000` consumes three flags; source vertices are sparse slots. Final position `(vertexByte + svtxOrigin) * tgeoScale / 8`.
- SVTX: one item/frame; i16 origin XYZ at 0/2/4 divided by32; view offset at 8/10/12; vertex count `0x10`, hitbox count `0x14`, vertex-data offset `0x18`, hitboxes from `0x1C`, stride `0x28`. Trust the vertex-data offset (some frames have extra 3 bytes); uncompressed bytes x/z/y. Hitbox fields flags/center/min/max at 0/4/16/28 remain uncalibrated. TGEO compression reads u16 at `offset ^ 2`, MSB-first signed deltas. Cr10G declares 309 vertices but has 305 compression instructions; unresolved.
- ZDAT header: sky EID `0x004`; world EIDs `0x034 + i*0x30`; info/database/object/other counts `0x184/188/18C/190`; other EIDs `0x194`. Database series count `+0x0C`; series data offset relative to database `+0x0C`, then align4. Original path transform `(offset + point * (1 << shift))*0x100`; do not assume an unproven path-to-world factor.
- RAWD-only files: `05,0D,13,17,19,1F,21,27`; current XZ flattening is an approximation. Mesh header `u16 type=2,count;u32 texEID`, records16 bytes RGB/pad + three xyuv byte tuples. Texture frames32x32 RGBA8888,4096 bytes; grid scroll and animated layers need runtime integration.
- CWT1: 64-byte LE header; counts at12/16/20/24, offsets28/32/36/40, pixel start44,total48,frame count52. Vertices8 floats; indicesu16 relative to batch; batches56 bytes (8 u32 + bounds6 floats); textures32 bytes. RGBA5551, 8x8 Morton tiles, flipped Y, dimensions power-of-two>=8. Limits16 MiB/file,8 MiB geometry,4 MiB texels. Retained ABR metadata does not implement PS1 blending.
- Native GOOL opcode table `0x8005D120`; frame enter/return `0x8001D588/0x8001D5E8`; frames hold flags, PC, packed links relative to object+`0x40`; frame slots0..2 header, locals>=3, negative slots arguments. Member aliases require registered providers; never substitute zero for unknown storage.
- GOOL SEND wrapper `0x80037EDC`, service `0x8001DB38`, RECEIVE `0x800383DC`, SET_STATE `0x80037BEC`, state entry `0x8001D2D4`. Default explicit signal routes always select BASE code + `(raw & 0x3FFF)` when bit`0x8000` is set; bit`0x4000` does not select alternate here. RECEIVE requires mode8 and borrowed engine result; sourceOp3 remains unsupported.
- GOOL token setters `0x80037CD8/0x80037DAC`, gate `0x8001D870`; token `tick_u32 | duration<<24`, consumed before main mode4, pre_run mode3. RESUME entry `0x8001D4B0`, return `0x80039A78`; mode`0x13` return seeds zero token. JALR opcode`0x47` at`0x80039978`; `0x3A` is another service. Code pointer opcode`0x18` at`0x800392B8`, null index`0x3FFF`; ARRAY_READ`0x2A`/WRITE`0x4E` at`0x80039724/754`, address `base + ((uint32(index)>>8)<<2)` with u32 wrap.
- Evidence retained as original bytes/disassembly in `docs/evidence/psx_signal_native.json`, replay tools/tests and original disc. Routine replays use explicitly declared hooks where needed; they do not establish gameplay parity. Memory domain:32 borrowed spans, non-reused handles; delivery pool16 jobs. Neither is an inferred original engine limit.
- Memory estimates from historical extracts: full converted container ~16 MiB; collision VBO reserve up to8867520 bytes (Road Crash), double replacement17735040; CWT max8300352 bytes plus1235456 texels; UI/character/effects2015184 bytes. Totals exclude SDK/heap/NDSP/render targets and are not measured peaks. `linearAlloc` uses linear FCRAM, not the 6 MiB dedicated VRAM. Finish GPU work before releasing buffers/textures.
- Original defects still open: scheduler/activation and engine services; nested signal root replacement/outer receivers; sourceOp3; fixed-point/GTE parity; packed normals misread as RGB; original RAWD/material composition; regenerated CWT brightness data; missing sprites/resources; secret-arrival selectors; original portal provenance/relic thresholds; moving platforms/cameras/vehicles/bosses/cinematics; original audio and complete physical acceptance.

### Detailed acceptance backlog

All inherited acceptance items remain open until production integration and suitable evidence exist. Historical unit-test counts are not completion percentages. Later implementation supersedes stale descriptions; unresolved acceptance stays open.

- [ ] **P0-01**: relic check already precedes camera fallback.
- [ ] **P0-02**: `StoryPortal` identifies 35 entries by camera/slot/type w/ access resolver.
- [ ] **P0-03**: tables + limits use five bosses; basement has five destinations, consumer no longer offers a fictitious sixth button.
- [ ] **P0-04**: hub pad + proximity query same resolver before load.
- [ ] **P0-05**: verify thresholds + hidden-route discovery vs original data.
- [ ] **P0-06**: implement GOOL runtime.
- [ ] **P0-07**: treat packed normals as normals when format/state says so.
- [ ] **P0-08**: top HUD reduced to bazooka reticle; mask, clock, boss health, context, pause moved to bottom screen.
- [ ] **P0-09**: resolve black presentation + get real boot/gameplay image.
- [ ] **CORE-01**: inventory PS1 executable, native routines, formats, engine calls; record what comes from original data vs port reconstruction.
- [ ] **CORE-02**: keep GOOL bytecode, constants, states, tables, entities, links, metadata in versioned runtime format.
- [ ] **CORE-03**: PS1/GTE fixed-point math needed for rotation, translation, projection, saturations, logic math.
- [ ] **CORE-04**: original update order + cadence, timestep, events, behavior at different render rates.
- [ ] **CORE-05**: manage load, level change, death, suspend, resume, exit w/o refs to freed resources.
- [ ] **CORE-06**: engine services GOOL calls: resources, transforms, camera, physics, signals, create/destroy, sound, progress, FX.
- [ ] **CORE-07**: resolve TGEO/SVTX warnings + triangle deltas; keep incident counts visible on export.
- [ ] **CORE-08**: resolve pending sprite pages/regions + cross-file refs via proven identity/provenance.
- [ ] **CORE-09**: document + implement still-undecoded formats/modes used by any level, boss, FX, cinematic.
- [ ] **GOOL-01**: runtime fetch/decode/dispatch w/ PC, registers, vars, base/alt constants, access limits.
- [ ] **GOOL-02**: stack, params, frames, pointers, object refs, aliases w/ original semantics.
- [ ] **GOOL-03**: arithmetic, logic, bit, trig, angle, random ops; verify sign/overflow cases.
- [ ] **GOOL-04**: jumps, conditions, calls, returns, state changes, exec-pointer modification.
- [ ] **GOOL-05**: pre_run/on_signal handlers, suspend, resume; honor original order + duration.
- [ ] **GOOL-06**: geometry selection, frames, UV, mirror, draw format, scale, per-state lighting params.
- [ ] **GOOL-07**: create children, link hierarchies, destroy entities; activate/deactivate on zone enter/exit.
- [ ] **GOOL-08**: signals, collisions, object lookup, interactions, globals, flag persistence.
- [ ] **GOOL-09**: native MIPS instructions + segments used by game; translate services or execute faithfully w/ evidence.
- [ ] **GOOL-10**: per-instance + scheduler resource accounting; avoid frozen, duplicated, wrongly reused objects.
- [ ] **GOOL-11**: replace name-based entity classification w/ class/ID/state + real data.
- [ ] **GOOL-12**: comparative trace of instructions + state changes w/ real on-disk objects; report unsupported opcodes w/ context.
- [ ] **GOOL-13**: retire approximate controllers only when original replacement integrated + tested; no fake successes.
- [ ] **GFX-01**: complete all 42 scenario files + RAWD composition; roads, water, terrain, special modes w/o fake planes.
- [ ] **GFX-02**: load all models: characters, enemies, crates, collectibles, bosses, mounts, vehicles per GOOL.
- [ ] **GFX-03**: all SVTX animations, interpolation, sync, clip changes, mirror, per-entity deformation.
- [ ] **GFX-04**: full sprites + quad lists, billboard, orientation, correct frame; check original Wumpa cadence.
- [ ] **GFX-05**: UV/palette/material animation; first exported frame not enough for water, FX, animated materials.
- [ ] **GFX-06**: Gouraud + normal lighting, LIT/CRYSTAL modes, per-zone/object light, GOOL-modified params.
- [ ] **GFX-07**: PS1 ABR semi-transparency, STP texels, clip/opacity, transparent/additive/subtractive material order.
- [ ] **GFX-08**: near clipping, culling, depth, ordering equivalent to surfaces that relied on the Ordering Table.
- [ ] **GFX-09**: sky, backgrounds, fog, water, darkness, interactive lights, per-level ambient changes.
- [ ] **GFX-10**: character/object shadows + original contact FX, dust, smoke, fragments, explosions, fire, sparks, trails.
- [ ] **GFX-11**: entry/exit portals, rings, Time Twister texture/video remap, original transitions.
- [ ] **GFX-12**: Aku Aku mask: GOOL follow, orientation, golden transform, invincibility, FX; today all three levels use same base model w/ port-specific position.
- [ ] **GFX-13**: crystal, clear/colored gems, relics, clock, extra lives, switches, special crates w/ original look.
- [ ] **GFX-14**: original title UI + graphics, icons, results, holograms, endings, credits.
- [ ] **GFX-15**: free caches + textures on zone change; keep explicit backups to diagnose missing assets; do not accept an object still shown as a cube or disappearing.
- [ ] **GFX-16**: reproducible visual comparison per object family + per level, incl.
- [ ] **WORLD-01**: interpret ZDAT/OCTR/SLST + zone relations w/ original activation + visibility.
- [ ] **WORLD-02**: initial spawn, orientation, arrival point per normal, secret, bonus, death, checkpoint entry.
- [ ] **WORLD-03**: replace own entry chronology w/ original sequence; control pad delivery, camera, FX.
- [ ] **WORLD-04**: original camera paths, rotations, interpolation, triggers, inter-segment links; do not follow speed only.
- [ ] **WORLD-05**: side cameras, chases, vehicles, bosses, bonus, secret routes, bazooka first person.
- [ ] **WORLD-06**: faithful collision of floors, walls, ceilings, slopes, steps, edges, corners, zone bounds.
- [ ] **WORLD-07**: height queries w/ adequate precision; avoid rounding jumps or picking another floor while moving.
- [ ] **WORLD-08**: moving platforms, elevators, belts, transports, character-relative support.
- [ ] **WORLD-09**: water, floods, lava, spikes, traps, projectiles, pits, per-scenario specific obstacles.
- [ ] **WORLD-10**: full restore of entities, cameras, checkpoints on death/restart; today only one checkpoint point kept.
- [ ] **WORLD-11**: bonus, death routes, gem platforms, return to main route, correct persistence of their crates/collectibles.
- [ ] **WORLD-12**: traverse every route w/o false falls, pass-throughs, blocks, early exits, out-of-scenario cameras.
- [ ] **PLAYER-01**: original Crash + Coco states/animations; correct model per character, level, mode.
- [ ] **PLAYER-02**: run, accel, brake, turn, jump, air control cross-checked vs original, not just stable in one test.
- [ ] **PLAYER-03**: normal spin, attack windows, recovery, affected enemies/crates, original FX.
- [ ] **PLAYER-04**: sweep, crouch, crawl, sweep-jump + their collisions/animations.
- [ ] **PLAYER-05**: belly flop + Super Belly Flop w/ original reach, impacts, recovery.
- [ ] **PLAYER-06**: Double Jump w/ correct availability, height, control, animation transition.
- [ ] **PLAYER-07**: Death Tornado Spin: repeat, duration, glide, reach, original activation rules.
- [ ] **PLAYER-08**: Fruit Bazooka: first person, aim, shot, collision, ammo/original rules, animations.
- [ ] **PLAYER-09**: Speed Shoes + combo w/ sweep/jump; resolve conflict between run button and slide attack.
- [ ] **PLAYER-10**: damage, temp immunity, mask loss, invincibility, specific deaths, lives, game over, continue.
- [ ] **PLAYER-11**: interactions + animations for vehicles, mounts, water, bosses; do not always reuse on-foot state.
- [ ] **PLAYER-12**: coherent control mapping on O3DS + equivalent behavior w/ Circle Pad + D-pad.
- [ ] **OBJ-01**: original population per zone/state + spawn conditions; incl.
- [ ] **OBJ-02**: wood, question, fruit/bounce, arrow, slot/content swap, extra life, Aku Aku w/ real logic + animation.
- [ ] **OBJ-03**: TNT: jump/ignite states, count, explosion, chain, damage, cancel/restore on death.
- [ ] **OBJ-04**: Nitro + green detonator: activation, chained explosions, count, original FX.
- [ ] **OBJ-05**: metal, reinforced, outline crates + `!` mechanisms; material + behavior per original ID/state.
- [ ] **OBJ-06**: checkpoints, stacks, repeated bounces, resistance, rewards, count of all crates w/o dupes or unreachable crates.
- [ ] **OBJ-07**: 1, 2, 3 second time crates + their time-trial substitutions, w/ original freeze + accumulation.
- [ ] **OBJ-08**: Wumpa, lives, mask: spawn, motion, pickup, accumulation, conversions, original FX/sounds.
- [ ] **OBJ-09**: crystal + each clear/colored gem, independent conditions + presentation; do not award them for finishing w/o collecting.
- [ ] **OBJ-10**: catalog + behavior of all enemies: medieval, oriental, arabian, egyptian, aquatic, prehistoric, future.
- [ ] **OBJ-11**: fauna + active decorations; do not turn a decorative entity into an enemy via name coincidence.
- [ ] **OBJ-12**: projectiles, traps, destructibles, enemies carried/spawned by other objects.
- [ ] **OBJ-13**: each family vs spin, jump, sweep, flop, bazooka, invincibility, incl.
- [ ] **VEH-01**: motorbike: original road, accel/brake, drift, ramps, traffic/rivals, race positions, victory rules.
- [ ] **VEH-02**: Pura: auto motion, steering, jumps, speed, obstacles, crates, damage, camera, Coco/Pura animations.
- [ ] **VEH-03**: jet ski: buoyancy, turn, ramps, waves, buoys/mines, enemies, platforms, Coco route.
- [ ] **VEH-04**: submarine/underwater mode: aquatic motion, weapons, boost, damage, enemies, enter/exit vehicle.
- [ ] **VEH-05**: biplane: flight, fire, targets, rivals, damage, instruments, victory, per-level gem/time-trial conditions.
- [ ] **VEH-06**: Baby T-Rex + chases: mount, jumps, obstacles, separation/reunion, original animations/cameras.
- [ ] **VEH-07**: mode changes inside a level + restart, checkpoint, secret-route conditions for all vehicles.
- [ ] **STORY-01**: full 3D Time Twister, navigation, five cameras, basement, platforms, selection, original transitions.
- [ ] **STORY-02**: sync portal data across `main.c`, `story.c`, entities; avoid conflicting duplicate tables.
- [ ] **STORY-03**: per-portal visual states, crystals, gems, relics, boss, locked level, alternate routes.
- [ ] **STORY-04**: holograms, heads, voices, dialogue, spawn conditions for Cortex, Uka Uka, N.
- [ ] **STORY-05**: crystal/power delivery + unlocks via real events; do not call campaign done on test flags alone.
- [ ] **STORY-06**: Hang'em High + Future Frenzy secrets, Hot Coco/Eggipus Rex access, return to correct place.
- [ ] **STORY-07**: conditions for five colored gems, extra gems, death routes, all-gems route.
- [ ] **BOSS-01**: Tiny Tiger full: arena, patterns, phases, aux enemies, damage windows, animations, defeat, reward.
- [ ] **BOSS-02**: Dingodile full: obstacles, attacks, barriers, vulnerable phases, defeat, reward.
- [ ] **BOSS-03**: Dr.
- [ ] **BOSS-04**: Dr.
- [ ] **BOSS-05**: Cortex & Uka Uka full: final fight, Aku/Uka interaction, shields, phases, victory, ending access.
- [ ] **STORY-08**: standard ending, full/105% ending, credits, exact conditions, ability to keep playing/load after.
- [ ] **STORY-09**: original percentage formula + requirements; do not conclude 105% just because counters add up.
- [ ] **TIME-01**: clock available only in correct level state; activation, restart, transition to time mode.
- [ ] **TIME-02**: restore objects/enemies + crate substitution per original mode rules.
- [ ] **TIME-03**: time pause per crate, indicators, precision, duration independent of presentation FPS.
- [ ] **TIME-04**: death, quick restart, checkpoints, exit coherent w/ original time trial.
- [ ] **TIME-05**: Sapphire/Gold/Platinum times per level from original source; current table alone is not verification.
- [ ] **TIME-06**: save best time + improve rank w/o duplicating relics or reducing obtained progress.
- [ ] **TIME-07**: original results, prize delivery, previous/new record, correct return to hub.
- [ ] **TIME-08**: one full time trial per entry that allows it, incl.
- [ ] **AV-01**: extract/interpret original banks, instruments, sequences, FX, dialogue, streams w/ verifiable provenance.
- [ ] **AV-02**: PS1 ADPCM decode + equivalent mix of the 24 required SPU voices, envelopes, pitch, volume, loops, FX.
- [ ] **AV-03**: music sequencer + original theme transitions/loops per level, boss, menu, event; retire synthesized BGM.
- [ ] **AV-04**: dialogue + XA/stream audio synced to animations/holograms/cinematics.
- [ ] **AV-05**: NDSP: queues, latency, streaming, panning, priorities, simultaneity w/o crackle or freed-buffer refs.
- [ ] **AV-06**: original logos + startup animations; a screen with their names does not reproduce the sequence.
- [ ] **AV-07**: full opening cinematic: station debris fall, temple, Uka Uka release, w/ sound + sync.
- [ ] **AV-08**: original title + menu w/ animations, transitions, 3DS-adapted interaction.
- [ ] **AV-09**: video/cinematic playback + in-engine sequences, allowed interrupt, correct resume/load.
- [ ] **AV-10**: boss intros, endings, credits, game over, other intermissions used by the game.
- [ ] **AV-11**: listen + sync on console, peak voice load, CPU/RAM budget, no sustained game drop.
- [ ] **UI-01**: bottom set coherent w/ user references: name, progress, Wumpa, crates, lives, inventory, powers.
- [ ] **UI-02**: show Aku Aku state, time trial, boss bottom + readable, w/o losing info when clearing top screen.
- [ ] **UI-03**: bottom Warp Room w/ selected level, progress, gems/relic, clearly distinct accessibility/secret entry.
- [ ] **UI-04**: adapt reference illustrations, icons, frames to 320x240; review size, contrast, real touch targets.
- [ ] **UI-05**: gestures reserved to buttons, camera drag, pad, touch w/o accidental actions on screen change.
- [ ] **UI-06**: pause, settings, restart, return to hub/title, needed confirmations, understandable error/load states.
- [x] **UI-07**
- [ ] **SAVE-01**: serialize all clear/colored gems, routes, secret entries, crystals, ranks, times, powers, story events.
- [ ] **SAVE-02**: versioned schema + legacy save migration, w/o relying on C padding/structure for future compat.
- [ ] **SAVE-03**: real atomicity, CRC, backup, recovery vs no space, SD removal, failed write/close/rename.
- [ ] **SAVE-04**: autosave + manual save/load from Warp Room; avoid incoherent states when loading mid-transition.
- [ ] **SAVE-05**: define + integrate Archive_SaveData for requested installed variant and its relation to `sdmc:/3ds/crash3/save.dat`.
- [ ] **SAVE-06**: recover a full game after close, reboot, reinstall; keep saves across package updates.
- [ ] **MEM-01**: real NSF/NSD paging + per-zone assets; reading 64 KiB blocks while keeping whole blob is not zone streaming.
- [ ] **MEM-02**: read/decompress worker + sync via `threadCreate`/available CPU time on O3DS, w/o assuming New 3DS capacity.
- [ ] **MEM-03**: pools + cache w/ RAM, linear memory, VRAM, DSP budget; safe eviction of still-referenced pages.
- [ ] **MEM-04**: avoid needless blob/VBO/texture duplication on level change; keep recovery on load failure.
- [ ] **MEM-05**: measure real peak, fragmentation, leaks over repeated enter/exit, death, vehicle, boss, cinematic cycles.
- [ ] **MEM-06**: profile CPU/GPU/SD per level + optimize culling, batches, animation upload, materials from measurements.
- [ ] **MEM-07**: reach + document requested 60 FPS target or log any real shortfall; do not infer physical FPS from host/emulator counter.
- [ ] **MEM-08**: decide ARM/VFP optimization flags w/ accuracy tests; do not apply `fast-math` to ops needing saturations/fixed behavior just to hit a performance label.
- [ ] **N01**: Toad Village - `0x0B`.
- [ ] **N02**: Under Pressure - `0x0E`, aquatic/submarine mode.
- [ ] **N03**: Orient Express - `0x0A`, Pura.
- [ ] **N04**: Bone Yard - `0x0C`, chase/mount + routes.
- [ ] **N05**: Makin' Waves - `0x19`, jet ski.
- [ ] **N06**: Gee Wiz - `0x0F`.
- [ ] **N07**: Hang'em High, normal entry - `0x16`.
- [ ] **N08**: Hog Ride - `0x15`, bike/race.
- [ ] **N09**: Tomb Time - `0x12`.
- [ ] **N10**: Midnight Run - `0x11`, Pura.
- [ ] **N11**: Dino Might! - `0x10`, mount/chase + secret access.
- [ ] **N12**: Deep Trouble - `0x1C`, aquatic/submarine mode + gem route.
- [ ] **N13**: High Time - `0x1A`, death/gem route.
- [ ] **N14**: Road Crash - `0x14`, bike + secret access.
- [ ] **N15**: Double Header - `0x1D`.
- [ ] **N16**: Sphynxinator - `0x1E`, alt routes/gem platform.
- [ ] **N17**: Bye Bye Blimps - `0x13`, flight/targets.
- [ ] **N18**: Tell No Tales - `0x0D`, jet ski.
- [ ] **N19**: Future Frenzy, normal entry - `0x1B`.
- [ ] **N20**: Tomb Wader - `0x18`, variable water.
- [ ] **N21**: Gone Tomorrow - `0x23`, platforms/transport + routes.
- [ ] **N22**: Orange Asphalt - `0x20`, bike/race.
- [ ] **N23**: Flaming Passion - `0x22`, death/gem route.
- [ ] **N24**: Mad Bombers - `0x17`, flight/targets.
- [ ] **N25**: Bug Lite - `0x24`, lighting + gem route.
- [ ] **N26**: Ski Crazed - `0x21`, jet ski + basement access.
- [ ] **N27**: Hang'em High, secret entry - `0x16`, own arrival/route.
- [ ] **N28**: Area 51? - `0x25`, bike/race + basement access.
- [ ] **N29**: Future Frenzy, secret entry - `0x1B`, own arrival/route.
- [ ] **N30**: Rings of Power - `0x1F`, flight/rings/race.
- [ ] **N31**: Hot Coco - `0x27`, jet ski, hidden access + exit.
- [ ] **N32**: Eggipus Rex - `0x26`, mount, hidden access + exit.
- [ ] **HUB**: Time Twister - `0x02`, 3D traversal + all states/portals.
- [ ] **J01**: Tiny - `0x06`; apply BOSS-01 + same audiovisual/technical acceptance.
- [ ] **J02**: Dingodile - `0x03`; apply BOSS-02.
- [ ] **J03**: N.
- [ ] **J04**: N.
- [ ] **J05**: Cortex/Uka Uka - `0x07`; apply BOSS-05.
- [ ] **SEQUENCES**: opening, both endings, intermissions, game over, title present in catalog; verify IDs/format + AV coverage; do not confuse 42 extracted files w/ 42 already playable levels.
- [ ] **QA-01**: matrix requirement -> code/data -> test -> capture/walkthrough; cover original requirements, not only existing suites.
- [ ] **QA-02**: production tests for VM, assets, collision, progress, save, audio, U, memory; do not duplicate impl in an always-passing test double, do not disable assertions.
- [ ] **QA-03**: compile both 3DSX/ELF variants + run C/ARM regressions; keep logs + hashes.
- [ ] **QA-04**: CIA target, `makerom`/`bannertool` deps, metadata, permissions, icon/banner, install/boot on console.
- [ ] **QA-05**: versioned format/runtime/exporter together; build reproducibility + executable/external-asset compatibility.
- [ ] **QA-06**: captures + same point/state comparison vs PS1; measure spawn, run, jump, camera, requested animations.
- [ ] **QA-07**: new game to standard ending w/o dev selector or flag editing; log route, failures, saves.
- [ ] **QA-08**: game to 105% ending w/ all routes, gems, relics, real requirements; check it can be loaded again.
- [ ] **QA-09**: regression runs for death, game over, restart, bonus, secret, boss, vehicle, suspend, SD failure.
- [ ] **QA-10**: full validation on **physical O3DS**: controls, image, audio, memory, load, FPS.
- [ ] **QA-11**: final SD package w/ all needed assets, hashes, install check; exclude saves, preserve updates.
- [ ] **QA-12**: final docs on install, controls, save, real failures.

### Retained third-party attribution

Format references: [noclip.website](https://github.com/magcius/noclip.website), `src/CrashWarped/bin.ts` and `script.ts`, MIT. Local SHA256: bin `5CF7A8EE666B4A026266CC272BE98F0BDB255E35F07C688EA8D4CA627F85E4A0`; script `C23246B0518B8C36DBDDCDC62AF8176831C41238F6219AB48828559A9EA05AD1`. These are format references, not native gameplay proof.

# Psx.jpg Credits

To the Author: SOQUEROEU.

The "psx.jpg" background was edited from the one obtained from "Soqueroeu TV Backgrounds 2.0" repository: https://github.com/soqueroeu/Soqueroeu-TV-Backgrounds_V2.0/tree/main.

The material is free to use according to the agreement below:

## AGREEMENT

This pack is free. You should not pay for anything related to this graphics pack and shader preset. You may distribute and reproduce part from this content, as long as you give credit to the authors involved. You may not profit from the sale of products that contain material in this package without the author's prior permission.

Main boots directly into the selected native level and owns2FC50/3079C defaults, original GP data,1AB64 scratch publication,156E0/15798 HID adapter and resumable demo signals. Loading requests the real NSD checkpoint ZDAT and enters215E8. Main additionally owns async142B4 resource requests, retained ZDAT/SVTX/TGEO/SLST banks, direct2AA14 list copy and1AE64 activation/creation with routed state entry. Main now owns22C9C/22DA0 page pins and cached142B4 imports,21E60/3505C zone state,12EAC resource draining,24330/2437C camera initialization,28740 camera sampling,exact-time mode3 scalars and25C0C/4F724 focal publication. Original title3C activation now runs TitOC logic and stops at opcode48/display events. Prior120 updates were flag-disabled hierarchy walks. SLST deltas,other timed intervals and unbound player-entry paths remain incomplete;1E78C physics, renderer packet/OT publication and SPU/INST/ADSR output remain open. No successful hardware boot or playable native cutover is claimed.

Current verification telemetry (2026-10-23): `python tests/run_checks.py`: 113/113 registered host checks passed; both ARM builds,42 GOOL catalog exports and SD package CRC/payload checks pass. 3DSX sizes: embedded 16470532 bytes, SD 500640 bytes. Both emulator variants execute title scripts through new-game reset, child creation and texture load, then fail opcode48; original menu/cinematics and playable native gameplay remain open.
