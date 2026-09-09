# PDv2 Round B / B1 — altar and respawn Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An altar in the entrance room and every fifth spine room that binds the run's respawn point; a player who dies on the PDv2 map is returned alive to the bound altar (before any binding: the entrance) with resurrection sickness within a second, never as a ghost, never to Westfall. Plus the typed kit-anchor decoder the rest of Round B reuses and the chest in every loop room.

**Architecture:** Engine-free pieces first (`PDv2SpawnAnchors.h` typed decoder, `IsAltarRoom` rule, harness checks), then the engine: the altar GameObject (910058, GOOBER, `go_pdungeon_altar`), `SpawnAltars` inside the instance's build guard, the death path (`ZoneScript::OnUnitDeath` on the instance script records a pending respawn; the existing 1 Hz `Update` branch performs `ResurrectPlayer` + `SpawnCorpseBones` + teleport), and the release veto (`OnPlayerCanRepopAtGraveyard` on the module's existing `PlayerScript`). Run state stays memory-only.

**Tech Stack:** C++17 AzerothCore module (MSVC via PowerShell), MySQL world SQL (one new `gameobject_template` row in `mod_pdungeon_templates_fix.sql`), the `pdblock` harness. No kit change, no client change.

**Spec:** `docs/superpowers/specs/2026-09-03-pdv2-b1-altar-respawn-design.md` (decisions 1–9, data flow); research `.superpowers/sdd/b1-research.md` (every `path:line`).

---

## Global Constraints

- The B0/B0b plan constraints apply (live checkout, branch `claude/pdv2-round-b-0cf92ad4`, Conventional Commits, `git -c core.autocrlf=false commit`, PowerShell for `cl.exe`/`cmake`, freshness proof, code style script, no server/DB touch, staging only).
- **Adding a new `.cpp` under `src/` requires a cmake re-configure** before the build: `cmake -S C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk -B C:/wowstuff/dcore_bin` then `cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m` (PowerShell, foreground). This plan adds `src/PDv2Altar.cpp`.
- **Header-only additions to `src/generator/` need no cmake change**, but the harness build line stays as in the B0b constraints (the header is included by `PDv2Mgr` and the harness).
- **Determinism:** nothing in this plan draws from any RNG. `SelectSpawns` and the layout are untouched. The layout pins must NOT move; the decor/critter pins must not move (the altar is summoned beside the decor, not planned by it).
- **Reserved ids:** GameObject 910058 (altar) is taken here, 910059 (barrier) reserved for B3; new rows in 910034–910099 live in `data/sql/db-world/mod_pdungeon_templates_fix.sql` with the explicit DELETE list extended (its header rule). The AC updater re-applies the changed file (idempotent DELETE + INSERT).
- **DB access:** read-only SELECTs only (`"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "<QUERY>"`).
- **Client-facing text** (notices) in English.

---

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `src/generator/PDv2SpawnAnchors.h` | Header-only typed anchor decoder: `SpawnAnchor`, `RoomAnchors`, `DecodeRoomAnchors`. Engine-free; shared by `PDv2Mgr`, the harness and (B2) spawn placement. |
| `src/PDv2Altar.cpp` | `go_pdungeon_altar` (`GameObjectScript::OnGossipHello` → `PDv2InstanceScript::BindAltar`) and `AddPDv2AltarScripts()`. |

**Modified:**

| Path | Change |
|---|---|
| `src/generator/PDBlockPlan.h` | `PD_ALTAR_EVERY_N_ROOMS`, `IsAltarRoom`. |
| `src/PDDefines.h` | `GO_ALTAR = 910058`, `GO_BARRIER = 910059`. |
| `src/PDv2Mgr.h/.cpp` | `_chunkRoomAnchors`, `RoomAnchorsFor`, decode in `LoadChunkMeta`. |
| `src/PDv2InstanceScript.h/.cpp` | `OnUnitDeath`, `BindAltar`, `HasPendingRespawn`, `SpawnAltars`, `RespawnPending`, `RespawnAltarFor`, the members, `DespawnAll` clears, loop-room chests in `SpawnDeadEndChests`, `Update` tick call. |
| `src/PDClientLink.cpp` | `OnPlayerCanRepopAtGraveyard` veto on the existing `PDClientLinkPlayerScript`. |
| `src/PDv2Commands.cpp` | `.pdungeon v2 info`: typed-anchor chunk count. |
| `src/mod_procedural_dungeon_loader.cpp` | `AddPDv2AltarScripts()`. |
| `data/sql/db-world/mod_pdungeon_templates_fix.sql` | Row 910058, DELETE list, gap comment. |
| `tests/blockplan_harness.cpp` | Typed-anchor checks + pin, altar-room rule check. |
| `CLAUDE.md` | Rows for the new files. |
| Vault (Task 4) | `06-custom-ids.md`, MIG-017, log, `runde28`. |

