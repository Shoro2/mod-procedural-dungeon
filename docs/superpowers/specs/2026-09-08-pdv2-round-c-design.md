# PDv2 Round C — the operator's T2 verdict on Round B, turned into fixes and features

**Date** 2026-09-08 · **Branch** `claude/pdv2-round-c-<sessionId>` off `claude/pdv2-round-b-0cf92ad4`
(`c0e3afb`; Round B stays unmerged until Round C passes the same operator test) · **Status** design,
approved by the operator in chat on 2026-09-08 (decisions listed per section), not implemented.

Source: the operator's Round B test (`tools/pd_testlauf_runde28.md`, run of 2026-09-08 09:49,
`Server_2026-09-08_09_49_57.log`: seed 2052467817, difficulty 1, 16 rooms / 2 bosses, 49 blocks,
completed 13/16 rooms, 61 kills). Research with `path:line`: `.superpowers/sdd/c-research-*.md`
(four files: patrol+ambush, ambush trigger, chest/altar/finale/UI, bosses/triangles).

Evidence tiers: **T0** written · **T1** verified offline on this box · **T2** run in a real client.

---

## 0. What the test said (verbatim intent, German kept where it is the decision)

| # | Operator | Verdict | Round C item |
|---|---|---|---|
| 1 | Altar im Startraum unnötig; Boss 1 gecleart, kein Altar zum Anklicken → *"lass uns die anklickbaren Altäre komplett streichen und stattdessen den Spieler einfach immer am Start oder dem am weitesten entfernten geclearten Bossraum"* | B1 superseded | **C5** |
| 2 | Shifting Cache um 90° nach rechts drehen; Klick tut nichts | bug | **C3** |
| 3 | UI: Fortschritt in % zum Öffnen der Tür | feature | **C7** |
| 4 | geclearte Räume auf der UI-Karte grün | feature | **C7** |
| 5 | Boss 1 (Dreadlord) hatte auf Schwierigkeit 1 nur Cleave → *"Bosse sollen 2 Basisfähigkeiten haben, dann jeweils auf 50 und 75 einen zusätzlichen"* | decision | **C6** |
| 6 | Patrouille läuft quer durch Wände / schwebt unter der Map | bug | **C1** |
| 7 | Lord Maltrion ist auch ein Dreadlord-Modell — redundant? | content | **C6** |
| 8 | Finaler Boss down → Chromie spawnt, kurzer Dialog, Portal nach Azealia, dicke Belohnungskiste | feature | **C8** |
| 9 | Ambush nicht bemerkt | measured: the run armed **0** corridors (both 50 % coins failed: draws 70 and 94); AND the trigger geometry is broken for every non-straight corridor | **C2** |
| 10 | Hinweise als Raid-Warnung oben statt im Chat | feature | **C7** |
| 11 | vereinzelte flache 2D-Dreiecke (Screenshots) | kit defect, cause identified with high confidence | **C4** |

Round B items that passed T2 without remark: B0 chain + B0b loop rooms, B2 pads/33 yd rooms/anchor
spawns, B3 barriers (both opened at 18/35, orientation not questioned). B4 and B5 failed as above.

---

## 1. Facts that shape the design (measured 2026-09-08)

