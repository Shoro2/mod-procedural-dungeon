# PDv2 Round F — F1b (mine walls), F2 (forest), F3 (mood per theme) — Plan

**Spec:** `docs/superpowers/specs/2026-09-11-pdv2-round-f-themes-design.md` (D1 dropdown, D5 own mood per theme, D7 forest with mixed
walls, D9 mine walls, D10 one MIG at the end). **Recons:** `.superpowers/sdd/roundf-recon-c.md` (kit chain, wall dressing, forest mix)
and `roundf-recon-d.md` (light / music / ambience lever). Read both before any task below — they carry the measurements this plan
builds on and are not repeated here.

**Operator decisions 2026-09-11 (after playing F1):** the theme choice is a dropdown under the caster/melee row (done: `34c7d1a`);
the mine walls are bare and need objects; every theme gets its own light and music; the forest walls mix rock cliff and tree line
with as much variety as possible; ONE host migration at the end when both themes are done.

## Global constraints (every task)

- Branch `claude/pdv2-round-f-63ac9a2a` in `mod-procedural-dungeon` (already checked out). Workspace `C:\wowstuff\ForgottenLand2.0`
  is NOT a git repo: before editing a workspace script, copy it to `<name>.pre_roundF_<YYYYMMDD>` next to it. `fl-stream-client` is
  its own repo (branch per task from its default branch).