---

### Task 1: Typed anchors and the altar-room rule (engine-free)

**Files:**
- Create: `src/generator/PDv2SpawnAnchors.h`
- Modify: `src/generator/PDBlockPlan.h` (after `SegmentOf`), `src/PDv2Mgr.h` (members/accessor), `src/PDv2Mgr.cpp` (`LoadChunkMeta`), `src/PDv2Commands.cpp` (info line)
- Test: `tests/blockplan_harness.cpp` (`KitChunk`, `LoadKitMeta`, new `RunTypedAnchorChecks`, `RunChainChecks` altar count, `RunBatch` wiring)

**Interfaces:**
- Produces: `struct SpawnAnchor { double u; double v; std::string role; }`, `struct RoomAnchors { bool hasEntry; SpawnAnchor entry; bool hasBoss; SpawnAnchor boss; bool hasChest; SpawnAnchor chest; std::vector<SpawnAnchor> spawns; }`, `bool DecodeRoomAnchors(std::string const& json, RoomAnchors& out)`; `constexpr int PD_ALTAR_EVERY_N_ROOMS = 5;` `inline bool IsAltarRoom(PlacedBlock const&)`; `RoomAnchors const* PDv2Mgr::RoomAnchorsFor(int chunkId) const`.

- [ ] **Step 1: The failing harness checks**

In `tests/blockplan_harness.cpp`: `#include "generator/PDv2SpawnAnchors.h"`; `KitChunk` gains `std::string anchorsJson;` and `RoomAnchors typed;` — in `LoadKitMeta`, where the anchors span is decoded, also `chunk.anchorsJson = span; DecodeRoomAnchors(span, chunk.typed);`. Add before `RunBatch`:

```cpp
    // Round B / B1: the typed anchors the altar (and B2's spawn placement)
    // stand on. Every room-role chunk publishes an entry on a walkable cell,
    // the entry is the first point of the flat list (AnchorsFor[0] - the
    // order 48 emits), boss rooms publish a boss, rooms a chest and spawns.
    // Pinned on two chunks, captured by running.
    char const* const PD_ROOM_ANCHOR_PIN_12015 = "";
    char const* const PD_ROOM_ANCHOR_PIN_12215 = "";

    std::string RoomAnchorsString(RoomAnchors const& a)
    {
        char buf[96];
        std::string s;
        if (a.hasEntry) { std::snprintf(buf, sizeof(buf), "E%.4f,%.4f;", a.entry.u, a.entry.v); s += buf; }
        if (a.hasBoss)  { std::snprintf(buf, sizeof(buf), "B%.4f,%.4f;", a.boss.u, a.boss.v); s += buf; }
        if (a.hasChest) { std::snprintf(buf, sizeof(buf), "C%.4f,%.4f;", a.chest.u, a.chest.v); s += buf; }
        for (SpawnAnchor const& p : a.spawns)
        {
            std::snprintf(buf, sizeof(buf), "S%.4f,%.4f,%s;", p.u, p.v, p.role.c_str());
            s += buf;
        }
        return s;
    }

    void RunTypedAnchorChecks()
    {
        if (g_kit.empty()) return;
        int roomChunks = 0;
        for (auto const& kv : g_kit)
        {
            int const id = kv.first;
            int const role = (id % 1000) / 100;
            if (role > 2) continue;                     // corridors publish only an entry
            ++roomChunks;
            KitChunk const& c = kv.second;
            Check(c.typed.hasEntry, "a room chunk publishes no entry anchor", static_cast<uint32_t>(id));
            if (!c.typed.hasEntry) continue;
            int const row = static_cast<int>(c.typed.entry.u / PD_CELL_SIZE_YD);
            int const col = static_cast<int>(c.typed.entry.v / PD_CELL_SIZE_YD);
            bool const inside = row >= 0 && col >= 0 && row < PD_CELLS_PER_BLOCK && col < PD_CELLS_PER_BLOCK;
            Check(inside && c.classes.size() == 64 && c.classes[static_cast<size_t>(row * PD_CELLS_PER_BLOCK + col)] == 'W',
                  "a room chunk's entry anchor is not on a walkable cell", static_cast<uint32_t>(id));
            Check(!c.anchors.empty() && c.anchors[0].u == c.typed.entry.u && c.anchors[0].v == c.typed.entry.v,
                  "the entry anchor is not the first point of the flat anchor list", static_cast<uint32_t>(id));
            if (role == 2)
            {
                Check(c.typed.hasBoss, "a boss chunk publishes no boss anchor", static_cast<uint32_t>(id));
                Check(c.typed.spawns.size() >= 2, "a boss chunk publishes fewer than two spawn anchors", static_cast<uint32_t>(id));
            }
            if (role == 0)
            {
                Check(c.typed.hasChest, "a room chunk publishes no chest anchor", static_cast<uint32_t>(id));
                Check(c.typed.spawns.size() >= 5, "a room chunk publishes fewer than five spawn anchors", static_cast<uint32_t>(id));
            }
            if (role == 1)
            {
                Check(c.typed.spawns.empty(), "the entrance chunk publishes spawn anchors", static_cast<uint32_t>(id));
            }
        }
        Check(roomChunks > 0, "no room chunk in kit_meta.json", 0);
        auto pin = [&](int id, char const* want)
        {
            auto it = g_kit.find(id);
            if (it == g_kit.end()) { Check(false, "pinned chunk missing from kit_meta.json", static_cast<uint32_t>(id)); return; }
            std::string const got = RoomAnchorsString(it->second.typed);
            if (got != want)
            {
                std::string const why = "the typed anchors of chunk " + std::to_string(id) + " moved: " + got;
                Check(false, why.c_str(), static_cast<uint32_t>(id));
            }
        };
        pin(12015, PD_ROOM_ANCHOR_PIN_12015);
        pin(12215, PD_ROOM_ANCHOR_PIN_12215);
    }
```

