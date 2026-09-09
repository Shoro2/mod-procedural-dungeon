# PDv2 Round C / C5–C6 — respawn without altars, four boss spells, no repeats — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Altars are gone; a dead player returns alive to the entrance or to the arena of the furthest cleared boss room. Every boss has two abilities at difficulty 1 (plus one at 50 and one at 75), and two boss rooms in one run never draw the same boss while the pool allows it.

**Architecture:** B1's death path (`OnUnitDeath` → `_pendingRespawn` → `RespawnPending`, release veto, `OnPlayerLeave`) stays; only the destination changes, computed from two new per-run facts (`_roomSpot` per room, the highest cleared boss chain index). The altar script, GO spawns, binding and `IsAltarRoom` are deleted. The boss no-repeat rule lives in the pure draw (`PDv2PackDraw.cpp`) and is pinned; the fourth spell is data in the two member-spell SQL files.

**Tech Stack:** C++17 module + harness (MSVC via PowerShell), worldserver build (cmake re-configure: a .cpp is deleted), module SQL.

**Spec:** `docs/superpowers/specs/2026-09-08-pdv2-round-c-design.md` §2 C5, C6; §1 "Altars", "Bosses". Research: `.superpowers/sdd/c-research-chest-altar-finale-ui.md` §2 (altars, the replacement rule, the removal list), `c-research-bosses-triangles.md` §1 (boss table, spell gating, no-repeat evidence).

---

## Global Constraints

Same as `2026-09-08-pdv2-round-c-c1-c3-bugs.md` (branch, commits, LF, harness gates and build lines, worldserver staged never swapped, read-only DB, `creature_template` 84263–84290 never edited). Additionally: a deleted `.cpp` needs `cmake -S C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk -B C:/wowstuff/dcore_bin` (PowerShell) before the build; the world frame constants in `PDv2WorldMath.h`; SQL files re-apply on the operator's restart when their bytes change, and a file's own DELETE scope decides what a re-apply may wipe (member-spell files own their rows by entry — safe to edit in place).

---

## File Structure

**Deleted:** `src/PDv2Altar.cpp`.
**Modified (engine):** `src/PDv2InstanceScript.h` (altar declarations out; `_roomSpot`, `_roomBX/_roomBY`, `_roomChain`, `_checkpointChain`, `_checkpointRoom` in), `src/PDv2InstanceScript.cpp` (`SpawnAltars`/`BindAltar`/`RespawnAltarFor` out; `SpawnFromPlan` room pass, `OnMobDied` boss branch, `RespawnPending` destination, rebuild resets, `OnPlayerEnter` call site), `src/mod_procedural_dungeon_loader.cpp` (:30, :48), `src/PDv2Commands.cpp` (:214-219 comment), `src/generator/PDBlockPlan.h` (:171-179 `PD_ALTAR_EVERY_N_ROOMS`/`IsAltarRoom` out), `src/generator/PDv2PackDraw.cpp` (boss no-repeat), `tests/blockplan_harness.cpp` (altar check :1934-1940 out; no-repeat check + pin in; spawn-draw pins re-captured).
**Modified (data):** `data/sql/db-world/mod_pdungeon_templates_fix.sql` (910058 comment), `data/sql/db-world/mod_pdungeon_member_spells.sql` (:344-356), `data/sql/db-world/mod_pdungeon_member_spells_undead_demon.sql` (:146-149, :203-206).
**Docs:** `CLAUDE.md` (:22 altar row out, instance-script row), `README.md` (:17), `conf/mod_procedural_dungeon.conf.dist` (any altar sentence), runde29, vault (Task 4).

---

### Task 1: Altars out, the computed checkpoint in (C5)

**Files:**
- Delete: `src/PDv2Altar.cpp`
- Modify: `src/PDv2InstanceScript.h`, `src/PDv2InstanceScript.cpp`, `src/mod_procedural_dungeon_loader.cpp`, `src/generator/PDBlockPlan.h`, `tests/blockplan_harness.cpp` (:1934-1940), `src/PDv2Commands.cpp` (:214-219), `data/sql/db-world/mod_pdungeon_templates_fix.sql` (910058 comment), `CLAUDE.md`, `README.md:17`

