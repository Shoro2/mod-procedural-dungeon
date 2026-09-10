# PDv2 Round E / WP5 — event rooms: "Hold the line" — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. One Opus agent per task; the planner reviews between tasks and does the docs.

**Goal:** Per boss segment a 25 % chance of an optional **event pocket**: a dead-end room off the spine with a friendly NPC in it; a gossip click starts a 60 s defence in which a wave creature spawns every 5 s at the room's rim and attacks the NPC; if the NPC survives, a Shifting Cache appears and every player on the map gets Paragon XP scaled by the loot multiplier. The HUD shows a countdown and the NPC's health while it runs.

**Architecture:** The block planner gains a layout input `eventChancePct` and a `PlacedBlock::isEvent` flag; event pockets are drawn on the layout stream AFTER the chain/pockets/loops and BEFORE the alt draw, so a layout without events is byte-identical to today and the harness default (0 %) keeps every pin. The engine skips event blocks in the pack pass, spawns NPC 910551 (faction 1727: friendly to both player factions, hostile to every pack faction) at the room's entry anchor, and runs an `EventRoom` state machine on the 1 Hz branch (build-time wave draw via `SelectSpawns`, one spawn per 5 s, `isExtra` tags, success = chest + XP, failure = despawn). Two trailing `R` fields feed a HUD line; the `M` map paints the room with its own role letter.

**Tech Stack:** C++17 module (engine-free generator + engine glue), `pdblock` harness, module SQL (world: NPC row; characters: `gen_event_pct`), AIO Lua addon, `ParagonUtils.h` (guarded include).