Call `RunTypedAnchorChecks();` in `RunBatch` right after `RunLayoutFreezeCheck();`. In `RunChainChecks`, inside the per-seed block after the chain is verified, add:

```cpp
                // Round B / B1: the altar rooms are the entrance and every
                // fifth spine room; pockets and loop rooms never carry one.
                int altars = 0;
                bool entranceHasAltar = false;
                for (PlacedBlock const& b : plan.blocks)
                {
                    if (!IsAltarRoom(b)) continue;
                    ++altars;
                    Check(b.chainIndex >= 0, "an altar room is not a spine room", seed);
                    if (b.role == BlockRole::RoomEntrance) entranceHasAltar = true;
                }
                Check(entranceHasAltar, "the entrance has no altar", seed);
                Check(altars == (wantChain - 1) / PD_ALTAR_EVERY_N_ROOMS + 1, "altar count is not floor((L-1)/5)+1", seed);
```

Build → compile errors (`PDv2SpawnAnchors.h`, `IsAltarRoom` missing) = the failing state.

- [ ] **Step 2: The header**

Create `src/generator/PDv2SpawnAnchors.h`:

```cpp
/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * (GPL v2 header as in the sibling files)
 */

#ifndef MOD_PDUNGEON_V2_SPAWN_ANCHORS_H
#define MOD_PDUNGEON_V2_SPAWN_ANCHORS_H

#include <cstdlib>
#include <string>
#include <vector>

// PDv2 typed kit anchors (Round B / B1). The kit publishes, per chunk, the
// JSON 48_gen_t1_blockkit.py writes into `pdungeon_chunk_meta`.`anchors`:
//   {"entry":{u,v,z},"boss":{u,v,z}|null,"chest":{u,v,z}|null,
//    "spawns":[{u,v,z,"role":"melee|caster|elite|patrol"},...],"props":[...]}
// DecodeAnchorList (PDv2DecorPlan.h) flattens every point into one clearance
// list on purpose; this decoder keeps the KIND, which the altar (entry), the
// chest (chest) and B2's spawn placement (boss, spawns) need. A scanner like
// its sibling, for the same reason: one generated writer, no JSON dependency.
// Engine-free: the harness proves it against kit_meta.json.
namespace PDungeon
{
    struct SpawnAnchor
    {
        double u = 0.0;             // block-local yards, south
        double v = 0.0;             // block-local yards, east
        std::string role;           // spawns only; empty otherwise
    };

    struct RoomAnchors
    {
        bool hasEntry = false;
        SpawnAnchor entry;
        bool hasBoss = false;
        SpawnAnchor boss;
        bool hasChest = false;
        SpawnAnchor chest;
        std::vector<SpawnAnchor> spawns;
    };

    namespace SpawnAnchorDetail
    {
        // The number after `key":` between `from` and `limit`; false if absent.
        inline bool ReadNumberAfter(std::string const& json, size_t from, size_t limit,
                                    char const* key, double& out, size_t& after)
        {
            size_t const k = json.find(key, from);
            if (k == std::string::npos || k >= limit)
            {
                return false;
            }
            size_t const colon = json.find(':', k);
            if (colon == std::string::npos || colon >= limit)
            {
                return false;
            }
            char const* begin = json.c_str() + colon + 1;
            char* end = nullptr;
            out = std::strtod(begin, &end);
            if (end == begin)
            {
                return false;
            }
            after = static_cast<size_t>(end - json.c_str());
            return true;
        }

        // One {"u":..,"v":..,"z":..[,"role":".."]} object whose '{' is at `open`.
        inline bool ReadPoint(std::string const& json, size_t open, SpawnAnchor& out, size_t& close)
        {
            if (open == std::string::npos || open >= json.size() || json[open] != '{')
            {
                return false;
            }
            close = json.find('}', open);
            if (close == std::string::npos)
            {
                return false;
            }
            size_t after = 0;
            if (!ReadNumberAfter(json, open, close, "\"u\"", out.u, after) ||
                !ReadNumberAfter(json, open, close, "\"v\"", out.v, after))
            {
                return false;
            }
            out.role.clear();
            size_t const r = json.find("\"role\"", open);
            if (r != std::string::npos && r < close)
            {
                size_t const colon = json.find(':', r);
                size_t const q1 = (colon == std::string::npos) ? std::string::npos : json.find('"', colon + 1);
                size_t const q2 = (q1 == std::string::npos) ? std::string::npos : json.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos && q2 < close)
                {
                    out.role = json.substr(q1 + 1, q2 - q1 - 1);
                }
            }
            return true;
        }

        // Where the value of a top-level key starts: '{' for an object, 'n'
        // for null, npos when the key is absent.
        inline size_t ValueStart(std::string const& json, char const* key)
        {
            size_t const k = json.find(key);
            if (k == std::string::npos)
            {
                return std::string::npos;
            }
            size_t const colon = json.find(':', k);
            if (colon == std::string::npos)
            {
                return std::string::npos;
            }
            return json.find_first_not_of(" \t\r\n", colon + 1);
        }
    }

    // False only on a malformed blob; an absent or null point simply leaves
    // its has* flag false.
    inline bool DecodeRoomAnchors(std::string const& json, RoomAnchors& out)
    {
        using namespace SpawnAnchorDetail;
        out = RoomAnchors();
        size_t close = 0;
        size_t at = ValueStart(json, "\"entry\"");
        if (at != std::string::npos && json[at] == '{')
        {
            if (!ReadPoint(json, at, out.entry, close)) return false;
            out.hasEntry = true;
        }
        at = ValueStart(json, "\"boss\"");
        if (at != std::string::npos && json[at] == '{')
        {
            if (!ReadPoint(json, at, out.boss, close)) return false;
            out.hasBoss = true;
        }
        at = ValueStart(json, "\"chest\"");
        if (at != std::string::npos && json[at] == '{')
        {
            if (!ReadPoint(json, at, out.chest, close)) return false;
            out.hasChest = true;
        }
        size_t const spawnsKey = json.find("\"spawns\"");
        if (spawnsKey == std::string::npos)
        {
            return true;
        }
        size_t const open = json.find('[', spawnsKey);
        size_t const end = (open == std::string::npos) ? std::string::npos : json.find(']', open);
        if (open == std::string::npos || end == std::string::npos)
        {
            return false;
        }
        size_t p = json.find('{', open);
        while (p != std::string::npos && p < end)
        {
            SpawnAnchor a;
            if (!ReadPoint(json, p, a, close)) return false;
            out.spawns.push_back(a);
            p = json.find('{', close);
        }
        return true;
    }
}