**Interfaces:**
- Consumes: `SegmentOf`, `PlacedBlock::chainIndex`, `BlockToWorld`, `NearestWalkable`, `GetWalkGrid()`, `EntranceWorldPos` (all existing).
- Produces (later plans rely on these exact names): in `PDv2InstanceScript.h`
  ```cpp
  struct RoomSpot { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
  std::vector<RoomSpot> _roomSpot;   // per dense roomIndex: the room's grid-vetoed arena centre
  std::vector<int> _roomBX;          // per dense roomIndex: the room's block coordinates (the K message, plan C7)
  std::vector<int> _roomBY;
  std::vector<int> _roomChain;       // per dense roomIndex: PlacedBlock::chainIndex (-1 off the spine)
  int _checkpointChain = -1;         // highest chainIndex among boss rooms whose boss is dead
  int _checkpointRoom = -1;          // that room's dense roomIndex, -1 = none (entrance)
  bool CheckpointSpot(float& x, float& y, float& z) const;   // true = a boss hall, false = use the entrance
  ```

- [ ] **Step 1: Delete the altar.** Remove `src/PDv2Altar.cpp`; in the loader remove `void AddPDv2AltarScripts();` (:30) and the call (:48). In the header remove `BindAltar` (:226), the `Altar` struct + `SpawnAltars` (:273-290), `RespawnAltarFor` (:366-374), `_altars`/`_altarByGuid`/`_boundAltar` (:467-469); keep `_pendingRespawn`, `HasPendingRespawn`, `OnUnitDeath`, `RespawnPending`, `OnPlayerLeave`. In the .cpp remove `SpawnAltars` (:1748-1852), `BindAltar` (:2389-2407), `RespawnAltarFor` (:2409-2419), the `SpawnAltars(*plan)` call in `OnPlayerEnter` and its comment, the `_altars/_altarByGuid/_boundAltar` clears (:742-744, :1750-1752 — keep `_pendingRespawn.clear()`). Remove `PD_ALTAR_EVERY_N_ROOMS`/`IsAltarRoom` from `PDBlockPlan.h` and the harness altar-count check (:1934-1940). `PDv2Commands.cpp:214-219`: reword the comment (typed anchors are what the cache and the spawns stand on). `templates_fix.sql`: the 910058 row stays; its comment becomes `-- Altar of Return: UNSPAWNED since Round C (C5) - the row stays, the script and the spawns are gone`. `CLAUDE.md`: drop the `PDv2Altar.cpp` row, update the instance-script row; `README.md:17`: the respawn sentence (entrance or the furthest cleared boss hall).

- [ ] **Step 2: The per-room facts.** In `SpawnFromPlan`'s room pass (`:1032-1050`), beside `_roomSegment.push_back(...)`:

```cpp
            _roomChain.push_back(b.chainIndex);
            _roomBX.push_back(b.bx);
            _roomBY.push_back(b.by);
            RoomSpot spot;
            {
                double const mid = PD_BLOCK_SIZE_YD / 2.0;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, mid, mid, spot.x, spot.y, spot.z);
                // Grid-vetoed like a spawn point: a 33 yd room's centre is
                // floor, but the veto is what makes that a fact, not a hope.
                if (WalkGrid const* grid = GetWalkGrid())
                {
                    int gcx = 0, gcy = 0;
                    WorldToCell(spot.x, spot.y, gcx, gcy);
                    GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                    GridPoint near;
                    if (!grid->At(cell.x, cell.y) &&
                        NearestWalkable(*grid, cell.x, cell.y, 2, near))
                    {
                        grid->GlobalFromLocalCell(near, gcx, gcy);
                        double wx = 0.0, wy = 0.0;
                        CellCentreToWorld(gcx, gcy, wx, wy);
                        spot.x = static_cast<float>(wx);
                        spot.y = static_cast<float>(wy);
                    }
                }
            }
            _roomSpot.push_back(spot);
```

Clear all four with `_roomSegment.clear()` at `:1032` and in the rebuild branch (`:267-271`); reset `_checkpointChain = -1; _checkpointRoom = -1;` in both places.

- [ ] **Step 3: The checkpoint moves on a boss kill.** In `OnMobDied` (`:591-594`), inside `if (tag->isRunBoss && _run.bossKilled < _run.bossTotal)`:

```cpp
                if (tag->roomIndex < _roomChain.size() && _roomChain[tag->roomIndex] > _checkpointChain)
                {
                    _checkpointChain = _roomChain[tag->roomIndex];
                    _checkpointRoom = static_cast<int>(tag->roomIndex);
                }
```

