# BACKLOG — acceptance by phase

All items are open. Counts of unit tests are not completion percentages. Close an item only with production integration plus evidence (trace, capture or measured run). Grep by ID.

Superseded (prototype hub, drop): P0-01..05, P0-08. Re-derive portals/HUD from original data under STORY-* and UI-*. Kept below: P0-06 (GOOL runtime), P0-07 (packed normals), P0-09 (black screen).

## Phase 1 — first pixels
- **P0-09** resolve black presentation; real boot image.
- **P0-07** packed normals treated as normals when format/state says so.
- **GFX-06** Gouraud + normal lighting, LIT/CRYSTAL modes. **GFX-07** PS1 ABR semi-transparency, STP texels, blend/mask order. **GFX-08** near clipping, culling, depth, Ordering-Table-equivalent ordering. **GFX-15** free caches/textures on zone change; no cube placeholders.
- **CORE-07/08** TGEO/SVTX warnings and triangle deltas; sprite pages/regions and cross-file refs by proven identity.
- **MEM-01** (first cut) real NSF/NSD paging per zone. **MEM-07** measure FPS on hardware, do not infer from emulator.

## Phase 2 — walk in one level
- **CORE-01..06, 09**: inventory of native routines/engine calls; versioned runtime format; PS1/GTE fixed-point math; original update order/cadence; load/transition/death/suspend without freed refs; all engine services GOOL calls.
- **GOOL-01..13**: fetch/decode/dispatch; stack/frames/pointers; arithmetic/trig/random with sign/overflow checks; jumps/calls/state changes; pre_run/on_signal/suspend/resume order; geometry selection; create/link/destroy and zone activation; signals/collisions/lookup/globals; native MIPS services; resource accounting; class/ID/state classification (no name-based); instruction/state traces vs real objects; retire approximate controllers only after original replacement is tested.
- **WORLD-01..07, 12**: ZDAT/OCTR/SLST interpretation; spawn/orientation per entry type; original entry sequence; camera paths/triggers/interpolation; collision of floors/walls/ceilings/slopes/edges/zone bounds; height-query precision; no false falls or pass-throughs.
- **PLAYER-01/02**: original Crash/Coco states and animations; run/accel/brake/turn/jump/air control cross-checked vs original. **PLAYER-12** O3DS control mapping (Circle Pad + D-pad).
- **MEM-02/03**: read/decompress worker without New-3DS assumptions; pools and cache across RAM/linear/VRAM/DSP with safe eviction.

## Phase 3 — one-level vertical slice
- **PLAYER-03..11**: spin, sweep/crouch/crawl, belly flop, double jump, death tornado, fruit bazooka, speed shoes (resolve run/slide conflict), damage/immunity/mask/deaths/lives/game over, vehicle/mount/water states.
- **OBJ-01..13**: population and spawn conditions; wood/question/bounce/arrow/slot/extra-life/Aku crates; TNT; Nitro + green detonator; metal/reinforced/outline/`!` crates; checkpoints and crate totals; time crates (1/2/3 s); Wumpa/lives/mask; crystal and gems (independent conditions); all enemy families; fauna and decorations (no name coincidence); projectiles/traps/destructibles; each family vs each attack.
- **GFX-02..05, 10, 12, 13, 16**: all models; SVTX animation; sprites/billboards; UV/palette/material animation; shadows and FX; Aku Aku mask; collectibles; per-family visual comparison.
- **WORLD-08..11**: moving platforms/belts/transports; water/lava/traps/pits; full restore on death/restart (today only one checkpoint kept); bonus/death/gem routes with correct persistence.
- **AV-01..05**: provenance for banks/instruments/sequences; PS1 ADPCM + 24 SPU voices with envelopes/pitch/loops; music sequencer (retire synthesized BGM); dialogue/XA sync; NDSP queues, latency, priorities, no crackle or freed-buffer refs.