**Spec:** `docs/superpowers/specs/2026-09-10-pdv2-round-e-loot-talents-design.md` §3 R5 (the spec's `BlockInfo` is `PlacedBlock` in code; "farthest-from-centre first" needs an explicit sort — the caster anchors are the outermost). Recon with `path:line`: `.superpowers/sdd/wp5-recon.md` (copied from the recon report by the controller).

---

## Global Constraints

- Branch `claude/pdv2-round-e-63ac9a2a` in `mod-procedural-dungeon` (HEAD `bce651d`); the WP1 gates on every `src/` task: codestyle, `pdblock --batch 500` / `--decor-batch 3000` / `--roomcap 3000` = 0 failures (**with the harness's `eventChancePct` default 0 every existing pin must hold; new pins run with 100**), staged worldserver build (`--parallel 4`, no install until the close), boot-log diff.
- **Determinism**: every planner draw is `PDRandom` on the layout stream in the documented order; the event coin + placement are appended AFTER `ExtendChain` returns and BEFORE the alt draw (`PDBlockPlan.cpp:1919-1924`) — a seed whose coins all miss produces today's layout byte-for-byte (a harness freeze check proves it). `PD_LAYOUT_VERSION` 4 → **5** (new layout input). Loot/wave rolls that are not layout stay on core `urand`... except the WAVE DRAW, which is `SelectSpawns` on a seeded stream like the ambush (`plan.effectiveSeed ^ PD_EVENT_SEED_MIX ^ segment * PD_SEGMENT_SEED_STEP`), so a re-entered dungeon fields the same wave.
- The manifest format does not change (only `bx, by, chunkId, 0, mask` reach it); an event block is an ordinary room chunk (`ChunkIdFor(theme, Room, mask, alt)`), so `49`/the DLL need nothing — `pdblock --manifest` + `49_pd_compose_blocks.py` on a seed WITH an event proves it.
- Every new conf key has a code default = `.conf.dist`; the layout input `V2.Event.ChancePct` travels like `V2.Branches` (conf → `PDv2Config` → `BlockCfg` → `gen_event_pct` column, clamped 0..100 for the TINYINT); the engine keys (`DurationSec`, `SpawnEverySec`, `CasterPct`, `ParagonXp`) are read live.
- Ids: NPC **910551** (`NPC_EVENT_HOST`, the 910550–910599 v2 block, its row in a NEW file `mod_pdungeon_event.sql` — never in `mod_pdungeon_templates.sql`, whose DELETE range is the trap). Reuses GO 910030 (Shifting Cache — WP1's `PDv2ChestLootScript` injects gear into it automatically on map 760).
- The NPC must be attackable by NPCs and never by players: faction template **1727** (`FactionTemplate.dbc`: group 0, friendGroup 7 = all players, enemyGroup 8 = monsters, friends 469/67 — measured 2026-09-10 against every pack faction: 14/16/21/24/90/233/974/1885/2068 are all group 8 with enemy mask 1, so aggro is mutual), `unit_flags` **0** (NOT Chromie's 514 — `0x200` is `IMMUNE_TO_NPC`, the comment in `mod_pdungeon_chromie.sql` mislabels it), `npcflag` 1 (gossip), `ScriptName 'npc_pdungeon_event'`, and the AI binder (`PDv2CreatureAIBinder::GetCreatureAI`, `src/PDv2CreatureAI.cpp:1919-1957`) must yield for this entry (its proximity aggro is not `_mob`-gated) so the CreatureScript's own passive AI wins.
- Docs in the same commits (`CLAUDE.md`, `README.md`, conf.dist); vault + runde31 by the controller at the close.

---

## File Structure

**Modified (generator):** `src/generator/PDBlockPlan.h` (`BlockCfg::eventChancePct`, `PlacedBlock::isEvent`), `src/generator/PDBlockPlan.cpp` (event coin + placement, validator, `ChainPinString` fourth field), `tests/blockplan_harness.cpp` (buckets, `RunEventPocketChecks`, freeze check).
**Modified (engine):** `src/PDv2Mgr.h/.cpp` (conf, `BlockCfg` fill, `SavePlanToDB`/`LoadPlanFromDB`, `PD_LAYOUT_VERSION`), `src/PDv2InstanceScript.h/.cpp` (`EventRoom`, `SpawnEventRooms`, `StartEvent`, `TickEvents`, the skips, rebuild reset, `EventStateFor`), `src/PDv2CreatureAI.cpp` (binder yield), `src/PDv2UILink.cpp` (`R` fields, `RoleChar` for the map), `src/PDDefines.h` (`NPC_EVENT_HOST`), `src/mod_procedural_dungeon_loader.cpp`.
**Created:** `src/PDv2EventNPC.cpp` (`npc_pdungeon_event` CreatureScript + `EventHostAI`), `data/sql/db-world/mod_pdungeon_event.sql` (910551 + model), `data/sql/db-characters/mod_pdungeon_account_event.sql` (`gen_event_pct`).
**Modified (addon):** `lua_scripts/flpdui.lua` (`ParseRun` second optional group, `hudEvent` line, `ROLE_COLOUR.V`), the deployed copy.
**Docs:** `conf/mod_procedural_dungeon.conf.dist`, `CLAUDE.md`, `README.md`.

---

### Task 1: Planner — the event pocket, its coin, the validator, the harness

**Files:**
- Modify: `src/generator/PDBlockPlan.h` (`BlockCfg` `:125-138`, `PlacedBlock` `:140-160`), `src/generator/PDBlockPlan.cpp` (`GenerateBlockPlan` draw order `:1665-1698`, the DFS return / alt draw `:1899-1924`, `PlacePockets` `:575-616` as the placement template, `ValidateBlockPlan` pocket checks `:1227-1256` and `:1508-1547`, `SegmentOf` `:919-944`, `ChainPinString` `:4729-4761` in the harness), `tests/blockplan_harness.cpp` (`RunOrdinaryRoomCountChecks` `:2306-2381`, batch counting `:7243-7251`, `:7318-7329`, the freeze check)

**Interfaces:**
- Produces:
  ```cpp
  // BlockCfg
  int eventChancePct = 0;          // layout input; per boss segment; 0 = no event pockets (harness default)
  // PlacedBlock
  bool isEvent = false;            // Room-role dead-end pocket with the event host; branchOf = the spine host, chainIndex/detourOf = -1
  // PDBlockPlan.cpp
  constexpr uint32_t PD_EVENT_SEED_MIX = 0xE7E27A5Du;   // engine-side wave stream (Task 4), documented here beside the other mixes
  int EventPocketCount(BlockPlan const&);               // blocks with isEvent
  ```
  Placement (after `ExtendChain` returns success, before the alt draw): for `k = 1..bosses` (segment order): `if (!rng.Chance(cfg.eventChancePct)) continue;` then candidate hosts = spine rooms of segment k that are not the entrance, not a boss, and **not already hosting a pocket or an event** (one hanger per host — the validator's one-pocket-per-host rule extends to events); host drawn uniformly (`rng.Range` as `PlacePockets` does), one `StepCandidates` step off the host into a free field cell, `CommitRoute`; if no host or no free step → the segment's event is DROPPED (counted, `dropped` returned in a `BlockPlan::eventsDropped` stat for the harness — no retry, no backtrack). The block: `role = Room`, `isEvent = true`, `branchOf = host chainIndex`, `roomId` from `roomOf` like every room, single socket back to the host. Draw order note in the header comment of `GenerateBlockPlan`.
  Validator: event blocks excluded from the pocket count/host checks, own checks: `isEvent ⇒ role == Room && exactly one socket && branchOf ≥ 1 && host not boss && host not entrance`; `EventPocketCount ≤ bossRooms`; two events never share a host; an event's host hosts no pocket.
  Harness: `RunOrdinaryRoomCountChecks` buckets `events` (`isEvent`) separately — `pockets = branchOf ≥ 0 && !isEvent`; the three assertions unchanged (events are additive like loops? NO: events are NOT rooms of the budget — `plain == rooms + loops` still holds because events are neither plain nor pockets; add `events <= boss`). New `RunEventPocketChecks(seeds)`: cfg with `eventChancePct = 100` over 300 seeds × rooms {1, 5, 14} × boss {1, 2}: every segment has one event or one drop, `eventsDropped` rate < 5 % overall (print it), every event block passes the validator (it runs anyway), the manifest for one event seed round-trips through `pdblock --manifest` (the harness's existing manifest byte-count pins cover length; the oracle run is Task 6's gate). Freeze: with `eventChancePct = 0` the pinned `PD_CHAIN_PIN`/`PD_CHAIN_PIN_LOOP` and the manifest pins are unchanged (no re-capture allowed in this task — if one moves, STOP).
- `ChainPinString` gains a fourth field `event>x,y;` (empty today for the pinned seeds since they run at 0 %).

- [ ] **Step 1:** Header fields + defaults; `PD_EVENT_SEED_MIX`; `EventPocketCount`.
- [ ] **Step 2:** Placement after the DFS, before the alt draw; `eventsDropped`; comments.
- [ ] **Step 3:** Validator + `SegmentOf` unchanged (branchOf carries it) — assert in a comment.
- [ ] **Step 4:** Harness buckets + `RunEventPocketChecks` + the freeze assertion; build `pdblock`; `--batch 500` (pins hold, check count grows), `--decor-batch 3000`, `--roomcap 3000` (room cap: events add at most `bossRooms` blocks — the manifest budget at `rooms 15, boss 2` + 2 events must stay under `PD_GAME_MANIFEST_BUDGET_B`; if not, the cap constant drops and the commit message says by how much).
- [ ] **Step 5:** Codestyle; commit `feat(planner): event pockets - one optional dead-end room per boss segment (layout input, isEvent)`.

---

### Task 2: Persistence and conf — `gen_event_pct`, `V2.Event.*`, layout version 5

**Files:**
- Create: `data/sql/db-characters/mod_pdungeon_account_event.sql` (`gen_event_pct TINYINT UNSIGNED NOT NULL DEFAULT 0`, the `mod_pdungeon_account_branches.sql` shape: information_schema guard, no AFTER)
- Modify: `src/PDv2Mgr.h` (`PDv2Config`: `int eventChancePct = 25; int eventDurationSec = 60; int eventSpawnEverySec = 5; int eventCasterPct = 10; uint32_t eventParagonXp = 1000;`; `PD_LAYOUT_VERSION = 5`), `src/PDv2Mgr.cpp` (`LoadConfig`: `V2.Event.ChancePct` clamped 0..100 (TINYINT), `DurationSec` 10..600, `SpawnEverySec` 1..60, `CasterPct` 0..100, `ParagonXp` ≥ 0; `GeneratePlan` `cfg.eventChancePct = _config.eventChancePct`; `SavePlanToDB` + `LoadPlanFromDB` carry `gen_event_pct` like `gen_branches`), `conf/mod_procedural_dungeon.conf.dist` (five blocks in the ambush voice; `ChancePct` documented as a LAYOUT input read at generation, the other four live)

- [ ] **Step 1:** SQL file. **Step 2:** conf + fields + `BlockCfg` fill + persistence (read the `gen_branches` precedent `PDv2Mgr.cpp:55-63`, `:335-367`, `:587-644`). **Step 3:** `PD_LAYOUT_VERSION = 5` with its paragraph. **Step 4:** build (`--parallel 4`), codestyle; commit `feat(conf): V2.Event.* keys, gen_event_pct persisted, layout version 5`.

---

### Task 3: The event host NPC — data, gossip script, passive AI, binder yield

**Files:**
- Create: `data/sql/db-world/mod_pdungeon_event.sql`, `src/PDv2EventNPC.cpp`
- Modify: `src/PDDefines.h` (`NPC_EVENT_HOST = 910551` in `PDCreatureEntries` with a comment), `src/PDv2CreatureAI.cpp` (binder: `if (creature->GetEntry() == NPC_EVENT_HOST) return nullptr;` with the reason — its proximity aggro is not tag-gated and the host must never engage), `src/mod_procedural_dungeon_loader.cpp` (`AddPDv2EventNPCScripts()`), `src/PDv2InstanceScript.h` (the two entry points Task 4 implements: `bool StartEvent(Creature* host, Player* starter); void OnEventHostDied(Creature* host);` + `EventState EventStateFor(ObjectGuid host) const` — declare now with stub bodies returning false/no-op so this task links)

**Interfaces:**
- SQL: `creature_template` 910551 `Weary Pilgrim` / subname `Lost in the Depths`, `minlevel/maxlevel 80`, `exp 2`, `faction 1727`, `npcflag 1`, `unit_class 1`, `unit_flags 0`, `type 7`, `rank 1`, `HealthModifier 50`, `DamageModifier 1`, `RegenHealth 0` (the fight is a countdown, not a regen race), `MovementType 0`, `ScriptName 'npc_pdungeon_event'`; `creature_template_model` row with a stock humanoid display (pick one from a Stormwind/Dalaran citizen: `SELECT CreatureID, CreatureDisplayID FROM creature_template_model WHERE CreatureID IN (SELECT entry FROM creature_template WHERE name LIKE '%Citizen%' AND minlevel >= 70) LIMIT 5` read-only via `docs/superpowers/notes/round-e-loot-sql/q.sh`); DELETE-before-INSERT, header comment: why a new file, why faction 1727, why unit_flags 0 (and the 514 mislabel).
- `npc_pdungeon_event : CreatureScript`: `OnGossipHello` — if `EventStateFor(guid) == Idle`: one item `GOSSIP_ICON_BATTLE "Hold the line with me!"`; if Running: text "Hold on…" no items; if Won/Lost: text; `SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature)`; `OnGossipSelect` → `CloseGossipMenuFor`, `script->StartEvent(creature, player)`. `GetAI` → `new EventHostAI(creature)` where `EventHostAI : PassiveAI` (`SetReactState(REACT_PASSIVE)` in ctor, `UpdateAI` no-op beyond `PassiveAI`, `JustDied(Unit*)` → `script->OnEventHostDied(me)`); `IsSummonedBy`/`Reset` nothing. The instance script is reached through `me->GetInstanceScript()` + `dynamic_cast<PDv2InstanceScript*>`.
- [ ] Steps: SQL → define → script file → binder yield → loader → stubs → build + codestyle → commit `feat(event): the event host NPC 910551 - gossip script, passive AI, binder yield`.

---

### Task 4: Engine — the event state machine

**Files:**
- Modify: `src/PDv2InstanceScript.h/.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class EventState : uint8 { Idle, Running, Won, Lost };
  struct EventRoom
  {
      int bx = 0, by = 0, segment = 0;
      ObjectGuid host;                        // NPC 910551
      EventState state = EventState::Idle;
      uint32 deadlineMs = 0;                  // wrap-safe like Finale
      uint32 nextSpawnMs = 0;
      std::vector<SpawnPick> picks;           // drawn once at build time on the event stream
      size_t nextPick = 0;
      std::vector<ObjectGuid> wave;           // spawned wave creatures
      std::vector<std::pair<float, float>> rim;  // world x,y of the room's spawn anchors, sorted farthest-from-centre first
      float chestX = 0, chestY = 0, chestZ = 0;  // the chest anchor (grid-vetoed), fallback block centre
      float z = 0;
  };
  std::vector<EventRoom> _events;             // reset by the rebuild like _ambushes/_finale
  void SpawnEventRooms(BlockPlan const&);     // after SpawnAmbushPlan in the build sequence (:398-442)
  bool StartEvent(Creature* host, Player* starter);
  void TickEvents();                          // 1 Hz branch, BEFORE OnInstanceTick
  void OnEventHostDied(Creature* host);
  EventState EventStateFor(ObjectGuid host) const;
  bool EventHudFields(uint32& secLeft, uint32& npcPct) const;  // the running event, if any (first Running)
  ```
  `SpawnEventRooms`: for each `isEvent` block: anchors via the chunk meta (`RoomAnchors`; entry → NPC spot, chest → chest spot, spawns → rim sorted by distance from the block centre descending, each `BlockToWorld` + `NearestWalkable` veto like the room pass); summon `NPC_EVENT_HOST` at the entry spot (`instance->SummonCreature`, `SetHomePosition`, `SetReputationRewardDisabled(true)`; push to `_spawnedGuids` so the rebuild tears it down); health: `SetDungeonHealth(host, base × difficulty factor)` — reuse the scaling helper so the host scales like the mobs; draw the wave: `SelectSpawns(plan.effectiveSeed ^ PD_EVENT_SEED_MIX ^ (segment * PD_SEGMENT_SEED_STEP), in, out)` with one `RoomRequest{0,false}`, `spawnsPerRoom = DurationSec / SpawnEverySec` (12), `bossRoomAdds 0`, `casterPct = cfg.eventCasterPct`, the account's band/dlvl/affixPct like the ambush (`:3453-3478`), placeholder fallback like `:3479-3486`.
  `StartEvent`: state Idle → Running, `deadlineMs = now + DurationSec × 1000`, `nextSpawnMs = now` (first spawn immediately), notice to every run player "Hold the line! {DurationSec} seconds." (raid warning via `SendNotice`), `host->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP)`.
  `TickEvents` (1 Hz): Running: if the host is gone/dead → Lost; else if `now >= deadlineMs` → Won; else if `now >= nextSpawnMs && nextPick < picks.size()`: spawn `picks[nextPick]` at `rim[nextPick % rim.size()]` (proto: `role`, `casterSpellId`, `roomIndex = PD_ROOM_NONE`, `countsForRun = false`, `isExtra = true`, affix 0), `SpawnTaggedMob(entry, proto, x, y, z)`, `ai->AttackStart(host)` (the host's faction makes it a valid target; `AddThreat` too so a re-evade returns to it), `wave.push_back`, `nextSpawnMs += SpawnEverySec × 1000`, `++nextPick`. Won: notice "The pilgrim lives!", despawn the remaining wave (`DespawnOrUnsummon`, they are `isExtra`, no counters), summon `GO_CHEST` at the chest spot (the `SpawnDeadEndChests` idiom: orientation `4.712389f`, `_decorGuids`), `ForEachRunPlayer`: `IncreaseParagonXP(player, cfg.eventParagonXp × _run.lootMultX100 / 100)` under `#if __has_include("ParagonUtils.h")` (the underground pattern) + a chat line with the amount; host: `SetNpcFlag(GOSSIP)` back so the gossip shows the Won text. Lost: notice "The pilgrim has fallen…", despawn the wave, state Lost. `MarkRunDirty()` on every state change (the HUD tick).
  Skips: `SpawnFromPlan` room loop `if (b.isEvent) continue;` (no pack, no roomIndex, no roomsTotal, no barrier denominator); `SpawnDeadEndChests` skips `isEvent` (no free chest); `SpawnPatrols` needs nothing (spine only).
  Rebuild: `_events.clear()` beside `_finale = Finale{}`.
- [ ] Steps: struct + members → `SpawnEventRooms` → `StartEvent`/`TickEvents`/`OnEventHostDied`/`EventStateFor`/`EventHudFields` → the skips → the 1 Hz hook (before `OnInstanceTick`) → build + codestyle + harness (unchanged) → commit `feat(event): the Hold-the-line state machine - wave, chest, Paragon XP`.

---

### Task 5: Wire + HUD — two trailing `R` fields, the event line, the map colour

**Files:**
- Modify: `src/PDv2UILink.cpp` (`SendRunTick` `:615-682`: append ` eventSecLeft eventNpcPct` after `segPct`, `0 0` when no event runs — from `script->EventHudFields`; `RoleChar`/`SendMap`: an event block → `'V'`), `lua_scripts/flpdui.lua` (`ParseRun` `:169-193`: a second optional `SplitHead(g.tail, 2)` → `eventSec, eventPct` default 0; `hudEvent` FontString under `hudGate` (the `hudGate` style `:567-572`, `RenderCounts` `:720-739`): `"Hold the line |cffffffff%d:%02d|r · |cffff8800%d%%|r"` shown while `eventSec > 0`, hidden otherwise; `ROLE_COLOUR.V = {0.55, 0.25, 0.75}` and `CLEARED_COLOUR.V` (an event room never "clears" — same colour); `hudSep` re-anchored under `hudEvent`, `Hud:SetHeight` + 14), the deployed copy `C:\wowstuff\dcore\lua_scripts\ProcDungeon\flpdui.lua`
- [ ] Steps: server fields → Lua parse/render/colour → `lua52_compiler.exe -p` → grep no numeric fallback → commit `feat(ui): event countdown and host health on the HUD, event rooms on the map` → deploy the Lua copy.

---

### Task 6: Close (controller): build + install + restart, `.pdungeon v2 gen` on a seed with an event, docs, runde31 section 8

Install (`cmake --install`), restart with nobody online (updater applies `mod_pdungeon_event.sql` + `mod_pdungeon_account_event.sql`), boot lines, `.pdungeon v2 info`; conf keys appended to the live conf; oracle run on an event seed (`pdblock --manifest <seed> file` → `49_pd_compose_blocks.py`); `CLAUDE.md`/`README.md`; `06-custom-ids.md` (910551), `09-db-tables.md` (`gen_event_pct`), queue, log; runde31 §8 (what to look for: the purple room on the map, the pilgrim, the gossip, the bar, the wave from the rim, the chest, the XP line).
