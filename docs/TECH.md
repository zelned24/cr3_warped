# TECH — formats, native addresses, evidence

Reference only. Grep by address or keyword; do not load whole.

## Source and data inventory
- PS-X EXE SHA256 `1b93cc563c90966e1d79b750f7d19bc1662859cceeb991775e9e9a22656d71d1`; 333824 B, payload at `0x800`, 331776 B mapped to `0x80010000`, entry `0x800489F8`. Disc sectors 2352 B, Mode2/Form1.
- Original data: 42 archives, 2282 raw NSF pages, 14778 entries, 186126 items, 334 texture pages; 1717 zones, 7761 entities, 133728 series, 2209337 elements, 62811 path points. Identity is EID/CID, never name substitution.
- noclip reference hashes (format reference only): `bin.ts` `5CF7A8EE666B4A026266CC272BE98F0BDB255E35F07C688EA8D4CA627F85E4A0`, `script.ts` `C23246B0518B8C36DBDDCDC62AF8176831C41238F6219AB48828559A9EA05AD1`.
- Evidence: original bytes/disassembly in `docs/evidence/psx_signal_native.json`, replay tools/tests, original disc. Replays use declared hooks and do not prove gameplay parity.

## NSF/NSD
- NSF page 65536 B: header `u16 magic, u16 type, u32 CID, u32 count, u32 checksum, u32 offsets[]`; magic `0x1234` raw / `0x1235` compressed. NSD indexes pages. NSD directory-end+8 is the level ID in all 42 archives.
- Entry: `u32 magic=0x0100FFFF, EID, type, itemCount, offsets[]`; EID charset `0-9a-zA-Z_!`. Page types: 0 normal, 1 TPAG, 3 ADIO, 4 INST, 5 MIDI-SEP.
- Entry types: 1 SVTX, 2 TGEO, 3 WGEO, 4 SLST, 7 ZDAT, 11 GOOL, 12 ADIO, 13 MIDI, 14 INST, 15 VCOL, 17 RAWD, 19 PBAK, 21 VIDO.

## Model and world formats
- **WGEO:** origin + signed 12-bit positions /16; XYZ order x/z/y; vertex 6 B, triangle 6 B, quad 8 B, texture descriptor 12 B.
- **TGEO:** scale i32[3] at `0x00` /256; texture EIDs `0x0C`; counts: UV `0x34`, vertex `0x38`, color `0x3C`, texture `0x40`, triangle `0x44`, animation `0x48`. UV stride 12: pairs at 0/4/8/10, CLUT at 2, texpage at 6. NEW_TRIANGLE `0x80000000` consumes three flags; source vertices are sparse slots. Final position `(vertexByte + svtxOrigin) * tgeoScale / 8`.
- **SVTX:** one item per frame; i16 origin XYZ at 0/2/4 /32; view offset 8/10/12; vertex count `0x10`, hitbox count `0x14`, vertex-data offset `0x18` (trust it; some frames have 3 extra bytes), hitboxes from `0x1C` stride `0x28` (fields flags/center/min/max at 0/4/16/28, uncalibrated). Uncompressed bytes x/z/y. TGEO compression: u16 at `offset ^ 2`, MSB-first signed deltas. `Cr10G` declares 309 vertices but has 305 compression instructions (unresolved).
- **ZDAT:** sky EID `0x004`; world EIDs `0x034 + i*0x30`; counts info/database/object/other at `0x184/188/18C/190`; other EIDs `0x194`. Database series count `+0x0C`; series data offset relative to database `+0x0C`, align4. Path transform `(offset + point * (1 << shift)) * 0x100`; no assumed path-to-world factor.
- **RAWD-only files** (`05,0D,13,17,19,1F,21,27`): mesh header `u16 type=2,count; u32 texEID`, 16 B records (RGB/pad + three xyuv byte tuples). Textures 32x32 RGBA8888 (4096 B). XZ flattening is an approximation (prototype).
- **CWT1 (prototype container, to be removed in Phase 0):** 64-byte LE header, RGBA5551, 8x8 Morton tiles, limits 16 MiB/file, 8 MiB geometry, 4 MiB texels. Does not implement PS1 blending.