## Phase 4 — breadth
- **GFX-01, 09, 11**: all 42 scenario files and RAWD composition; sky/fog/water/darkness/lights; portals/rings/Time Twister remap.
- **VEH-01..07**: motorbike, Pura, jet ski, submarine/underwater, biplane, Baby T-Rex chases, mode changes inside a level.
- **STORY-01..07**: full 3D Time Twister, five cameras, basement, portal data synced across `main.c`/`story.c`/entities (no duplicate tables), per-portal states, holograms/voices for Cortex/Uka Uka/N, crystal/power unlocks via real events, secret routes (Hang'em High, Future Frenzy, Hot Coco, Eggipus Rex), colored/extra/all-gems conditions.
- **BOSS-01** Tiny Tiger; **BOSS-02** Dingodile; **BOSS-03/04** the two Dr. N. bosses (IDs for J03/J04 missing in the old README: fill from NSD); **BOSS-05** Cortex & Uka Uka. Each: arena, patterns, phases, damage windows, animations, defeat, reward.
- **AV-06..10**: original logos/startup, opening cinematic, title/menu, video and in-engine sequences, boss intros/endings/credits/game over.

### Levels (ID = NSD level ID; mode notes from acceptance text)
| Code | Level | ID | Notes |
|---|---|---|---|
| N01 | Toad Village | 0x0B | Phase 2/3 test level |
| N02 | Under Pressure | 0x0E | submarine |
| N03 | Orient Express | 0x0A | Pura |
| N04 | Bone Yard | 0x0C | chase/mount, routes |
| N05 | Makin' Waves | 0x19 | jet ski |
| N06 | Gee Wiz | 0x0F | |
| N07 | Hang'em High (normal) | 0x16 | |
| N08 | Hog Ride | 0x15 | bike/race |
| N09 | Tomb Time | 0x12 | |
| N10 | Midnight Run | 0x11 | Pura |
| N11 | Dino Might! | 0x10 | mount/chase, secret access |
| N12 | Deep Trouble | 0x1C | submarine, gem route |
| N13 | High Time | 0x1A | death/gem route |
| N14 | Road Crash | 0x14 | bike, secret access |
| N15 | Double Header | 0x1D | |
| N16 | Sphynxinator | 0x1E | alt routes, gem platform |
| N17 | Bye Bye Blimps | 0x13 | flight/targets |
| N18 | Tell No Tales | 0x0D | jet ski |
| N19 | Future Frenzy (normal) | 0x1B | |
| N20 | Tomb Wader | 0x18 | variable water |
| N21 | Gone Tomorrow | 0x23 | platforms/transport, routes |
| N22 | Orange Asphalt | 0x20 | bike/race |
| N23 | Flaming Passion | 0x22 | death/gem route |
| N24 | Mad Bombers | 0x17 | flight/targets |
| N25 | Bug Lite | 0x24 | lighting, gem route |
| N26 | Ski Crazed | 0x21 | jet ski, basement access |
| N27 | Hang'em High (secret) | 0x16 | own arrival/route |
| N28 | Area 51? | 0x25 | bike/race, basement access |
| N29 | Future Frenzy (secret) | 0x1B | own arrival/route |
| N30 | Rings of Power | 0x1F | flight/rings/race |
| N31 | Hot Coco | 0x27 | jet ski, hidden access + exit |
| N32 | Eggipus Rex | 0x26 | mount, hidden access + exit |
| HUB | Time Twister | 0x02 | 3D traversal, all states/portals |
| J01 | Tiny | 0x06 | BOSS-01 |
| J02 | Dingodile | 0x03 | BOSS-02 |
| J03/J04 | Dr. N. bosses | ? | BOSS-03/04 |
| J05 | Cortex/Uka Uka | 0x07 | BOSS-05 |

RAWD-only levels: `05,0D,13,17,19,1F,21,27`. **SEQUENCES**: opening, both endings, intermissions, game over, title; 42 extracted files != 42 playable levels.

## Phase 5 — completion
- **SAVE-01..06**: serialize gems/routes/secrets/crystals/ranks/times/powers/story events; versioned schema with legacy migration (no C-padding dependence); atomic write + CRC + backup + recovery (no space, SD removal, failed write/rename); autosave and manual save/load from Warp Room without incoherent mid-transition states; define Archive_SaveData vs `sdmc:/3ds/crash3/save.dat`; recover a full game after close/reboot/reinstall and across updates.
- **UI-01..07**: bottom-screen HUD (name, progress, Wumpa, crates, lives, inventory, powers, Aku state, time trial, boss), Warp Room UI, 320x240 adaptation with real touch targets, gesture/camera-drag rules, pause/settings/restart/confirmations/error states; top screen = gameplay (bazooka reticle only).
- **TIME-01..08**: clock only in correct state; object/enemy/crate substitution; per-crate pause and precision independent of FPS; death/restart/checkpoint behavior; Sapphire/Gold/Platinum times from original data (current table is not verification); best-time saves without duplicating relics; results/prize/hub return; one full trial per eligible entry.
- **STORY-08/09**: standard and 105% endings, credits, original percentage formula (counters adding up is not proof), keep playing/loading afterwards.
- **MEM-04..08**: no blob/VBO/texture duplication on level change; measure peak/fragmentation/leaks across enter/exit/death/vehicle/boss/cinematic cycles; per-level CPU/GPU/SD profiling; 60 FPS target or logged shortfall; ARM/VFP flags with accuracy tests (no `fast-math` on saturating or fixed-behavior ops).
- **QA-01..12**: requirement -> code -> test -> capture matrix; production tests for VM/assets/collision/progress/save/audio/memory (no duplicated implementations in test doubles); compile both builds and keep logs/hashes; CIA target (`makerom`/`bannertool`, metadata, permissions, icon/banner, console install/boot); versioned format/runtime/exporter; PS1 comparison captures at same point/state; new game to standard ending with no dev selector; 105% run reloadable; regression runs (death, game over, restart, bonus, secret, boss, vehicle, suspend, SD failure); **full validation on physical O3DS** (controls, image, audio, memory, load, FPS); final SD package with hashes (no saves, preserves updates); install/controls/save/failure docs.