#endif
```

In `src/generator/PDBlockPlan.h`, after `SegmentOf`:

```cpp
    // Round B / B1: an altar (the run's respawn point) stands in the entrance
    // and in every PD_ALTAR_EVERY_N_ROOMS-th spine room by chain index.
    // Pockets and loop rooms carry chainIndex -1 and never qualify.
    constexpr int PD_ALTAR_EVERY_N_ROOMS = 5;

    inline bool IsAltarRoom(PlacedBlock const& block)
    {
        return block.chainIndex >= 0 && block.chainIndex % PD_ALTAR_EVERY_N_ROOMS == 0;
    }
```

- [ ] **Step 3: The manager**

`src/PDv2Mgr.h`: `#include "generator/PDv2SpawnAnchors.h"`; after `AnchorsFor` declare

```cpp
        // The same row's anchors with their KINDS kept (entry, boss, chest,
        // spawns) - what the altar, the loop-room chest and B2's spawn
        // placement read. nullptr for a chunk the SQL does not know.
        RoomAnchors const* RoomAnchorsFor(int chunkId) const;
        size_t RoomAnchorChunkCount() const { return _chunkRoomAnchors.size(); }
```
and the member `std::unordered_map<int, RoomAnchors> _chunkRoomAnchors;`. In `LoadChunkMeta`, after the flat decode: `RoomAnchors typed; if (!DecodeRoomAnchors(anchorText, typed)) { LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed typed anchors field", chunkId); } _chunkRoomAnchors[chunkId] = std::move(typed);` (clear the map at the top beside `_chunkAnchors.clear()`). Implement `RoomAnchorsFor` beside `AnchorsFor`. In `.pdungeon v2 info` extend the walk-mask line: `"pdungeon v2: {} walk mask(s) loaded, {} with typed anchors"`.

- [ ] **Step 4: Build, capture the two pins, gates, commit**