## GOOL VM (native addresses)
- Opcode table `0x8005D120`; frame enter/return `0x8001D588/0x8001D5E8`; frames hold flags, PC, packed links relative to object+`0x40`; slots 0..2 header, locals >=3, negative slots = arguments. Member aliases need registered providers.
- SEND wrapper `0x80037EDC`, service `0x8001DB38`, RECEIVE `0x800383DC`, SET_STATE `0x80037BEC`, state entry `0x8001D2D4`. Explicit signal routes with bit `0x8000` select BASE code + `(raw & 0x3FFF)`; bit `0x4000` selects no alternate here. RECEIVE needs mode 8 and a borrowed engine result; sourceOp3 unsupported.
- Token setters `0x80037CD8/0x80037DAC`, gate `0x8001D870`; token `tick_u32 | duration<<24`, consumed before main mode 4, pre_run mode 3. RESUME entry `0x8001D4B0`, return `0x80039A78`; mode `0x13` return seeds zero token. JALR opcode `0x47` at `0x80039978`; `0x3A` is a different service. Code-pointer opcode `0x18` at `0x800392B8`, null index `0x3FFF`; ARRAY_READ `0x2A` / WRITE `0x4E` at `0x80039724/754`, address `base + ((uint32(index)>>8)<<2)`, u32 wrap.

## Native engine map
- Creation `0x80019CE4..0x8001A428`: admission `0x8001A084`, slot selection `0x8001A154`, init `0x8001A5C8`; search `0x80019844` with predicate `0x80019CC0` (skips type-2 roots, snapshots siblings). Retirement wrapper `0x8001AA78` forces notify 1 into `0x8001A844`.
- Pool bootstrap `0x8002049C..0x800205F4`: 96 slots * `0x23C` + player `0x33C`; roots `0x80068EA0`, free root `0x80068EE0`, current object/tick `0x80068EE8/EC`, player/pool pointers `0x80068E98/9C`; auxiliary: player `0x80069C48`, objects `0x80069CB4 + i*0x30`. Native handles are explicit port mappings, not host-pointer casts.
- Path service `0x80020670..0x80020EFC` (selection/interpolation/flags/orientation/projection; final original-execution check pending). Position block `0x800207B0..0x800207F4`: signed16 components, shift&31, wrapping 32-bit. Player creation overwrites position from its checkpoint; do not use an entity-path spawn.
- Zone links `0x80021330..0x80021474`; startup `0x800212A4..0x800212D4` selects database triple via checkpoint+`0x2E0` (`+0x2E4` opaque). Adjacency predicate `0x800214B8..0x80021530` (header+`0x190` count, +`0x194` refs; odd neighbors EIDs, even resolved entries). Entry init `0x80021530..0x8002157C` keeps header+`0x2BC` bit0, else writes `0x80069210=0`, `0x80069218=0`, `0x80069214=0x19000`, sets bit0.
- Timing: unsigned source divides by 34 at `0x8001D16C`; frame factor `0x80061A7C+0x18` clamps at 102 in `0x8001AB64`. Units unproved; do not use presentation delta time.
- Implemented modules: **data** `zdat_native/resources/path_native/lookup`, `gool_native_bounds`, NSF-backed item resolution, async NSF pages; **GOOL** `gool_state_native`, `gool_native_{dispatch,state,factory,router,update,mode,represent,call,activation,zone_transition,zone_retirement,matrix,primitive,shading,render,model_prepare}`, `gool_world`, `native_runtime`; **model/render** `model_{actor,pose,draw,texture_cache,gpu_texture,triangles,vertices,project,clip,visibility,commands,emit,packets,packet_plan,stream,present}`; **audio** `audio_sequence`.

## Budgets and storage
- Async NSF: 4 linear 64 KiB pages, 128 KiB encoded staging, 8 KiB worker stack; generation tickets, request/page borrows, cancellation, joined close. GOOL level bank: 20 fixed 64 KiB slots (42 catalogs, 3..18 behaviors per level). Resource arena: 32 pages = 2097152 B (port budget). Native constant storage 262144 B. Memory domain: 32 borrowed spans; delivery pool 16 jobs (port limits, not original).
- Audio: 3 linear PCM buffers, 10752 B per stream; explicit ADIO EID playback exists. Release queued PCM and pages only after NDSP stops.
- Historical estimates (not measured peaks): converted container ~16 MiB; collision VBO reserve up to 8867520 B (Road Crash), double 17735040; CWT max 8300352 B + 1235456 texels; UI/character/effects 2015184 B. Exclude SDK/heap/NDSP/render targets.

## Open defects
Scheduler/activation and engine services; nested signal root replacement/outer receivers; sourceOp3; fixed-point/GTE parity; packed normals misread as RGB; RAWD/material composition; CWT brightness data; missing sprites/resources; secret-arrival selectors; portal provenance/relic thresholds; moving platforms/cameras/vehicles/bosses/cinematics; original audio; physical O3DS acceptance.