- **Patrol.** `GridLineWalkable` (`src/generator/PDv2WalkGrid.cpp:209-228`) is a Bresenham
  one-sample-per-major-step test, not the supercover test its header claims: at every minor-axis
  transition it skips a cell. `SimplifyGridPath` (`:231-254`) then builds the longest approved
  segments, and `MoveToWaypoint` (`src/PDv2CreatureAI.cpp:197-208`) walks them as raw straight
  lines at constant `floorZ` 50 with gravity off. Over the 122 deployed theme-2 masks 1.70 % of the
  approved cell pairs leave the walk mask (1.03 % by more than 2 yd, worst 5.21 yd, 116 of 122 chunks
  affected).
  **Correction, measured after C1 landed** (Task 1 review, exact-rational arithmetic over all 244
  staged masks — quote this split, not "4014 segments leave the walk mask" or "1.70 %"): of the
  **301 084** pairs the Bresenham sampler approves, **2 136 (0.71 %) are real excursions** — the
  segment crosses an unwalkable cell's *interior* — and a further **1 878 are exact-corner grazes**,
  which the 1/64 sampler flags only because its `double` wobbles at the corner; 2 136 + 1 878 = the
  4 014 the harness's first failing run reported. The supercover fix removes **9 604** approvals in
  all: those 2 136 plus **7 468** refused by the no-corner-cutting rule alone, which is a deliberate
  body-radius tightening rather than a measured wall crossing. The commit message of `4f5b7e0`
  carries the older reading and cannot be changed; every document quotes the split above instead.
  WALK cells are Z 50, WALL cells rise to Z 56, VOID is Z −400 — one defect yields both
  "through walls" and "under the map". Secondary: the evade rejoin (`PDv2CreatureAI.cpp:380-401`) is
  an uncapped beeline gated only by that test; the patrol spawn point takes no grid veto; the
  `StartWaypointRun` index-0 comment is wrong after the planner's 16.7 yd snap. Every other user of
  the line test (the rejoin gate, the caster's `GridLineOkTo`) inherits the fix.
- **Ambush trigger.** The 9.0 yd disc around the block centre reaches the lane only on straight
  corridors (8.08 yd). Corners, T-pieces and crossings put the lane 11.20–11.79 yd from the centre
  (junction square half-width 8.33 yd → corner 11.79); a player walking the lane can never fire those
  spots. The engine side is sound (`Update` is the InstanceScript override the core calls, `_ambushes`
  survives, same `BlockToWorld` frame as the barriers). The arm-time log prints only a count.
- **Chest.** `gameobject_template` 910030 (`Shifting Cache`, type 3 CHEST, display 259, loot 910030
  valid) has `Data0` (lockId) **0**. Chests are opened only through the client's "Opening" cast, and
  `Spell::CheckCast` rejects a lockless GO with `SPELL_FAILED_BAD_TARGETS`; `GameObject::Use()` has no
  CHEST case. All 1356 working chests in the world DB use lock **57** (`LOCKTYPE_OPEN`, treasure,
  skill 0). `Data3` (consumable) and `Data2` (restock) are 0 → once lockable it restocks instantly.
  The chest is spawned with orientation `0.0f` (`src/PDv2InstanceScript.cpp:1722`); WoW orientation is
  counter-clockwise, so "90° nach rechts" is **−π/2 = 4.712389**.
- **Altars.** `IsAltarRoom` = `chainIndex % 5 == 0`; bosses sit at `BossChainIndex` (6 and 12 in the
  test) — the two grids never coincide, so no boss room can host one. Removal touches GO 910058's
  script and spawns, `PDv2Altar.cpp`, `SpawnAltars`/`BindAltar`/`RespawnAltarFor`, `_altars`,
  `_altarByGuid`, `_boundAltar`, `IsAltarRoom` and three notices. `OnUnitDeath` + `_pendingRespawn`,
  `RespawnPending`, the release veto (`PDClientLink.cpp:435`), `OnPlayerLeave` and `_entranceX/Y/Z`
  stay. `roomBlocks` is discarded at `PDv2InstanceScript.cpp:1030`; `_roomIsBoss`, `_roomSegment`,
  `_roomAlive` and `EntranceWorldPos` exist.
- **Bosses.** Five role-2 entries in `pdungeon_pack_members`; each carries exactly three rows in
  `pdungeon_member_spells` gated `minDiff 1 / 50 / 75` (`src/PDv2CreatureAI.cpp:589-596` drops
  `minDiff > difficulty`), so difficulty 1 is a one-ability rotation by Round A design. The draw is
  uniform over the pool with no no-repeat rule (the 02:06 run drew 29620 for both boss rooms).
  84289 Lord Maltrion = `Creature\Dreadlord\DreadLord.mdx` ×1.5; 29620 Dreadlord Mal'Ganis =
  `Creature\MalGanis\MalGanis.mdx` ×3.0 — two models, both nathrezim. `creature_template` rows
  84263–84290 are read-only; membership and spells are module SQL.
- **Run end.** `FinishRun` (`src/PDv2InstanceScript.cpp:646-706`) grants XP, sends `E`, chats,
  writes `pdungeon_runs`, teleports nobody. The final boss is the kill that makes
  `bossKilled == bossTotal`. Azealia = **map 727**, `game_tele` 20040 `flraidazealia`
  (13611.1, 13655.7, 9.87548, o 4.7776); stock portal GOs 777000 / 222000 / 223000 use display
  **9041**. No custom Chromie exists; stock Chromie entries use gnome model **10008**. In-module
  scaffolds: `PDExitObjects.cpp:26-36` (`go_pdungeon_exit`, GO 910032 type 10 GOOBER, click →
  `TeleportTo`) and `PDEntranceNPC.cpp:35-78` (gossip NPC). Free ids: GO 910067–910069, NPC
  910550–910599 (registry `06-custom-ids.md`).
- **UI link.** Five addon messages on prefix `FLPDU`: `C` cfg, `M` map (`w h cpb ex ey` +
  `bx,by,role,mask;`), `R` tick (elapsedSec, killed, total, bossKilled, bossTotal, roomsCleared,
  roomsTotal, px, py, state), `E` completion, `N` notice. The map has no per-room state; notices go
  to `DEFAULT_CHAT_FRAME:AddMessage` only (`flpdui.lua:770`). 3.3.5a offers
  `RaidNotice_AddMessage(RaidWarningFrame, text, ChatTypeInfo["RAID_WARNING"])`.
- **Triangles.** No asset is missing (kit M2s, all WMOs and their doodads, all 42 GO displays,
  ground-effect doodads: 0 missing models, 0 missing textures). `ShadowfangFog01.m2` (105 placements,
  all at floorZ 50) contains **four static opaque two-sided triangles ~1.8 yd wide**, textured with the
  solid-white `WHITE8X8.BLP`, 0.05–0.86 yd above ground, two flat and two tilted into slivers — the
  shapes in both screenshots. Second candidate: `WEBSTRETCH01`/`WEBDANGLE01` alpha planes in
  `house_p` doodad set 0 (22 placements).

---

## 2. Decisions

### C1 — patrol movement (bug)

| # | Decision |
|---|---|
| 1 | `GridLineWalkable` becomes a true supercover test (Amanatides–Woo grid traversal): every cell the segment crosses must be walkable, and at a diagonal crossing **both** straddling cells must be walkable (no corner cutting; this is the body-radius hardening in its cheapest form). Header and source comments state what the test does. |
| 2 | The evade rejoin is capped: a waypoint qualifies only if the supercover line from the creature's cell is walkable **and** at most 4 cells (33 yd) long; otherwise the creature first walks `FindGridPath` to the nearest route waypoint, then resumes. No beeline longer than that ever exists. |
| 3 | The patrol spawn point takes the same grid veto `SpawnFromPlan` uses (`NearestWalkable` fallback). |
| 4 | `StartWaypointRun`'s index-0 contract is made honest: the planner's snapped start cell is the first waypoint and the creature walks to it (no skip). |
| 5 | Harness: a supercover check over every kit walk mask — for every ordered pair of walkable cells the new test approves, every crossed cell is walkable by construction (asserted), and the count of approved pairs is pinned; the Bresenham defect cases the research measured (segments leaving the mask) are asserted rejected. A patrol beat pin on the operator's seed 2052467817 (route length, waypoint count) captured by running. **As built:** the pin is ONE aggregate `approved,rejected;` for the whole kit rather than per chunk — the plan prescribed that string, so the narrowing is sanctioned, and the cost is that a moved pin says "the kit's walkable geometry changed" without naming the chunk. Because the sampled reference is *looser* at an exact corner than the DDA is (`floor(f + 0.5)` names the upper cell only), no kit-derived check can see the no-corner-cutting rule at all; `CheckCornerRule` — eight hand-built 8×8 cases, four APPROVED and four REFUSED, with negative controls run both ways — is the oracle for that half and is the one walk-grid check that also runs on a box with no kit staged. |

### C2 — ambush trigger (bug)

| # | Decision |
|---|---|
| 1 | The disc is replaced by a cell test: a spot fires when a player's `WorldToCell` position lies in the spot's block (`bx, by`) on a walkable cell. Kind-independent, no tuning constant; `V2.Ambush.RadiusYd` is removed from code and conf. |
| 2 | The arm-time log names every spot (`segment, block (bx,by), chunk, world centre`). |
| 3 | Everything else of B5 stays (own stream, 250 ms scan, stun 20170, eight offsets, GM filter). |

### C3 — Shifting Cache (bug)

| # | Decision |
|---|---|
| 1 | 910030 gets `Data0 = 57`, `Data3 = 1` (consumable), `Data2 = 0`. **As built: in `mod_pdungeon_templates_fix.sql` ONLY** — the two declaring files (`mod_pdungeon_templates.sql:26`, `mod_pdungeon_phase2.sql:20`) are left byte-for-byte untouched on purpose. The updater re-applies any file whose bytes change, and a re-applied `mod_pdungeon_templates.sql` re-runs its wide `DELETE FROM gameobject_template WHERE entry BETWEEN 910000 AND 910099` while the unchanged fix file — the one that restores 910040–910099 — does not re-run behind it. The fix file is the single place that runs last on every database (after the wide DELETE on a fresh one, alone on an existing one), so its copy of 910030 is what survives either way. Consequence to live with: the three rows now differ on purpose, and "tidying" 910030 back into the declaring file arms exactly the landmine the fix file exists to defuse; the fix file's header says so. |
| 2 | Spawn orientation `4.712389f` (−π/2) for every cache (pocket and loop room). |
| 3 | The chest keeps its placeholder loot rows (Frostweave, Runic Healing Potion, Frozen Orb); real tables are a later round. |

### C4 — kit v37: the fog model (kit defect)

| # | Decision |
|---|---|
| 1 | Script 48's fog placement switches from `ShadowfangFog01.m2` to a fog/mist M2 that has **no static geometry** (particle-only), chosen by an M2 scan of the client's MPQs (all `*fog*`/`*mist*`/`*smoke*` models: static triangle count 0, textures present); the scan result and the pick are recorded in the kit README. If no such model exists, the fog placements are dropped. **As built (`5aa75b3`):** the scan is a new read-only tool `scripts/58_m2_static_scan.py` that proves its MD20/`.skin` parser on a control before it prints anything — `ShadowfangFog01` must report its **4 static triangles on 12 vertices**, textured `INTERFACE\BUTTONS\WHITE8X8.BLP`, which is what the operator saw. 224 models matched, 53 were particle-only (`reports/m2_fog_scan_20260908.txt`), and the fallback leg of this row was never needed. The pick is `World\LORDAERON\STRATHOLME\PASSIVEDOODADS\FX\StratholmeSmokeEmitter.M2` — 0 triangles, 1 emitter, same stock archive `common-2.MPQ` as the model it replaces. It was NOT taken by the plan's radius-only ranking, which picks a Zul'Drak water mist whose Z band starts 17 yd above the floor: the two properties that decided it are the **particle-only** reference radius (29.13 vs the control's 33.15; the control's header radius 53.46 is inflated by the very triangles being removed) and a bbox that reaches the floor (Z −8.6..29.5). Ladder if the look fails in game: `SPELLS\AcidCloudBreath_GroundSmoke.m2`, then the `SPELLS\*RadiationFog.m2` family. |
| 2 | Kit chain as in Round B: `KIT_VERSION` 25, two byte-identical builds, staging flip, 51 → 52 → 49 → DLL 497/0, audits (walk masks and anchors must not change: `[6b]` passes against `baseline_walk_v24.json` without re-capture; `pdblock` pins unmoved), deploy `t1b-v37` + `KitDir`, `patch_ini_v37.py`, `mod_pdungeon_chunk_meta.sql` regenerated (kit 25, same rows). **As built: every gate held** — 247/247 byte-identical twice, raw and again after 51+52; `[6b]` green on 244 chunks with no re-capture; 51/52/49 `ALL CHECKS PASS`; DLL 497/0 with both `byte-identical to the oracle` lines; `pdblock --batch 500` 675142/0 and the same 803-byte manifest (trailer `E;13894ce8`); the five composed-layout audits equal to v36. **One expectation in this row was wrong and is worth recording**: a model-name change cannot touch only the 105 fog chunks. A doodad's name lives in **MMDX**, which this kit bakes as ONE kit-wide union table into EVERY file so a `nameId` means the same thing in any copied MCNK payload — so all 245 ADTs are rewritten by construction, and the 6-byte-longer path shifts their offsets by +6. Proven chunk by chunk over all 245: only MMDX (one name entry), MMID, MHDR and MCIN differ, while MVER, MTEX, MWMO, MWID, MDDF, MODF, MFBO/MH2O/MTXF and all 256 MCNK payloads are byte-identical, with 105 fog rows before and after on the same chunk ids. Judge such a diff field by field, never by the file count. |
| 3 | The web planes (`house_p` doodad set) stay; if triangles survive v37, they are the next suspect. |

### C5 — respawn without altars (supersedes B1)

| # | Decision |
|---|---|
| 1 | Altars are removed: no spawn, no script, no binding, no altar notices. The 910058 template row stays in `mod_pdungeon_templates_fix.sql` (unspawned, like 910040; the registry rule "do not prune casually") with a comment "unspawned since Round C". `PDv2Altar.cpp` is deleted (cmake re-configure). **As implemented (`3bedac8`) two things the table did not spell out:** the `GO_ALTAR = 910058` constant is removed from `src/PDDefines.h` (a live enum constant is a symbol, and the gate was "every altar symbol gone"; the id stays documented in the band comment), and 910058's `ScriptName` is cleared to `''` — a name assigned in the DB with no registered script makes `ScriptMgr::CheckIfScriptsInDatabaseExist` log a startup **error** on the operator's very next restart. |
| 2 | The checkpoint is computed, never chosen: the **furthest cleared boss room** = the boss room with the highest `chainIndex` whose boss is dead; if none, the **entrance**. Respawn position = that room's arena centre (`BlockToWorld(bx, by, mid, mid)`, grid-vetoed) or `EntranceWorldPos`. |
| 3 | State: `_roomBlock` (roomIndex → block index) kept from `SpawnFromPlan`; `_checkpointChain` (highest cleared boss chain index, −1 = none) set in `OnMobDied`'s `isRunBoss` branch; both reset with the run. |
| 4 | Death flow unchanged from B1: recorded in `OnUnitDeath`, the release veto holds for one tick, `RespawnPending` resurrects at full health with level-scaled resurrection sickness and teleports to the checkpoint; notice "You return to the entrance, weakened." / "You return to the last boss's hall, weakened." **Amended — operator 2026-09-08 T2: no resurrection sickness.** The call is `ResurrectPlayer(1.0f, false)` — `applySickness` is the core's own switch, false returning at `Player.cpp:4446` before the `CastSpell(this, 15007)` at `:4460` — and both notices lost their "weakened": "You return to the entrance." / "You return to the last boss's hall." |

### C6 — bosses: four spells, no repeats

| # | Decision |
|---|---|
| 1 | Every role-2 member carries **four** rows in `pdungeon_member_spells`: **two rows at `minDiff 1`**, one at 50, one at 75. (The `slot` column is NOT a position in a rotation and does not order anything: `BuildKit` reads it only to tell the filler apart — `slot == MEMBER_SPELL_SLOT_FILLER` (0) becomes `_fillerSpellId`, everything else is appended to `_kit` in row order, `src/PDv2CreatureAI.cpp:664-681`. Two `minDiff 1` rows is what makes difficulty 1 a two-ability rotation; as shipped both of a boss's base rows simply carry `slot = 1`, which is legal because the primary key is `(entry, spellId)` — `mod_pdungeon_member_spells.sql:229-237`.) The existing 1/50/75 rows stay; each boss gains a **second base ability** taken from its own pack's already-authored trash spells (same theme, proven to cast on this core): Dralak — a Holy damage/stun ability from pack 3; Lord Maltrion — a shadow/blood bolt from pack 3; Mor'Kar — a poison/nature ability from pack 3; Scourge Overlord — a shadow ability from pack 4; Mal'Ganis — a fire/shadow ability from pack 5. The exact ids are listed in the plan with the identity test (spell name from `Spell.dbc` matches). |
| 2 | No-repeat: the boss draw excludes entries already drawn in this run while the pool has more distinct entries than boss rooms; otherwise repeats are allowed. Implemented in the pure draw (`PDv2PackDraw`), pinned. |
| 3 | The nathrezim overlap is accepted for now (two models, two names); reskinning Maltrion is a content decision for the loot/content round at the end. |

### C7 — UI: barrier progress, cleared rooms, raid warnings

| # | Decision |
|---|---|
| 1 | `R` tick gains three trailing fields: `segPlanned segKilled segPct` for the **next closed barrier** (the lowest segment whose barrier is still sealed; `0 0 0` when none). The HUD shows "Gate: 18/35 (51 %)" under the run line. |
| 2 | New message `K` (cleared rooms): `bx,by;` per cleared room block, sent in full on HELLO/enter and as a delta after each room clear (`_roomAlive[r] == 0`), budget-checked like `M`. The map paints those blocks green (boss rooms a darker green). |
| 3 | Every `N` notice is shown as a raid warning (`RaidNotice_AddMessage(RaidWarningFrame, …)`) **and** kept in the chat frame. The barrier hint, "The barrier to the boss falls.", "Ambush!" and the respawn notices are the vocabulary. |
| 4 | The deployed `flpdui.lua` is replaced with the module copy (content-identical today); the operator document names the addon reload (`/reload`). |

### C8 — the finale: Chromie, the portal, the reward chest

| # | Decision |
|---|---|
| 1 | On the final boss's death (`bossKilled == bossTotal`, inside `FinishRun`): the module summons **Chromie** (new creature template **910550**, name "Chromie", gnome model 10008, level 80, `UNIT_FLAG_NON_ATTACKABLE`, no gossip, and — **as implemented (`b3ef10a`, `de46c40`) — no script at all**: `ScriptName` is `''` and the instance script drives Chromie by GUID from its own timer, so no name is left in the DB for `ScriptMgr` to fail to resolve) 6 yd from the arena centre facing the boss's corpse, and the **reward chest** (new GO **910068** "Chromie's Cache", type 3, display 259 at `size 2.0`, lock 57, consumable, loot 910068 = placeholder rows: the five FL mats 920100–920104 at 100 %) 4 yd beside her. **As implemented (`de46c40`, `f92700c`)** all three spots are grid-vetoed through the module's own `NearestWalkable(SPAWN_FALLBACK_SNAP_CELLS)` first (with no grid, or nothing walkable that close, the offset stands), and the cache does **not** face the centre directly: its facing is the angle to the centre **minus π/2**, the model-forward correction display 259 was measured into by the operator's "90 Grad nach rechts" on the dead-end caches (T2 2026-09-08, `220a295`, which set those to a fixed `4.712389f`). That term is a **T2 observable**: runde29 §C8 asks whether the cache faces the middle or is turned 90°, and the answer decides whether it stays. |
| 2 | Chromie speaks three lines 4 s apart via the module's own timer (no `creature_text` rows): "Well, that took you long enough! The timeways are humming again." / "Take what the Depths owe you - you have earned every bit of it." / "When you are ready, step through. Azealia is waiting." English, as every module text. **As implemented (`de46c40`)** that timer is a **wrap-safe deadline**, `static_cast<int32>(getMSTime() - nextAtMs) < 0`, not the plan's plain `>=`: `getMSTime()` is a uint32 of milliseconds since start-up and wraps every 49.7 days, and a plain comparison across that wrap would park the finale for another 49 days. The idiom appears nowhere else in the module (the other timers are ages, not deadlines) and is deliberate. |
| 3 | After the third line the **portal** appears (new GO **910067** "Portal to Azealia", type 10 GOOBER, display 9041, ScriptName `go_pdungeon_azealia_portal`): a click teleports the clicking player to `(727, 13611.1, 13655.7, 9.87548, o 4.7776)`. Nobody is teleported automatically; the chest can be looted first. **As implemented (`f92700c`) the portal fires on the TICK AFTER the third line, ≈ 13 s** after the last boss's death (Chromie and the cache at 0 s, the lines at ≈4 / ≈8 / ≈12 s): the 4 s beat spaces the LINES, and `TickFinale` stops advancing the deadline once no line is left, so step 3 is already due when the third line is spoken. A fourth 4 s beat (≈16 s) would leave four silent seconds after "Azealia is waiting." and widen the window in C8.4 by a fifth. |
| 4 | Chromie, chest and portal are torn down with the run (rebuild/`DespawnAll`); a run that is already complete on re-entry spawns them again with the rebuild rule of B0 (same seed re-populates). **Consequence, accepted and documented rather than special-cased:** `FinishRun` sets `_run.complete` at the moment of the kill, so `OnPlayerEnter`'s `runFinished` is already true DURING the finale — any player who zones out and back in, or disconnects and reconnects, inside those ~13 seconds triggers the full rebuild, and `DespawnAll` takes Chromie, the portal **and an un-looted cache** with it. The finale is the first feature for which B0's rule is reachable inside a quarter-minute and costs a reward. No code change: runde29 §C8 carries it as an explicit "do not zone out until you have looted" note and in its *Falls doch* list, so a lost cache is not mis-diagnosed as a summon failure. |
| 5 | Ids registered in `06-custom-ids.md` (GO 910067, 910068; NPC 910550); rows in `mod_pdungeon_templates_fix.sql` (GOs, DELETE list extended) and a new `mod_pdungeon_chromie.sql` (creature template + loot). |

---

## 3. Config keys

Removed: `V2.Ambush.RadiusYd`. Unchanged: all other Round B keys. No new key — the finale, the
checkpoint and the UI need none.

## 4. Acceptance

T1: harness — supercover pins over every kit mask, the patrol beat pin, the no-repeat boss draw
pin, the ambush plan pins unmoved; `--batch 500`, `--decor-batch 3000`, `--roomcap 3000` green;
worldserver builds after the re-configure; codestyle; SQL rows verified read-only; kit v37 chain
byte-identical twice, DLL 497/0, `[6b]` green without re-capture, pins unmoved.
T2 (round document `tools/pd_testlauf_runde29.md`): the patroller walks its beat on the lane and
returns to it after a pull; an armed corner corridor fires when walked; the cache opens once and
faces the room; no altar anywhere, death returns the player to the entrance and, after boss 1, to
boss 1's hall; every boss uses two abilities on difficulty 1 and two different bosses appear; the
gate line counts up and the wall falls as the line reaches the configured percentage, at
which point the line switches to the next gate — the threshold percentage itself is never
displayed, because `EvaluateBarrier` runs synchronously inside `OnMobDied` and the barrier
is already open by the next 1 Hz tick; cleared rooms turn green; notices
appear as raid warnings; Chromie speaks, the cache loots, the portal lands in Azealia; no white
triangles.