and the accessor:

```cpp
    bool PDv2InstanceScript::CheckpointSpot(float& x, float& y, float& z) const
    {
        if (_checkpointRoom < 0 || static_cast<size_t>(_checkpointRoom) >= _roomSpot.size())
        {
            return false;
        }
        RoomSpot const& s = _roomSpot[static_cast<size_t>(_checkpointRoom)];
        x = s.x; y = s.y; z = s.z;
        return true;
    }
```

- [ ] **Step 4: The destination.** In `RespawnPending` replace the altar branch (`:2486-2495`) with:

```cpp
            float cx = 0.0f, cy = 0.0f, cz = 0.0f;
            if (CheckpointSpot(cx, cy, cz))
            {
                player->TeleportTo(instance->GetId(), cx, cy, cz + 2.0f, 0.0f);
                sPDv2UILink->SendNotice(player, "You return to the last boss's hall, weakened.");
                LOG_INFO(PD_LOG, "PDv2: {} returned alive to the hall of chain room {} in instance {} after {} ms",
                         player->GetName(), _checkpointChain, instance->GetInstanceId(), waitedMs);
            }
            else if (_haveEntrance)
            {
                ... (existing entrance branch; its LOG_WARN becomes LOG_INFO and loses "seated no altar")
            }
```

Rewrite the header comment above `RespawnPending` (the altar sentences → "entrance or the furthest cleared boss hall, computed, never chosen").

- [ ] **Step 5: Gates.** `cmake -S … -B …` (re-configure), worldserver build exit 0, stage, md5; harness fresh build, `--batch 500` (no pin may move: `IsAltarRoom` was never a draw), `--decor-batch 3000`, `--roomcap 3000`; codestyle; LF. Commit `feat(v2): respawn at the entrance or the furthest cleared boss hall; altars removed`.

---

### Task 2: No boss twice (C6, the draw)

**Files:**
- Modify: `src/generator/PDv2PackDraw.cpp` (:254-270 the `wanted` loop, :303-322 the boss emit)
- Test: `tests/blockplan_harness.cpp` (a distinct-boss check over the batch + `PD_BOSS_NOREPEAT_PIN`; spawn-draw pins re-captured if they move)

- [ ] **Step 1: Harness check first.** In the batch's spawn-draw block (where `SelectSpawns` results are already inspected): for every layout with `bossRooms >= 2` and a boss pool larger than `bossRooms`, collect the first pick of every boss room and `Check` they are pairwise distinct; count such layouts and `Check(count > 0)` (non-vacuous). Add a pinned draw on `MakeCfg(12345u, 4)` with `bossRooms = 4` serialised `"e1,e2,e3,e4;"` as `PD_BOSS_NOREPEAT_PIN`. Build, run: the distinct check must FAIL somewhere in the batch (the research saw a repeat in a real run; if 500 seeds never repeat, raise to the pinned 4-boss config where the probability is high) — record the evidence.

- [ ] **Step 2: The rule.** Before the rooms loop:

```cpp
        // Round C / C6: no boss twice in one run - while the pool has MORE
        // distinct bosses than there are boss rooms. A run asking for more
        // boss rooms than bosses exist falls back to repeats once the fresh
        // ones are used up. One draw per boss room either way, so the stream
        // keeps its shape; only the pool the draw ranges over shrinks.
        size_t bossRoomsTotal = 0;
        for (RoomRequest const& room : in.rooms)
        {
            if (room.isBoss) ++bossRoomsTotal;
        }
        std::vector<uint32_t> drawnBosses;
```

and the emit (`:321`):

```cpp
            if (room.isBoss)
            {
                PackMember const* pick = bossStandIn;
                if (!bosses.empty())
                {
                    std::vector<PackMember> fresh;
                    if (bosses.size() > bossRoomsTotal)
                    {
                        for (PackMember const& m : bosses)
                        {
                            if (std::find(drawnBosses.begin(), drawnBosses.end(), m.entry) == drawnBosses.end())
                            {
                                fresh.push_back(m);
                            }
                        }
                    }
                    pick = fresh.empty() ? WeightedPick(bosses, rng) : WeightedPick(fresh, rng);
                    if (pick)
                    {
                        drawnBosses.push_back(pick->entry);
                    }
                }
                emit(pick, true, 0, out);
            }
```