Build the harness (PowerShell), prove freshness, `--batch 500`: expected failures only `the typed anchors of chunk 12015 moved: …` and `… 12215 moved: …` — paste both strings into the pin constants, rebuild, `--batch 500` ALL CHECKS PASS (no layout/decor/critter pin moved), `--decor-batch 3000`, `--roomcap 3000` green. Worldserver: **no new .cpp yet**, plain `cmake --build` → exit 0. Code style. Commit:

```bash
git add src/generator/PDv2SpawnAnchors.h src/generator/PDBlockPlan.h src/PDv2Mgr.h src/PDv2Mgr.cpp src/PDv2Commands.cpp tests/blockplan_harness.cpp
git -c core.autocrlf=false commit -m "feat(v2): typed kit anchors and the altar-room rule"
```

---

### Task 2: The altar object, its placement and the loop-room chest

**Files:**
- Modify: `src/PDDefines.h`, `data/sql/db-world/mod_pdungeon_templates_fix.sql`, `src/PDv2InstanceScript.h/.cpp`, `src/mod_procedural_dungeon_loader.cpp`
- Create: `src/PDv2Altar.cpp`

**Interfaces:**
- Consumes: `RoomAnchorsFor`, `IsAltarRoom`, `GetWalkGrid`, `BlockToWorld`, `WorldToCell`, `GridPoint`/`LocalFromGlobalCell`/`At` (as `SplitOnDeath` uses them), `SendNotice` (call it exactly the way the instance script already does — grep `SendNotice` in `PDv2InstanceScript.cpp`).
- Produces: `bool PDv2InstanceScript::BindAltar(Player*, ObjectGuid const&)`, `_altars` (chain order, `[0]` the entrance's), `RespawnAltarFor(ObjectGuid const&)`.

- [ ] **Step 1: Ids and the SQL row**

`src/PDDefines.h`, in `PDGameObjectEntries` after `GO_ENTRANCE_DECO`:

```cpp
        GO_ENTRANCE_DECO = 910033,
        // Round B (2026-09-03): gameplay objects in the 910050+ band that
        // mod_pdungeon_templates_fix.sql owns. 910059's row lands with B3.
        GO_ALTAR         = 910058,  // B1: respawn altar, type 10, display 7355
        GO_BARRIER       = 910059   // B3: boss-room barrier
```

`data/sql/db-world/mod_pdungeon_templates_fix.sql`: the gap comment reads `910048-910049, 910059, 910067-910069, 910077-910099 are unused gaps`; the DELETE list gains `910058` on its own line after the `910050…910057` line; the INSERT gains, after the last clutter row (change that row's `;` to `,`):

```sql
-- Round B / B1: the respawn altar. type 10 GOOBER so OnGossipHello fires on
-- click (GameObject::Use, no GO_FLAG_NOT_SELECTABLE); display 7355 Altar01.m2
-- resolves in GameObjectDisplayInfo.dbc, has a collision model in
-- GameObjectModels.dtree, and is the display of the stock 'WotLK Light Altar'
-- (gameobject_template 190741) - the three-way check this file's header asks for.
(910058, 10, 7355, 'Altar of Return', 1, 0, 0, 'go_pdungeon_altar');
```

Read-only verification: `SELECT entry FROM gameobject_template WHERE entry = 910058` → no row; `SELECT entry, name FROM gameobject_template WHERE displayId = 7355 LIMIT 1` → `190741 WotLK Light Altar`.

- [ ] **Step 2: Instance state and placement**

`src/PDv2InstanceScript.h` — public, after `OnMobDied`:

```cpp
        // Round B / B1. A player died on this map: ZoneScript hook, reached
        // through GetInstanceScript() from Unit::setDeathState - before any
        // corpse or ghost exists. Only records the death; the resurrect runs
        // on the 1 Hz tick (RespawnPending), never inside the death itself.
        void OnUnitDeath(Unit* unit) override;

        // The player clicked an altar: binds the run's respawn point to it.
        // False for a GameObject that is not one of this instance's altars.
        bool BindAltar(Player* player, ObjectGuid const& altarGuid);

        // True while a death is waiting for its tick; the release veto in
        // PDClientLink reads it so a quick 'release spirit' cannot beat the
        // tick to the graveyard.
        bool HasPendingRespawn(ObjectGuid const& playerGuid) const
        {
            return _pendingRespawn.find(playerGuid) != _pendingRespawn.end();
        }
```
private, after `SpawnDeadEndChests`:

```cpp
        // Round B / B1. One altar per altar room (IsAltarRoom), in chain
        // order, on a walkable cell beside the room's entry anchor. The
        // respawn spot itself is the entry anchor. Same guard and teardown
        // as the decor.
        struct Altar
        {
            int chainIndex = 0;
            float x = 0.0f;         // the respawn spot (entry anchor), world
            float y = 0.0f;
            float z = 0.0f;
            ObjectGuid guid;        // the altar GameObject; empty when none could be seated
        };
        void SpawnAltars(BlockPlan const& plan);
        void RespawnPending();
        Altar const* RespawnAltarFor(ObjectGuid const& playerGuid) const;
```
members (beside `_roomAlive`):

```cpp
        std::vector<Altar> _altars;                              // chain order; [0] = the entrance's
        std::unordered_map<ObjectGuid, size_t> _altarByGuid;     // altar GO -> index into _altars
        std::unordered_map<ObjectGuid, size_t> _boundAltar;      // player -> index into _altars
        std::unordered_map<ObjectGuid, uint32> _pendingRespawn;  // player -> getMSTime() at death
```
(`#include <unordered_map>`; `ObjectGuid` hashes with the core's `std::hash<ObjectGuid>`.)

`src/PDv2InstanceScript.cpp` — in the `!_spawned` build block after `SpawnDeadEndChests(*plan);` add `SpawnAltars(*plan);`. In `DespawnAll`, after `_decorGuids.clear();` add `_altars.clear(); _altarByGuid.clear(); _boundAltar.clear(); _pendingRespawn.clear();`. Add the functions (beside `SpawnDeadEndChests`):

```cpp
    void PDv2InstanceScript::SpawnAltars(BlockPlan const& plan)
    {
        _altars.clear();
        _altarByGuid.clear();
        _boundAltar.clear();
        _pendingRespawn.clear();

        std::vector<PlacedBlock const*> rooms;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (IsAltarRoom(b))
            {
                rooms.push_back(&b);
            }
        }
        std::sort(rooms.begin(), rooms.end(), [](PlacedBlock const* a, PlacedBlock const* b)
        {
            return a->chainIndex < b->chainIndex;
        });

        WalkGrid const* grid = GetWalkGrid();
        uint32 placed = 0;
        for (PlacedBlock const* b : rooms)
        {
            RoomAnchors const* anchors = sPDv2Mgr->RoomAnchorsFor(b->chunkId);
            if (!anchors || !anchors->hasEntry)
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} chunk {} publishes no entry anchor - "
                                 "no altar in chain room {}",
                         instance->GetInstanceId(), b->chunkId, b->chainIndex);
                continue;
            }

            Altar altar;
            altar.chainIndex = b->chainIndex;
            sPDv2Mgr->BlockToWorld(b->bx, b->by, anchors->entry.u, anchors->entry.v,
                                   altar.x, altar.y, altar.z);

            // The altar stands one cell beside the entry anchor, off the socket
            // track (the block's centre row and column), on a cell the walk
            // grid calls floor - checked in WORLD coordinates through the same
            // conversion SplitOnDeath trusts, so no axis assumption is made.
            int const entryRow = static_cast<int>(anchors->entry.u / PD_CELL_SIZE_YD);
            int const entryCol = static_cast<int>(anchors->entry.v / PD_CELL_SIZE_YD);
            int const tries[4][2] = { { -1, 0 }, { 0, -1 }, { 0, 1 }, { 1, 0 } };   // N, W, E, S
            bool seated = false;
            for (auto const& t : tries)
            {
                int const row = entryRow + t[0];
                int const col = entryCol + t[1];
                if (row < 0 || col < 0 || row >= PD_CELLS_PER_BLOCK || col >= PD_CELLS_PER_BLOCK)
                {
                    continue;
                }
                if (row == PD_CELLS_PER_BLOCK / 2 || col == PD_CELLS_PER_BLOCK / 2)
                {
                    continue;       // the socket track: the one line every player walks
                }
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                sPDv2Mgr->BlockToWorld(b->bx, b->by, (row + 0.5) * PD_CELL_SIZE_YD,
                                       (col + 0.5) * PD_CELL_SIZE_YD, ax, ay, az);
                if (grid)
                {
                    int gcx = 0, gcy = 0;
                    WorldToCell(ax, ay, gcx, gcy);
                    GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                    if (!grid->At(cell.x, cell.y))
                    {
                        continue;
                    }
                }
                float const facing = std::atan2(altar.y - ay, altar.x - ax);
                GameObject* go = instance->SummonGameObject(GO_ALTAR, ax, ay, az, facing,
                                                            0.0f, 0.0f, 0.0f, 0.0f, 0);
                if (!go)
                {
                    LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon the altar "
                                      "(missing gameobject_template {}?)",
                              instance->GetInstanceId(), uint32(GO_ALTAR));
                    break;
                }
                _decorGuids.push_back(go->GetGUID());
                altar.guid = go->GetGUID();
                _altarByGuid[go->GetGUID()] = _altars.size();
                seated = true;
                ++placed;
                break;
            }
            if (!seated)
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} found no cell for the altar in chain room {} "
                                 "- the room keeps its respawn spot without an altar",
                         instance->GetInstanceId(), b->chainIndex);
            }
            _altars.push_back(altar);
        }
        LOG_INFO(PD_LOG, "PDv2: instance {} placed {} altar(s) in {} altar room(s)",
                 instance->GetInstanceId(), placed, uint32(_altars.size()));
    }

    bool PDv2InstanceScript::BindAltar(Player* player, ObjectGuid const& altarGuid)
    {
        auto const it = _altarByGuid.find(altarGuid);
        if (!player || it == _altarByGuid.end())
        {
            return false;
        }
        auto const bound = _boundAltar.find(player->GetGUID());
        if (bound != _boundAltar.end() && bound->second == it->second)
        {
            PDv2UILink::SendNotice(player, "This altar is already yours.");
            return true;
        }
        _boundAltar[player->GetGUID()] = it->second;
        PDv2UILink::SendNotice(player, "Altar bound. If you fall, you return here.");
        LOG_DEBUG(PD_LOG, "PDv2: {} bound the altar of chain room {}",
                  player->GetName(), _altars[it->second].chainIndex);
        return true;
    }

    PDv2InstanceScript::Altar const* PDv2InstanceScript::RespawnAltarFor(ObjectGuid const& playerGuid) const
    {
        auto const bound = _boundAltar.find(playerGuid);
        if (bound != _boundAltar.end() && bound->second < _altars.size())
        {
            return &_altars[bound->second];
        }
        return _altars.empty() ? nullptr : &_altars[0];
    }
```
(`SendNotice` — use the exact call form the file already uses; if it is a member of a singleton, write it that way. `#include <algorithm>` for `std::sort`; `WorldToCell` and `GridPoint` are what `SplitOnDeath` already uses in this file.)

- [ ] **Step 3: The loop-room chest**

In `SpawnDeadEndChests`, widen the predicate: a block qualifies when `b.role == BlockRole::CorridorDeadEnd` (chest at the block centre, as today) **or** `b.detourOf >= 0` (chest at `RoomAnchorsFor(b.chunkId)->chest` when `hasChest`, else at the block centre with a `LOG_WARN`). Update the function's comment ("One Shifting Cache per dead-end stub … and per loop room (B0b), on the kit's chest anchor") and the log line to count both.

- [ ] **Step 4: The click script**

Create `src/PDv2Altar.cpp`:

```cpp
/* (GPL header as in the sibling files) */

#include "GameObject.h"
#include "PDv2InstanceScript.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace PDungeon;

// Round B / B1: the respawn altar. Binding lives on the instance script;
// this script only carries the click to it. Never despawns on use.
class go_pdungeon_altar : public GameObjectScript
{
public:
    go_pdungeon_altar() : GameObjectScript("go_pdungeon_altar") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (auto* script = dynamic_cast<PDv2InstanceScript*>(go->GetInstanceScript()))
        {
            script->BindAltar(player, go->GetGUID());
        }
        return true;
    }
};

void AddPDv2AltarScripts()
{
    new go_pdungeon_altar();
}
```
Loader: declare `void AddPDv2AltarScripts();` and call it after `AddPDv2UILinkScripts();`.

- [ ] **Step 5: Re-configure, build, commit**

PowerShell: `cmake -S C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk -B C:/wowstuff/dcore_bin` then the build → exit 0. Harness unchanged by this task, but rebuild + `--batch 500` once as a regression guard. Code style. Commit:

```bash
git add src/PDDefines.h data/sql/db-world/mod_pdungeon_templates_fix.sql src/PDv2InstanceScript.h src/PDv2InstanceScript.cpp src/PDv2Altar.cpp src/mod_procedural_dungeon_loader.cpp
git -c core.autocrlf=false commit -m "feat(v2): respawn altars in the entrance and every fifth spine room, loop-room chests"
```

---

### Task 3: The death path

**Files:**
- Modify: `src/PDv2InstanceScript.cpp` (`OnUnitDeath`, `RespawnPending`, the `Update` tick), `src/PDClientLink.cpp` (the veto)

- [ ] **Step 1: Death and the tick**

```cpp
    void PDv2InstanceScript::OnUnitDeath(Unit* unit)
    {
        if (!unit || unit->GetTypeId() != TYPEID_PLAYER)
        {
            return;
        }
        // Recorded only. The core is still inside Unit::Kill -> KillPlayer at
        // this point (Unit.cpp:11439), so the resurrect waits for the tick.
        _pendingRespawn[unit->GetGUID()] = getMSTime();
        LOG_DEBUG(PD_LOG, "PDv2: {} died in instance {} - respawn on the next tick",
                  unit->GetName(), instance->GetInstanceId());
    }

    void PDv2InstanceScript::RespawnPending()
    {
        if (_pendingRespawn.empty())
        {
            return;
        }
        for (auto it = _pendingRespawn.begin(); it != _pendingRespawn.end();)
        {
            Player* player = ObjectAccessor::GetPlayer(instance, it->first);
            if (!player || !player->IsInWorld() || player->GetMap() != instance)
            {
                it = _pendingRespawn.erase(it);      // left the map: the core owns them now
                continue;
            }
            if (player->IsAlive())
            {
                it = _pendingRespawn.erase(it);      // someone else resurrected them
                continue;
            }
            float x = _entranceX, y = _entranceY, z = _entranceZ;
            int chainIndex = 0;
            if (Altar const* altar = RespawnAltarFor(it->first))
            {
                x = altar->x;
                y = altar->y;
                z = altar->z;
                chainIndex = altar->chainIndex;
            }
            // Alive, full health, resurrection sickness scaled by level (the
            // core's own rule inside ResurrectPlayer); no durability loss.
            // SpawnCorpseBones is a no-op when the player never released.
            player->ResurrectPlayer(1.0f, true);
            player->SpawnCorpseBones();
            player->TeleportTo(instance->GetId(), x, y, z + 2.0f, 0.0f);
            PDv2UILink::SendNotice(player, "You return to the altar, weakened.");
            LOG_INFO(PD_LOG, "PDv2: {} returned alive to the altar of chain room {} in instance {}",
                     player->GetName(), chainIndex, instance->GetInstanceId());
            it = _pendingRespawn.erase(it);
        }
    }
```
`#include "ObjectAccessor.h"` and `#include "Timer.h"` (for `getMSTime`). In `Update`'s 1 Hz branch, call `RespawnPending();` right after `CatchFallers();`.

- [ ] **Step 2: The release veto**

`src/PDClientLink.cpp`: `#include "PDv2InstanceScript.h"`; the ctor list gains `PLAYERHOOK_CAN_REPOP_AT_GRAVEYARD`; add to the class:

```cpp
    // Round B / B1: a player who presses 'release' within the second before
    // the instance's tick resurrects them must not be sent to a graveyard -
    // zone 5100 has none, and the core's fallback is Westfall. While a
    // respawn is pending the release is simply refused; the tick then
    // removes the ghost aura and the corpse on its own.
    bool OnPlayerCanRepopAtGraveyard(Player* player) override
    {
        if (!sPDv2Mgr->IsEnabled() || !player ||
            player->GetMapId() != sPDv2Mgr->GetConfig().mapId)
        {
            return true;
        }
        if (auto* script = dynamic_cast<PDungeon::PDv2InstanceScript*>(player->GetInstanceScript()))
        {
            return !script->HasPendingRespawn(player->GetGUID());
        }
        return true;
    }
```

- [ ] **Step 3: Build, commit**

`cmake --build …` → exit 0 (no new file). Code style. Commit: `feat(v2): death returns the player alive to the bound altar with resurrection sickness`.

---

### Task 4: Docs, gates, restage, vault

- [ ] **Step 1: Module docs.** `CLAUDE.md`: a row for `src/generator/PDv2SpawnAnchors.h` (typed anchors) and `src/PDv2Altar.cpp` (the altar click), and the instance-script row mentions altars/respawn. Commit `docs: altar, respawn and typed anchors in CLAUDE.md`.
- [ ] **Step 2: Gates.** Fresh harness: `--batch 500`, `--decor-batch 3000`, `--roomcap 3000` green; worldserver built and staged (`certutil` md5).
- [ ] **Step 3: Operator document.** `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde28.md` gains a section "B1 — Altar und Respawn": what to see (an altar beside the room centre in the start room and every fifth room; click → "Altar bound"; die → within a second alive at the bound altar, weakened; release during that second does nothing; never Westfall; the fall catcher still returns to the entrance; a loop room carries a chest), the boot line to look for (`placed N altar(s)`), *Erwartet* / *Falls doch*, and the new staged md5 in the state table.
- [ ] **Step 4: Vault (share-public `main`).** `docs/World of Warcraft/06-custom-ids.md`: register 910058 (altar) and 910059 (barrier, reserved) in the PDv2 GO block; MIG-017 bullet: B1 commits + "templates_fix.sql re-applied by the updater (row 910058)"; `claude_log.md` END entry; runbook check 0 errors; one commit `docs(pdv2): B1 altar/respawn - ids registered, MIG-017, runde28`.

---

## Self-review

- Spec coverage: decisions 1–2 → Task 3; 3–6 → Task 2 (`SpawnAltars`, `RespawnAltarFor`, `BindAltar`); 7 → Task 2 Step 1; 8 → Task 1 (header + manager); 9 → Task 1 harness; §5 of the B0b spec (loop-room chest) → Task 2 Step 3.
- Types: `RoomAnchors`/`SpawnAnchor`/`DecodeRoomAnchors` consistent across header, manager, harness; `Altar` struct fields used consistently by `SpawnAltars`/`RespawnAltarFor`/`RespawnPending`; `HasPendingRespawn(ObjectGuid const&)` used by the veto.
- Placeholders: the two anchor pins are captured by running; the md5 at execution.