- KLEIN und GEZIELT: touch only the files a task names. Commit per task (Conventional Commits, English, trailer
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`). If a commit fails on `index.lock`, wait and retry — other agents share
  the module repo. Do not push. Do not deploy. Do not restart the worldserver (it is running; the operator may be in game).
- No DB writes. Read-only world DB runner: `docs/superpowers/notes/round-e-loot-sql/q.sh` (`sh q.sh <sqlfile>`, SELECT only).
- Module C++: C++17, `-Werror`, `codestyle-cpp.py`; harness `pdblock --batch 500` / `--decor-batch 3000` must stay green with every
  `PD_*_PIN` unchanged (the fixtures are theme 0; a pin that moves means a bug, not a re-pin). Only ONE task (F3-B) builds the
  worldserver; nobody else touches `C:\wowstuff\dcore_bin`.
- Kit chain (48 → 51 → 52 → 49) runs twice per kit for determinism (byte-identical file sets); the oracle `49 --manifest` must stay
  byte-exact with `flstream_tests.exe`. Chain run time is unrecorded — record it in the task report.
- Report per task in `.superpowers/sdd/<task>-report.md` (gitignored): what changed with file:line, measurements, checks verbatim,
  deviations, what a boot or the T2 must still prove.

## Packages and order

| Task | Repo / files | Depends on | Builds |
|---|---|---|---|
| K1 mine walls + wall-style engine | workspace `scripts/48_gen_t1_blockkit.py` (+ `51` texture table if needed) | — | kit chain (mine+city, `KIT_VERSION` 28, `t1b-v40`) |
| F2-D forest data | module `data/sql/db-world/mod_pdungeon_packs_forest.sql`, `mod_pdungeon_decor_forest.sql`, `mod_pdungeon_templates_fix.sql` | — | none |
| F3-A mood rows | workspace `scripts/47_pd_map760_rows.py` → `output\hot_dbc\...` + module `data/sql/db-world/mod_pdungeon_map760.sql` (generated) | — | none (30/102 run at close) |
| F3-B engine | module `src/generator/PDBlockPlan.*`, `PDv2Commands.cpp`, `PDv2Mgr.*`, `PDv2InstanceScript.cpp`, `conf/*.conf.dist`, harness | — | worldserver |
| F3-C composer areaid | `fl-stream-client` (`src/lua_bridge.cpp`, `tests/flstream_tests.cpp`) + workspace `scripts/49_pd_compose_blocks.py` | — | DLL + tests |
| K2 forest kit | workspace `48` / `51` | K1 (same file) + F3-B (theme 3 namespace for the oracle) | kit chain (three themes, 369 files) |

K1, F2-D, F3-A, F3-B, F3-C run in parallel; K2 after K1 and F3-B. Sonnet review per task; fixes by the planner or the same agent.

## Task K1 — mine wall dressing and the per-side wall-style engine (script 48)

**Goal:** the mine's wall band carries rock bodies (and some mine timbering) so the bare terrain bank is hidden and the wall reads
as a cave wall; the engine that does it is theme-generic so K2 reuses it for the forest.

**Contract (decisions made — implement, do not re-decide):**
1. **Channel = kit MDDF (M2 doodads).** No MODF/WMO for theme 1 (an empty `THEME1_WMO` keeps `PD_PATROL_CLEAR_PIN` untouched; MDDF
   never collides, which is right on the band and wrong on the floor — nothing may stand on a WALK cell).
2. **Slots:** `PLACE_K_D` 12 → 24 (format-safe, precedent 8 → 12 in v32; the DLL derives `slotsDoodad` from the kit). Assert MCRF
   refs per MCNK ≤ 32 in the chain output and print the max.
3. **Per-side style pick** inside `add_facades`'s side loop, keyed by the per-side seed it already computes:
   `THEME_WALL_STYLE = {1: ("rockwall", "rockwall", "timber")}` (repetition = weight; K2 adds `3: ("cliff", "treeline")`). A style is a
   named menu of models with per-model measured bbox (from `58_m2_static_scan.py`), `yawFix`, `sinkDz`, `inset`.
4. **Seat rule (`band_seat`)**: a body's front face sits at the wall-foot line (inset 0 … −1 yd into the wall cell, measured from
   the walkable edge like `wall_edge_point`), its base is sunk ≥ 0.5 yd below the seat surface (never floating), its yaw puts the
   shallow bbox axis across the band, and the yawed bbox must fit within 16 yd of the foot (the block's own ring plus the
   neighbour's ring; beyond that a body pokes into the neighbouring floor). Models whose shallowest axis exceeds 16 yd are excluded
   from the menu, not squeezed. Pack a side widest-first within the existing `VARIETY_BAND` law; fillers close the gaps; corners get
   one body each so the band has no naked corner.
5. **Blob outlines** (theme 1 alt-1 rooms): walk the wall-foot polyline cell by cell (`wall_edge_point` per wall cell) instead of
   assuming four straight sides; a body per ~its width along the polyline. Corridors: both long sides, 1–3 bodies each.
6. **Mine menus (stock 3.3.5a only, no patch-4/5 Cataclysm rocks):** rockwall = `DeadwindPassRockTree02/03/04`, the six
   `OgreMoundRocks`, `CaveMineSpiderPillar01`, `BadlandsRock01/03/05`, `ElwynnCliffRock04` (exclude any whose seat rule fails);
   timber = `HU_Scaffolding02`, `Shadowmoon_scaffolding03` (upright against the band, never over a door). `RockArch02DNR` may frame a
   door segment as a one-off if its bbox clears the door mask — optional.
7. **Wall texture:** swap the mine's layer-0 wall texture (`THEME_TEXTURE_LAYERS[1]`) for a more structured stock rock with a
   verified `_s` pair (recon C §2 list: BurningStepps 01–04, Badlands, SP_RockBaseA/B, BoneWastes); it must be structureless
   enough to rotate. Keep two MCAL layers (no third layer — +46 % kit size). Record the choice and the reason in `kit_meta.json`.
8. Walk masks, anchors, socket contract, `PD_LAYOUT_VERSION`, manifest format, the existing `add_wall_props` GO columns /
   stalagmites: unchanged. `KIT_VERSION` 27 → 28, kit dir `t1b-v40` (K2 rebuilds into the same version — do not bump twice).

**Verification:** chain twice → byte-identical (247/247); `49 --manifest` oracle byte-exact for a theme-1 and a theme-2 manifest
(`pdblock --manifest <seed> <file>` from the module); per-block MDDF use for theme 1 before/after (mean, max) and MODF unchanged
(0); MCRF max; the seat rule proved by a script that reads the written MDDF entries back and checks every body against the rule
(front at the foot, base sunk, bbox within 16 yd, none on a WALK cell) — print counts; kit size; run time.

## Task F2-D — forest packs, decor rules, GO rows (module SQL)

Same shape and discipline as F1's Task 3 (`mod_pdungeon_packs_mine.sql`, `mod_pdungeon_decor_mine.sql` are the templates; read
`.superpowers/sdd/f1-task-3-report.md`).
- `mod_pdungeon_packs_forest.sql`: packs 12 **Wailing Caverns** (Deviate / Druids of the Fang: e.g. 3636, 3637, 3638, 3654,
  3669 Lord Cobrahn, 3670 Lord Pythas, 3671 Lady Anacondra, 3673 Lord Serpentis, 3674 Skum, 3653 Kresh, 5775 Verdan), 13
  **Maraudon** (Razorfen? no — Maraudon: Theradrim / Noxxion / Landslide / Rotgrip / Celebras / Princess Theradras family; choose
  10–12 members), 14 **Razorfen** (Kraul/Downs quilboar: 4416–4442 family, bosses Charlga Razorflank 4421, Agathelos 4422,
  Aggem Thorncurse 4424, Overlord Ramtusk 4420, Tuten'kash 7355, Amnennar 7358). Rules: `theme 3`, `level 80/80`, `unlock_dlvl 0`,
  `enabled 1`, weights 100, roles 0/1/2 with casterSpellId on role 1 only (the shipped `69211`/`60015` law); every entry checked
  in `creature_template` (exists, no difficulty twin, `exp 0` or the basehp trap discussed, own loot), NOT already in packs 1–11,
  **no `spell_school_immune_mask` ≠ 0** (the 8911 lesson — a school-immune trash mob is a bug), stated mechanic immunities in the
  header. Aim for 10–12 members per pack, ≥ 2 bosses per pack.
- `mod_pdungeon_decor_forest.sql`: rules 34+ `theme 3`: fallen trees / logs at `wall_foot` (these are the blockers the spec names:
  6826 log, 8025 / 8028 fallen trees — verify dtree; the recon flags all-zero DBC geoboxes on 8025/8028 → prove collision via
  `GameObjectModels.dtree`, else pick logs that have it), fences 6151 / 6150 at `wall_foot` low weight, a campfire (`corner`,
  boss rooms) and a lantern/torch post, scatter: stumps, mushrooms, bushes only if they have a dtree (else skip — no collision =
  players walk through decor = bug). Budget check against `PD_DECOR_MAX_SPOTS = 450` with the replay method of the mine header.
- `mod_pdungeon_templates_fix.sql`: new GO rows from the free block **910087–910094 at most** (keep ≥ 8 free), type 5, `PD `
  prefix, three-way check per row (DBC model, dtree, stock precedent), DELETE list extended, free-id comment updated.
- Verification: idempotence (entry-exact DELETEs), sort order vs the CREATE-owning files, column order, counts; q.sh spot checks.

## Task F3-A — mood rows per theme (script 47 → client DBCs + module SQL)

**Contract:**
- AreaTable rows: 5100 unchanged (city: ambience 37, music 439); **5101 "The Forgotten Mine"** (ambience **34** MineStandard,
  ZoneMusic **236** Zone-Dwarf Outpost); **5102 "The Forgotten Woods"** (ambience **35** ForestNormalDay/Night, ZoneMusic **1**
  Zone-Forest). Copy `Flags`, `FactionGroupMask`, `MinElevation`, `AmbientMultiplier`, `LiquidTypeOverride`, `ExplorationLevel`
  and the AreaBit (reuse 4007) from 5100 verbatim; only name, ambience, music differ. Emit the matching `areatable_dbc` rows.
- Light rows: **2864 mine** ← LightParams **919** built from donor 811 (Ahn'kahet) sampled at noon into single-key bands (the
  `build_light_params` construction of 918), fog re-scaled to 918's `fogEnd` band (333 yd — the far wall must not pop), skybox
  referenced, 808's `fogStartScaler` trap respected; **2865 forest** ← LightParams **920** from donor 513 (Scarlet Monastery
  green). Bands: IntBand 16525–16542 / 16543–16560, FloatBand 5509–5514 / 5515–5520 (verify free at run time, abort if not).
  Position both rows OFF-FIELD (never `(0,0,0)`: e.g. X = Y = 15000, falloff inner/outer small) so the map default stays 2863 on
  both sides (`GetDefaultMapLight` scans descending).
- New `light_dbc` leg in the module SQL: rows 2863 (the existing map default — copy its values), 2864, 2865 (only ID/map/X/Y/Z
  are read server-side; fill the 15 columns anyway).
- `47 --check` must diff every new row client↔server; run 47 (writes `output\hot_dbc\DBFilesClient\{AreaTable,Light,LightParams,
  LightIntBand,LightFloatBand}.dbc` and regenerates `<module>\data\sql\db-world\mod_pdungeon_map760.sql`), then `--check` clean.
  Do NOT run 30/102 (close sequence). Idempotent re-run proven (second run = identical files).
- Header/doc of 47 updated (the per-theme table is the single source: theme → area, light, ambience, music).

## Task F3-B — engine: theme 3 namespace + per-theme light override (module C++)

1. `ThemeChunkIdBase(3) = 22000` (`PDBlockPlan.cpp`), validator/GM command accept 3, `ThemeMax()` unchanged (from chunk meta);
   harness: one case that theme 3 ids resolve (no pin moves).
2. Conf keys `ProceduralDungeon.V2.Theme<N>.LightId` for N = 1..9 (default 0 = no override); `.conf.dist` ships `Theme1.LightId
   = 2864`, `Theme3.LightId = 2865`, `Theme2.LightId = 0` with a comment naming the rows and F3-A. Loaded into a small array in
   `PDv2Mgr` (log one INFO line listing non-zero themes at boot).
3. In `PDv2InstanceScript::SpawnFromPlan` (after the map exists, the plan's theme at hand): if the theme's LightId ≠ 0 →
   `map->SetZoneOverrideLight(zoneId, lightId, 0ms)` with `zoneId` = the map's linked zone (5100; read it from `MapEntry`, do not
   hard-code). Verify the packet reaches late joiners via `SendZoneDynamicInfo` (read `Map.cpp:3135-3153` and `PlayerUpdates.cpp:1295`)
   — if it does, nothing else; if it does not, hook `OnPlayerEnter`. Note in the report that `_defaultLight` needs the `light_dbc`
   row 2863 from F3-A (boot without it sends default 0 — say what the client does then, from the packet handler's perspective).
4. Build worldserver (`cmake --build C:\wowstuff\dcore_bin --config RelWithDebInfo --parallel 4 --target worldserver`), harness
   green with pins held, codestyle clean. Do not install.

## Task F3-C — composer: areaid per theme (fl-stream-client + script 49)

1. `kAreaId` constant → `AreaIdForTheme(theme)`: 1 → 5101, 2 → 5100, 3 → 5102, anything else → 5100 (`lua_bridge.cpp:34, 171`;
   the manifest's theme is already parsed). Unit fixture in `tests/flstream_tests.cpp` (the `:823` fixture) gains the theme cases;
   the all-MCNK assertion stays.
2. Script 49: `--area-id` follows the manifest theme with the same map (single source: mirror the table, assert equality in a test
   or a comment that names `lua_bridge.cpp`). Oracle byte-exact for a theme-1 and a theme-2 manifest.
3. Build the DLL (Release, the repo's documented way) and `flstream_tests.exe`; run the tests; record the DLL hash and size.
   Commit on a branch from the repo's default branch; do not push. Do not touch `FLStream.ini`'s KitDir (close sequence).

## Task K2 — forest theme in the kit (after K1 and F3-B)

`THEME_BASES[3] = 22000`, `THEME_NAMES[3] = "forest"`, textures with `_s` pairs: wall `GH_MossyRockA` (alt B/C by alt), floors
`GH_PineNeedlesA` (corridor), `GH_GrassyA` (room), `GH_PineNeedlesB` (boss) or the recon's list; `TEXTURE_SUPERSET` extended (check
the MTEX superset budget); `THEME_WALL_STYLE[3] = ("cliff", "treeline")` per side: cliff = forest rock bodies (scan Grizzly Hills /
Ashenvale / Duskwood rock M2s with 58; fallback the mine's rock family), treeline = Grizzly pines + Terokkar Large/Medium + fallen
logs (trunks on the bank/crest, never on a WALK cell; canopy may overhang), `THEME3_WMO = {}`; no theme-3 kit GO props (decor is
F2-D's). Chain → 369 files, determinism 369/369, `pdungeon_chunk_meta.sql` with 122 theme-3 rows, oracle per theme (1/2/3),
MDDF use per block per theme, seat-rule readback for theme 3, kit size (~409 MB), run time.

## Close sequence (planner)

1. Reviews + fixes; rebuild worldserver if F3-B changed after review.
2. `30_build_hot_dbc_patch.py` (patch-9 with the new DBCs) → `102_build_launcher_payload.py` (kit `t1b-v40`, DLL, `FLStream.ini`
   KitDir flip, patch-9, FT addon unchanged) — the launcher listing must name `t1b-v40` BEFORE any ini flip (FL_Client_Sync
   archives unlisted kit dirs).
3. Workbench: install worldserver (backup), SQL at boot (chunk meta v40, packs_forest, decor_forest, templates_fix, map760),
   conf keys appended from the boot log, `flpdui.lua` deployed; client `C:\wowstuff\FL2-Client`: kit v40 + DLL + ini + patch-9
   copied (backup of the old); boot checks (packs per theme line: theme 0: 8, 1: 3, 3: 3; decor rules; light INFO line; 0 ERROR).
4. `tools/pd_testlauf_runde32.md` §2 (F1b/F2/F3 — what to see: dropdown under casters, mine walls, forest, own light/music),
   docs (CLAUDE.md row, README), vault (06 ids incl. the Light/LightParams/band registry, 09 tables, queue, log), memory.
5. Host: ONE MIG after the operator's T2 of everything (server DBC/override-SQL class + launcher payload leg).