(`#include <algorithm>` if missing; `fresh` is a plain vector — the contract allows it.)

- [ ] **Step 3: Build, run.** Expected pin moves: ONLY spawn-draw pins whose pinned config drew a repeated boss (say which, with old → new), plus the new `PD_BOSS_NOREPEAT_PIN` captured by running. Layout/decor/critter/chain/ambush pins must not move. Gates green. Commit `feat(generator): a boss appears at most once per run while the pool allows`.

---

### Task 3: Two base abilities per boss (C6, the data)

**Files:**
- Modify: `data/sql/db-world/mod_pdungeon_member_spells.sql:344-356`, `data/sql/db-world/mod_pdungeon_member_spells_undead_demon.sql:146-149,203-206`

- [ ] **Step 1: Verify the AI's kit rule.** Read `src/PDv2CreatureAI.cpp:580-620`: confirm every row with `minDiff <= difficulty` enters the rotation regardless of how many share `slot 1` (if the AI keys the rotation on `slot` positions, use slot **2** for the new row and say so in the report). The BOSS rows use `slot 1` today.

- [ ] **Step 2: The rows.** Add one `minDiff 1` row per boss, cooldown 9000 ms, taken from the same file's authored trash rows (copy the id, cite the source row in the comment):

```sql
  -- Round C / C6: a second base ability per boss (operator, 2026-09-08: "2 Basis, dann je eine auf 50 und 75")
  (84288, 59992, 1,  9000,  1, 1),  -- Cleave            weapon damage   (as 84264)         t0 #2
  (84289, 47864, 1,  9000,  1, 1),  -- Curse of Agony R9 30 yd DoT       (as 84267)         t0 #2
  (84290, 59116, 1,  9000,  1, 1),  -- Poison Cloud      6 yd ground DoT (as 84286)         t0 #2
```

and in the undead/demon file:

```sql
  (25352, 60015, 1,  9000,  1, 1),  -- Shadow Bolt       40 yd single    (as 30203 filler)  t0 #2
  (29620, 69211, 1,  9000,  1, 1),  -- Shadow Bolt       30 yd single    (as 18870 filler)  t0 #2
```

Keep each file's DELETE scope as it is (they own their rows by entry). Fix the wrong `Blood Tap` comment on `64160` (it is Drain Life; research §1.6).

- [ ] **Step 3: Verify read-only** that the five ids exist in `acore_world.spell_dbc`? No — stock spells live in the client DBC; instead confirm each id is already present in the same table for a trash entry (`SELECT entry, spellId FROM acore_world.pdungeon_member_spells WHERE spellId IN (59992,47864,59116,60015,69211)`), which is the "proven to cast" evidence. Commit `feat(data): every boss has two abilities at difficulty 1`.

---

### Task 4: Docs, gates, operator document, vault

- [ ] Gates on a fresh harness and worldserver (from the tip; the re-configure from Task 1 is already in place), codestyle. `CLAUDE.md`/`README.md` rows checked. `conf/mod_procedural_dungeon.conf.dist`: any altar sentence removed.
- [ ] `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde29.md` §C5 (no altar anywhere; die before boss 1 → entrance; after boss 1 → boss 1's arena; after boss 2 → boss 2's arena; the notice text), §C6 (on difficulty 1 every boss uses two abilities — name them per boss; two boss rooms show two different bosses), state table (md5, re-configure note, SQL files that re-apply).
- [ ] Vault (`share-public` `main`, one commit): `06-custom-ids.md` (910058 unspawned since Round C), MIG-017 (deleted `PDv2Altar.cpp` → re-configure; member-spell files re-apply; C5/C6 commits), `12-server-todo.md` Round C row (C5/C6 T1), `claude_log.md` END; runbook check 0 errors.

---

## Self-review

- Spec coverage: C5.1 → Task 1 Step 1; C5.2–C5.3 → Task 1 Steps 2–4; C5.4 → Task 1 Step 4; C6.1 → Task 3; C6.2 → Task 2; C6.3 → no work (accepted); §4 → Task 4.
- Types: `RoomSpot`, `_roomSpot`, `_roomBX`, `_roomBY`, `_roomChain`, `_checkpointChain`, `_checkpointRoom`, `CheckpointSpot` as declared above; plan C7 consumes `_roomBX/_roomBY` and `_roomAlive`.
- Placeholders: pins and md5s from runs; spell ids are copied from authored rows.
