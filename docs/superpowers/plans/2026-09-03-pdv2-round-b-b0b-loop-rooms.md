# PDv2 Round B / B0b — loop rooms instead of shortcuts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the shortcut mechanism from the B0 chain generator and add loop rooms — an extra room beside a straight three-cell corridor run of the spine, entered from the run and left back into it two cells further, 33 % per boss segment, additional to the room budget — with the validator, the harness, the config and the docs following.

**Architecture:** Everything structural stays in the engine-free `src/generator/PDBlockPlan.{h,cpp}` and is proven by `tests/blockplan_harness.cpp`. A loop room is a **detour step** inside the existing depth-first chain search (`ExtendChain`): when the segment the next spine room belongs to wants a loop room and has none, the detour candidates (straight Manhattan-4 run + free side strip) are tried before the ordinary candidates. The validator learns the one sanctioned corridor fork (the two attachment cells of a loop) and reconstructs every loop from its sockets. The engine only renames a config key (`V2.LoopChance` → `V2.DetourChance`, default 33) and keeps persisting it in the existing `gen_loop_pct` column.

**Tech Stack:** C++17 (MSVC 19.44 `cl.exe`; no g++ on this box), AzerothCore 3.3.5a module, MySQL 8.4, the `pdblock` harness. No kit, no DLL, no SQL schema change.

**Specs:** `docs/superpowers/specs/2026-09-03-pdv2-b0b-loop-rooms-design.md` (this change) on top of `2026-09-02-pdv2-b0-spine-generator-design.md` (§3–§7, §11 as built). Read both first.

---

## Global Constraints

Every task's requirements implicitly include all of these (the B0 plan's constraints, unchanged where not restated).

- **Live checkout:** `C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\modules\mod-procedural-dungeon`, branch `claude/pdv2-round-b-0cf92ad4` (tip `a0edda1` + the spec commits). Never commit to `main`.
- **Conventional Commits, English everywhere, `git -c core.autocrlf=false commit`** (its CRLF diffstat is misleading; the stored blobs stay LF).
- **Determinism contract:** no `std::random` distribution, no `unordered_*` iteration, no `std::shuffle` in `src/generator/`; fixed iteration orders; `PDRandom::UniformInt(lo, hi)` returns `lo` WITHOUT drawing at `lo >= hi`; `Chance(pct)` draws nothing at `<= 0` / `>= 100`. Every draw is documented in the DRAW ORDER comment of `GenerateBlockPlan`, in the same commit.
- **Pins are captured by running the harness and reading its failure message**, never by reasoning. Every layout-derived pin moves in this change (the shortcut draw is removed and the detour draws are added): the layout freeze (`RunLayoutFreezeCheck`), `PD_DECOR_PLAN_PIN`, `PD_CRITTER_PLAN_PIN`, `PD_CHAIN_PIN`. The spawn-draw pins do not move. `PD_LAYOUT_VERSION` stays **3** (nothing is deployed; the server is not live).
- **Harness gates before any commit that touches `src/generator/` or the harness:** fresh `pdblock.exe` proven newer than every source, `--batch 500` ALL CHECKS PASS (with the `214 walk mask(s)` line), `--decor-batch 3000` 0 failures, `--roomcap 3000` cap recorded. Build and run from **PowerShell**:

```powershell
cd C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\modules\mod-procedural-dungeon
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp src\generator\PDv2PackDraw.cpp'
```

Freshness proof (Git Bash): `stat -c '%y %n' pdblock.exe src/generator/*.cpp src/generator/*.h tests/blockplan_harness.cpp | sort | tail -3` — `pdblock.exe` last. Expected build noise: exactly one `warning C4456` in the harness.
- **`EmitManifest` is untouched** (three-implementation contract). **`SelectSpawns` and its draw order are untouched.**
- **Code style:** `python "C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\apps\codestyle\codestyle-cpp.py"` from the module root before every commit touching `src/`.
- **No task stops or restarts the operator's worldserver, writes to a database, or copies over `C:\wowstuff\dcore\worldserver.exe`.** A rebuilt binary is staged in `C:\wowstuff\dcore_bin\bin\RelWithDebInfo\`. `cmake --build` runs from PowerShell (`cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m`), foreground, generous timeout; no re-configure (no new `.cpp`).
- **Vocabulary in code:** *loop room* in comments and docs; `detour*` in identifiers (`detourOf`, `detourChancePct`, `DetourCandidate`, `CommitDetour`) — the two words mean the same structure.

---

## File Structure

**Modified:**

| Path | Change |
|---|---|
| `src/generator/PDBlockPlan.h` | `PlacedBlock::shortcutTo` removed, `PlacedBlock::detourOf` added; `BlockCfg::loopChancePct` → `detourChancePct` (33). |
| `src/generator/PDBlockPlan.cpp` | Shortcut draw and `NextBossAfter` removed; segment detour draws, `DetourCandidates`, `CommitDetour`, detour steps in `ExtendChain`, loop rooms materialised; validator: loop-room physics, attachment-aware junction rule and corridor walk; `AsciiBlockDump` `o`. |
| `tests/blockplan_harness.cpp` | Shortcut checks/pins/summary removed; loop-room re-derivation, combos `detourPct 0/100`, `sawDetour`, `ChainSummary` `loops:` line, `ChainPinString` format, batch yield line; pins re-captured. |
| `src/PDv2Mgr.h/.cpp`, `src/PDv2Commands.cpp` | `detourChancePct` / `V2.DetourChance` (33, clamped 0..100), boot + info lines `detour {}%`, `gen_loop_pct` keeps carrying it. |
| `conf/mod_procedural_dungeon.conf.dist` | `V2.LoopChance` block replaced by `V2.DetourChance`. |
| `CLAUDE.md`, `README.md`, spec §11 | Wording: loop rooms, no shortcuts. |
| Vault + workspace (Task 3) | `12-server-todo.md` (shortcut-yield row removed, PDv2 row), `15-host-migration-log.md` MIG-017 bullet, `claude_log.md` END, `tools/pd_testlauf_runde28.md`, deployed conf key. |

---

### Task 1: Remove shortcuts, rename the chance, add the `detourOf` field

**Files:**
- Modify: `src/generator/PDBlockPlan.h` (`BlockCfg` ~:93, `PlacedBlock` ~:113-119)
- Modify: `src/generator/PDBlockPlan.cpp` (`struct Pocket` ~:375, `NextBossAfter` ~:392, `PlacePockets` ~:403-467, DRAW ORDER comment ~:1139-1170, validator pocket block ~:800-830 and rule 3 ~:1000-1050, materialisation ~:1356)
- Modify: `src/PDv2Mgr.h:64,151-158`, `src/PDv2Mgr.cpp:53-54,96-99,146,194-203,340,370`, `src/PDv2Commands.cpp:201-204`, `conf/mod_procedural_dungeon.conf.dist:298-307`
- Test: `tests/blockplan_harness.cpp` (`RunChainMathChecks` ~:920, `RunThemeParityChecks` ~:1472, `ChainSummary` ~:294-372, `RunChainChecks` ~:1587-1868, `RunBatch` ~:3395-3411 and ~:3560-3585, `PD_CHAIN_PIN`/`ChainPinString` ~:2740-2765)

**Interfaces:**
- Consumes: the B0 branch as it stands.
- Produces (used by Task 2): `PlacedBlock::detourOf` (int, −1), `BlockCfg::detourChancePct` (int, 33), `PDv2Config::detourChancePct`, conf key `ProceduralDungeon.V2.DetourChance`; a harness without any shortcut expectation; `RunChainChecks(int seeds, bool& sawPocket)`; `Combo::detourPct`.

- [ ] **Step 1: Header**

In `src/generator/PDBlockPlan.h`:

```cpp
        int detourChancePct = 33;   // Round B (B0b): chance per boss segment that a loop room hangs off the run
```
replaces the `loopChancePct` line. In `PlacedBlock`, replace the three-field block and its comment with:

```cpp
        // Round B. Spine rooms carry their chain index; pocket rooms (dead
        // ends off a spine room) carry the chain index of the room they hang
        // off; loop rooms (B0b: beside a straight run, entered from the run
        // and left back into it) carry the chain index of the spine room
        // their run leads into. -1 means "not that kind of block". B1/B3/B4
        // read nothing else.
        int chainIndex = -1;
        int branchOf = -1;
        int detourOf = -1;
```

- [ ] **Step 2: Generator — the shortcut draw goes**

In `src/generator/PDBlockPlan.cpp`: delete `int shortcutTo = -1;` from `struct Pocket`; delete the `NextBossAfter` function entirely (its only caller was the shortcut block); in `PlacePockets` delete the whole `if (rng.Chance(cfg.loopChancePct)) { … }` block (from that line to its closing brace), leaving `out.push_back(pocket);` directly after `pocket.host = host;`. In the materialisation loop delete `b.shortcutTo = pocket.shortcutTo;`. Rewrite DRAW ORDER item 3 to:

```cpp
        //   3. once the spine is complete, the pockets, still inside the
        //      search: per pocket a host index, a candidate index and the
        //      axis coin (if both). Pockets that do not fit unwind the search,
        //      and the draws simply continue from wherever it lands
```
and the `Chance` bullet below it to name `V2.DetourChance` instead of `V2.LoopChance` (Task 2 adds the draw itself).

- [ ] **Step 3: Validator — no shortcut branch**

In `ValidateBlockPlan`: in the header comment of the chain block drop "and a shortcut lands forward on a non-boss room of the same segment"; delete the `if (b.shortcutTo >= 0) { … }` block inside the pocket loop; in rule 3 remove `targetHits` and its `else if (b.shortcutTo >= 0 …)` branch, so the rule reads:

```cpp
            if (hostHits != 1 || others != 0)
            {
                return fail("a pocket's corridors do not match its declared host");
            }
```
and its comment says "exactly its declared host (once). Stub runs are ignored; anything else is a corridor the plan does not admit to."

- [ ] **Step 4: Engine + conf**

`src/PDv2Mgr.h`: `int detourChancePct = 33;  // Round B (B0b): chance per boss segment of a loop room (V2.DetourChance)` replaces the `loopChancePct` member; in the `PD_LAYOUT_VERSION` v3 paragraph replace "and LoopChance now means "a pocket carries a shortcut"" with "and `gen_loop_pct` carries V2.DetourChance (B0b: loop rooms, no shortcuts)".

`src/PDv2Mgr.cpp`:
```cpp
        // Round B (B0b): the chance, per boss segment, that a loop room hangs
        // off the spine's corridor run. Clamped into a percent; persisted in
        // the gen_loop_pct column, whose name predates the loop rooms.
        _config.detourChancePct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.DetourChance", 33)));
```
replaces the `loopChancePct` load; the boot `LOG_INFO` ends `pockets {} detour {}%` with `_config.detourChancePct`; `GeneratePlan`: `cfg.detourChancePct = _config.detourChancePct;`; `SavePlanToDB` passes `cfg.detourChancePct` where it passed `cfg.loopChancePct` (the column list is unchanged — add the comment `// gen_loop_pct carries V2.DetourChance since B0b` above the statement); `LoadPlanFromDB`: `cfg.detourChancePct = fields[8].Get<uint8>();`.

`src/PDv2Commands.cpp`: the info line ends `| pockets {} | detour {}%` with `cfg.detourChancePct`.

`conf/mod_procedural_dungeon.conf.dist`: replace the `V2.LoopChance` block with:

```
#
#    ProceduralDungeon.V2.DetourChance
#        Description: Round B: percent chance, per boss segment, that a loop
#                     room hangs off the main path - out of a straight corridor
#                     run to the side, around a corner into an extra room, out
#                     of its far side and back into the run two cells further.
#                     One per segment at most; the room is additional to the
#                     room count. Replaces V2.LoopChance (shortcuts, withdrawn).
#                     Stored per account as gen_loop_pct with the layout.
#        Default:     33
#

ProceduralDungeon.V2.DetourChance = 33
```
(v1's `ProceduralDungeon.LoopChance` block near line 131 is a different, still-live key: leave it.)

- [ ] **Step 5: Harness — strip the shortcut expectations**

- `RunChainMathChecks`: the defaults check becomes `fresh.chainIndex == -1 && fresh.branchOf == -1 && fresh.detourOf == -1` and `cfg.detourChancePct == 33` ("BlockCfg::detourChancePct must default to 33").
- `RunThemeParityChecks`: compare `detourOf` instead of `shortcutTo`.
- `ChainSummary`: the pockets loop prints only `"  R#%d + pocket (%d,%d)"`.
- `ChainPinString`: pockets as `"%d>%d,%d;"` (`branchOf, bx, by`).
- `RunChainChecks(int seeds, bool& sawPocket)`: `Combo::loopPct` → `detourPct` (rows `{ 8, 1, 2, 0 }` and `{ 8, 1, 2, 100 }` keep their values with comments `V2.DetourChance 0` / `100`); `cfg.detourChancePct = combo.detourPct;`; the corridor check reads `b.detourOf < 0` instead of `b.shortcutTo < 0`; delete `shortcutsHere`, the whole `if (b.shortcutTo >= 0) { … } else { … }` becomes just the dead-end body (flood from the pocket with the host removed must touch no spine room); delete the `if (combo.loopPct >= 100 && combo.branches > 0) { … }` block at the end.
- `RunBatch`: `bool sawShortcut` and its `Check` removed; call `RunChainChecks(count / 10 + 1, sawPocket);`; the comment above it loses its shortcut sentences; `shortcutsHere`/`shortcutLayouts` removed and the summary line becomes `"pockets per layout: %d..%d\n"`.
- Grep the harness for `shortcut` — zero hits except none.

- [ ] **Step 6: Build, capture the moved pins, gates**

Build (PowerShell), prove freshness, `--batch 500`. Expected failures: only the four pin messages (`the bossRooms=1 layout MOVED`, `the decor plan moved`, `the critter plan moved`, `the chain moved`) — the pocket shortcut draw is gone from the stream. Any other failure is a bug. Re-pin all four from the messages (freeze: `PINNED_BYTES`/`PINNED_TRAILER`, and set `PREVIOUS_BYTES = 363`, `PREVIOUS_TRAILER = "E;a5019024"`; decor/critter strings; `PD_CHAIN_PIN`), rebuild, run `--batch 500`, `--decor-batch 3000`, `--roomcap 3000`: all green; record the cap.

- [ ] **Step 7: Worldserver compiles, code style, commit**

`cmake --build …` (PowerShell) → exit 0. Code style. Then:

```bash
git add src/generator/PDBlockPlan.h src/generator/PDBlockPlan.cpp src/PDv2Mgr.h src/PDv2Mgr.cpp src/PDv2Commands.cpp conf/mod_procedural_dungeon.conf.dist tests/blockplan_harness.cpp
git -c core.autocrlf=false commit -m "refactor(generator): withdraw shortcuts, V2.DetourChance replaces V2.LoopChance"
```

---

### Task 2: Loop rooms — generator, validator, harness

**Files:**
- Modify: `src/generator/PDBlockPlan.cpp` (`Field` ~:180, after `StepCandidates` ~:372, `ChainGoal`/`ExtendChain` ~:470-530, `GenerateBlockPlan` draw order + start ~:1139-1200, materialisation ~:1330-1372, `AsciiBlockDump` ~:1484, validator ~:686-1060)
- Test: `tests/blockplan_harness.cpp` (`ChainSummary`, `RunChainChecks`, `RoomAtEndOf` ~:1541, `ChainPinString`, `RunBatch` summary)

**Interfaces:**
- Consumes: Task 1's fields and `detourChancePct`.
- Produces: loop rooms in every generated plan where a segment's chance hit and the geometry fit; `ValidateBlockPlan` refusing every malformed loop; `ChainSummary`'s `loops:` line; the `PD_CHAIN_PIN` format `chain|pockets|loops`.

- [ ] **Step 1: The failing harness checks**

In `RunChainChecks`, after the pocket loop and before the junction check, add the loop-room re-derivation (own code, no validator call):

```cpp
                // Loop rooms (B0b): re-derived from the sockets. R has exactly
                // two opposite sockets; the corners, the two attachment cells
                // and the straight middle cell have exactly the masks the
                // geometry demands; the run's two ends are chain rooms
                // detourOf-1 and detourOf (either orientation).
                std::set<size_t> attachments;
                std::vector<bool> loopInSegment(static_cast<size_t>(N + 1), false);
                int loops = 0;
                for (size_t at = 0; at < plan.blocks.size(); ++at)
                {
                    PlacedBlock const& b = plan.blocks[at];
                    if (b.detourOf < 0) continue;
                    ++loops;
                    if (b.detourOf > 0 && b.detourOf < wantChain) sawDetourHere = true;
                    Check(b.role == BlockRole::Room && b.chainIndex < 0 && b.branchOf < 0,
                          "a loop room carries the wrong role or fields", seed);
                    bool const intoOk = b.detourOf >= 1 && b.detourOf < wantChain;
                    Check(intoOk, "a loop room's run leads into no chain room", seed);
                    if (!intoOk) continue;
                    int const seg = SegmentOf(plan, plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.detourOf)])]);
                    Check(seg >= 1 && seg <= N && SegmentOf(plan, b) == seg, "a loop room is not in its run's segment", seed);
                    if (seg >= 1 && seg <= N)
                    {
                        Check(!loopInSegment[static_cast<size_t>(seg)], "two loop rooms in one segment", seed);
                        loopInSegment[static_cast<size_t>(seg)] = true;
                    }
                    unsigned const m = b.socketMask;
                    bool const opposite = (m == (SOCKET_N | SOCKET_S)) || (m == (SOCKET_E | SOCKET_W));
                    Check(opposite, "a loop room does not have exactly two opposite sockets", seed);
                    if (!opposite) continue;
                    int dx = 0, dy = 0;
                    if (m == (SOCKET_N | SOCKET_S)) { dx = 0; dy = 1; } else { dx = 1; dy = 0; }
                    // The strip: S1 = R - d, S2 = R + d, both corners sharing a side t.
                    PlacedBlock const* s1 = plan.At(b.bx - dx, b.by - dy);
                    PlacedBlock const* s2 = plan.At(b.bx + dx, b.by + dy);
                    unsigned const dBit = (dy > 0) ? SOCKET_S : SOCKET_E;
                    unsigned const dOpp = (dy > 0) ? SOCKET_N : SOCKET_W;
                    bool cornersOk = s1 && s2 && s1->roomId < 0 && s2->roomId < 0 &&
                                     (s1->socketMask & dBit) && (s2->socketMask & dOpp);
                    unsigned const t1 = s1 ? (s1->socketMask & ~dBit) : 0u;
                    unsigned const t2 = s2 ? (s2->socketMask & ~dOpp) : 0u;
                    cornersOk = cornersOk && t1 == t2 && (t1 == SOCKET_N || t1 == SOCKET_E || t1 == SOCKET_S || t1 == SOCKET_W)
                                && t1 != dBit && t1 != dOpp;
                    Check(cornersOk, "a loop room's corners are not two matching corner corridors", seed);
                    if (!cornersOk) continue;
                    int tx = 0, ty = 0;
                    if (t1 == SOCKET_N) ty = -1; else if (t1 == SOCKET_S) ty = 1; else if (t1 == SOCKET_W) tx = -1; else tx = 1;
                    unsigned const tOpp = (t1 == SOCKET_N) ? SOCKET_S : (t1 == SOCKET_S) ? SOCKET_N : (t1 == SOCKET_W) ? SOCKET_E : SOCKET_W;
                    PlacedBlock const* m1 = plan.At(s1->bx + tx, s1->by + ty);
                    PlacedBlock const* m2 = plan.At(s2->bx + tx, s2->by + ty);
                    PlacedBlock const* mid = plan.At(b.bx + tx, b.by + ty);
                    unsigned const attachMask = dBit | dOpp | tOpp;
                    unsigned const StubsOff = [&](PlacedBlock const* c) -> unsigned
                    {
                        // The mask without sockets that lead to chest stubs.
                        unsigned out = 0;
                        for (unsigned bit = 1; c && bit <= SOCKET_W; bit <<= 1)
                        {
                            if (!(c->socketMask & bit)) continue;
                            int ex = 0, ey = 0;
                            if (bit == SOCKET_N) ey = -1; else if (bit == SOCKET_S) ey = 1; else if (bit == SOCKET_W) ex = -1; else ex = 1;
                            PlacedBlock const* n = plan.At(c->bx + ex, c->by + ey);
                            if (n && n->role == BlockRole::CorridorDeadEnd) continue;
                            out |= bit;
                        }
                        return out;
                    };
                    bool const runOk = m1 && m2 && mid && m1->roomId < 0 && m2->roomId < 0 && mid->roomId < 0 &&
                                       StubsOff(m1) == attachMask && StubsOff(m2) == attachMask &&
                                       StubsOff(mid) == (dBit | dOpp);
                    Check(runOk, "a loop room's run is not the straight three-cell run with two attachments", seed);
                    if (!runOk) continue;
                    for (size_t k = 0; k < plan.blocks.size(); ++k)
                    {
                        if (&plan.blocks[k] == m1 || &plan.blocks[k] == m2) attachments.insert(k);
                    }
                    PlacedBlock const* e1 = plan.At(m1->bx - dx, m1->by - dy);
                    PlacedBlock const* e2 = plan.At(m2->bx + dx, m2->by + dy);
                    PlacedBlock const* into = &plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.detourOf)])];
                    PlacedBlock const* before = &plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.detourOf - 1)])];
                    bool const endsOk = (e1 == before && e2 == into) || (e1 == into && e2 == before);
                    Check(endsOk, "a loop room's run does not join chain rooms detourOf-1 and detourOf", seed);
                }
                Check(loops <= N, "more loop rooms than segments", seed);
                Check(rooms == total + loops, "room count is not rooms + bossRooms + loop rooms", seed);
```

`StubsOff` is a lambda assigned to a `std::function`-free local — write it as `auto const StubsOff = [&](PlacedBlock const* c) -> unsigned { … };` (the code above shows the body; use that form). The room count check earlier in the function (`Check(rooms == total, …)`) is replaced by the last line above, and the `strays` accounting treats `detourOf >= 0` rooms as legitimate (not strays).

Add `bool sawDetourHere = false;` per combo at the top of the seed loop's enclosing combo loop, and after the seed loop:

```cpp
            if (combo.detourPct >= 100)
            {
                std::snprintf(msg, sizeof(msg), "no loop room at all over %d seeds with DetourChance 100 (%d rooms + %d boss)",
                              seeds, combo.rooms, combo.bossRooms);
                Check(sawDetourHere, msg, 0);
                sawDetour = true;
            }
            if (combo.detourPct <= 0)
            {
                Check(!sawDetourHere, "a loop room appeared with DetourChance 0", 0);
            }
```
with `RunChainChecks(int seeds, bool& sawPocket, bool& sawDetour)` and `RunBatch` checking `sawDetour` ("no seed in the sample produced a loop room - the detour draw is dead code").

Junction rule in the harness: `Check(through == (attachments.count(k) ? 3 : 2), "a corridor block is a junction (not a loop attachment)", seed)`; the harness `RoomAtEndOf` gains the attachment rule (below), taking the set as a parameter.

`ChainSummary`: collect `loops` (blocks with `detourOf >= 0`) and after the pockets line print `loops:` with `"  R#%d run + loop room (%d,%d) [segment %d]"` per loop (`detourOf`, `bx`, `by`, `SegmentOf(plan, *loop)`), and count them in the segment lines (`pockets %d, loops %d`). `ChainPinString`: after the pockets add `'|'` and `"%d>%d,%d;"` (`detourOf, bx, by`) per loop room. `RunBatch` summary: count loop rooms and segments (`N = max(1, cfg.bossRooms)` per layout) and print `"loop rooms: %d of %d segments carry one (%d layouts)\n"`.

Build; expected: compile errors only until Task 2's generator exists? No — the checks compile against Task 1's fields; run `--batch 500`: expected `no loop room at all … DetourChance 100` and `the detour draw is dead code` failures, nothing else.

- [ ] **Step 2: Generator — detour candidates, commit, search**

In `src/generator/PDBlockPlan.cpp`, extend `Field`:

```cpp
            std::vector<Cell> rooms;
            std::vector<Cell> chain;
            std::vector<std::pair<Cell, int>> loops;   // B0b: loop room cell, chain index its run leads into
```

After `StepCandidates`, add:

```cpp
        // --- B0b loop rooms --------------------------------------------------

        int const DETOUR_STEP = 4;          // Manhattan: a straight run of three corridor blocks

        struct DetourCandidate
        {
            Cell dest;                      // the spine room the run leads into (P + 4d)
            int dx = 0, dy = 0;             // run direction d
            int sx = 0, sy = 0;             // strip side s (perpendicular to d)
        };

        bool GapOk(Field const& f, Cell const& c)
        {
            for (Cell const& r : f.rooms)
            {
                if (Manhattan(r, c) < MIN_ROOM_GAP)
                {
                    return false;
                }
            }
            return true;
        }

        // Detour steps seen from `from`: a straight run of three corridor
        // cells into the next spine room with the loop strip (corner, loop
        // room, corner) free beside it. Destinations in the field's (y, x)
        // order and, per destination, the left side before the right, so a
        // draw index means the same on every compiler. The direction bias
        // applies to the run direction like any step.
        std::vector<DetourCandidate> DetourCandidates(Field const& f, Cell const& from, Cell const* prev)
        {
            std::vector<DetourCandidate> forward;
            std::vector<DetourCandidate> backward;
            for (int y = 0; y < f.size; ++y)
            {
                for (int x = 0; x < f.size; ++x)
                {
                    Cell dest;
                    dest.x = x;
                    dest.y = y;
                    int const ddx = dest.x - from.x;
                    int const ddy = dest.y - from.y;
                    bool const straight = (ddx == 0 && std::abs(ddy) == DETOUR_STEP) ||
                                          (ddy == 0 && std::abs(ddx) == DETOUR_STEP);
                    if (!straight || !f.Free(dest) || !GapOk(f, dest))
                    {
                        continue;
                    }
                    int const dx = ddx / DETOUR_STEP;
                    int const dy = ddy / DETOUR_STEP;
                    bool runFree = true;
                    for (int k = 1; k <= 3 && runFree; ++k)
                    {
                        Cell c;
                        c.x = from.x + k * dx;
                        c.y = from.y + k * dy;
                        runFree = f.Free(c);
                    }
                    if (!runFree)
                    {
                        continue;
                    }
                    // Left, then right of the run direction (kit frame: bx
                    // east, by south) - only the fixed order matters.
                    int const sides[2][2] = { { -dy, dx }, { dy, -dx } };
                    for (int side = 0; side < 2; ++side)
                    {
                        int const sx = sides[side][0];
                        int const sy = sides[side][1];
                        bool stripFree = true;
                        Cell room;
                        for (int k = 1; k <= 3 && stripFree; ++k)
                        {
                            Cell c;
                            c.x = from.x + k * dx + sx;
                            c.y = from.y + k * dy + sy;
                            stripFree = f.Free(c);
                            if (k == 2)
                            {
                                room = c;
                            }
                        }
                        if (!stripFree || !GapOk(f, room))
                        {
                            continue;
                        }
                        DetourCandidate cand;
                        cand.dest = dest;
                        cand.dx = dx;
                        cand.dy = dy;
                        cand.sx = sx;
                        cand.sy = sy;
                        bool reversal = false;
                        if (prev)
                        {
                            int const px = from.x - prev->x;
                            int const py = from.y - prev->y;
                            reversal = (px * dx + py * dy) < 0;
                        }
                        (reversal ? backward : forward).push_back(cand);
                    }
                }
            }
            return forward.empty() ? backward : forward;
        }

        // Lays a detour step: the straight run with its two attachments, the
        // strip (corner, loop room, corner) and the destination spine room.
        void CommitDetour(Field& f, Cell const& from, DetourCandidate const& c, Cell& loopRoom)
        {
            unsigned const dBit = BitForStep(c.dx, c.dy);
            unsigned const sBit = BitForStep(c.sx, c.sy);
            Cell run[5];
            for (int k = 0; k <= 4; ++k)
            {
                run[k].x = from.x + k * c.dx;
                run[k].y = from.y + k * c.dy;
            }
            Cell strip[3];
            for (int k = 1; k <= 3; ++k)
            {
                strip[k - 1].x = run[k].x + c.sx;
                strip[k - 1].y = run[k].y + c.sy;
            }
            for (int k = 0; k < 4; ++k)
            {
                f.masks[f.Index(run[k])] |= dBit;
                f.masks[f.Index(run[k + 1])] |= OppositeBit(dBit);
            }
            for (int k = 1; k <= 3; ++k)
            {
                f.occ[f.Index(run[k])] = 2;
            }
            f.masks[f.Index(run[1])] |= sBit;
            f.masks[f.Index(strip[0])] |= OppositeBit(sBit);
            f.masks[f.Index(run[3])] |= sBit;
            f.masks[f.Index(strip[2])] |= OppositeBit(sBit);
            f.masks[f.Index(strip[0])] |= dBit;
            f.masks[f.Index(strip[1])] |= OppositeBit(dBit);
            f.masks[f.Index(strip[1])] |= dBit;
            f.masks[f.Index(strip[2])] |= OppositeBit(dBit);
            f.occ[f.Index(strip[0])] = 2;
            f.occ[f.Index(strip[2])] = 2;
            f.occ[f.Index(strip[1])] = 1;
            f.rooms.push_back(strip[1]);
            loopRoom = strip[1];
            f.occ[f.Index(run[4])] = 1;
            f.rooms.push_back(run[4]);
            f.chain.push_back(run[4]);
        }

        // The boss segment a chain index belongs to (1..N); the entrance is 0.
        int SegmentIndexOf(std::vector<int> const& bosses, int chainIndex)
        {
            if (chainIndex <= 0)
            {
                return 0;
            }
            for (size_t k = 0; k < bosses.size(); ++k)
            {
                if (chainIndex <= bosses[k])
                {
                    return static_cast<int>(k) + 1;
                }
            }
            return static_cast<int>(bosses.size());
        }

        bool HasLoopInSegment(Field const& f, std::vector<int> const& bosses, int segment)
        {
            for (auto const& loop : f.loops)
            {
                if (SegmentIndexOf(bosses, loop.second) == segment)
                {
                    return true;
                }
            }
            return false;
        }
```

`ChainGoal` gains `std::vector<bool> const* wantDetour = nullptr;  // per segment, index 1..N`. In `ExtendChain`, after `prev` is computed and before the ordinary candidates:

```cpp
            // B0b: a segment that wants a loop room and has none yet tries the
            // detour steps first; when none fits (or every one fails deeper
            // in the search) the ordinary steps follow, and the loop room may
            // still land on a later step of the same segment.
            int const nextIndex = static_cast<int>(f.chain.size());
            int const segment = SegmentIndexOf(*goal.bosses, nextIndex);
            if (segment >= 1 && (*goal.wantDetour)[static_cast<size_t>(segment)] &&
                !HasLoopInSegment(f, *goal.bosses, segment))
            {
                std::vector<DetourCandidate> dcands = DetourCandidates(f, from, prev);
                while (!dcands.empty())
                {
                    if (budget <= 0)
                    {
                        return false;
                    }
                    --budget;
                    size_t const pick = static_cast<size_t>(
                        rng.UniformInt(0, static_cast<int>(dcands.size()) - 1));
                    DetourCandidate const cand = dcands[pick];

                    Field next = f;
                    Cell loopRoom;
                    CommitDetour(next, from, cand, loopRoom);
                    next.loops.push_back(std::make_pair(loopRoom, nextIndex));
                    if (ExtendChain(rng, goal, next, pocketsOut, budget))
                    {
                        f = next;
                        return true;
                    }
                    dcands.erase(dcands.begin() + static_cast<std::ptrdiff_t>(pick));
                }
            }
```

In `GenerateBlockPlan`: after the start cell draws, add the segment draws and wire the goal:

```cpp
            // B0b: one Chance per boss segment, in order, before any step -
            // the segments are arithmetic, so the draws are too.
            std::vector<bool> wantDetour(static_cast<size_t>(bosses) + 1, false);
            for (int k = 1; k <= bosses; ++k)
            {
                wantDetour[static_cast<size_t>(k)] = rng.Chance(cfg.detourChancePct);
            }
            goal.wantDetour = &wantDetour;
```
(`goal` is built before the attempt loop today — move its construction inside the loop, or set `goal.wantDetour` per attempt; the vector must outlive the search.) DRAW ORDER: insert item `2. per boss segment k = 1..N: Chance(detourChancePct) (nothing at 0/100)` and renumber; item 3 (chain steps) gains "a detour-candidate index when the segment wants a loop room and has none (one candidate costs no draw), otherwise / afterwards the ordinary candidate index and the axis coin".

Materialisation: build `std::map<Cell, int> loopOf` from `field.loops` (room ids after the pockets: `chainLen + pockets.size() + p`), and in the room branch:

```cpp
                    auto lit = loopOf.find(c);
                    if (cit != chainOf.end()) { … as today … }
                    else if (lit != loopOf.end())
                    {
                        b.role = BlockRole::Room;
                        b.detourOf = lit->second;
                    }
                    else { … the pocket branch … }
```
`AsciiBlockDump`: `case BlockRole::Room: out += (b->detourOf >= 0) ? 'o' : (b->branchOf >= 0) ? 'r' : 'R'; break;` and the `PrintOne` legend gains `o = loop room`.

- [ ] **Step 3: Validator**

In `ValidateBlockPlan`, after the pocket checks and BEFORE the corridor-run walk lambda, add the loop-room reconstruction (same geometry as the harness code in Step 1, written against `plan.blocks` and `index`; `fail(...)` on every `Check`; collect `std::set<size_t> attachment`; enforce one loop room per segment via `SegmentOf` and `loops <= bosses`). Then:

- the walk lambda: after collecting `outs`/`next`, replace `if (outs != 1) { junction = true; return -1; }` with

```cpp
                if (outs != 1)
                {
                    // The one sanctioned fork: a loop attachment entered along
                    // the run continues straight through it.
                    if (attachment.count(at) && (b.socketMask & entryBit) && outs == 2)
                    {
                        next = entryBit;
                    }
                    else
                    {
                        junction = true;
                        return -1;
                    }
                }
```
- rule 1: `if (through != (attachment.count(i) ? 3 : 2)) return fail("a corridor block is a junction");` (iterate with an index so `i` is available).
- rule 4 (chain length) unchanged; add rule 5: `rooms counted == max(2, rooms + bossRooms) + loop rooms` → `fail("room count does not match the budget plus the loop rooms")`.

- [ ] **Step 4: Build, pins, gates**

Build (PowerShell), prove freshness, `--batch 500`. Expected failures: exactly the four pin messages (the segment draws moved every stream). Re-pin from the messages (freeze `PREVIOUS_*` = Task 1's values), rebuild; `--batch 500` ALL CHECKS PASS with the sweep table and the new `loop rooms:` line; `--decor-batch 3000` 0 failures; `--roomcap 3000` — record the table and the cap; if the cap drops below 15, set `PD_GAME_ROOMS_CAP_MEASURED` and refresh its comment table (Round B B0b, a detour costs three extra cells). Run `./pdblock.exe --path 12345 5`, `--path 777 8`, `--path 4242 15` and paste the outputs; at least one must show a `loops:` line (if none does, try seeds until one does and report how many seeds it took — the yield line in the batch says what to expect).

- [ ] **Step 5: Code style, commit**

```bash
git add src/generator/PDBlockPlan.cpp src/generator/PDv2GameMath.h tests/blockplan_harness.cpp
git -c core.autocrlf=false commit -m "feat(generator): loop rooms beside a straight run, 33% per boss segment"
```
(omit `PDv2GameMath.h` if the cap did not move.)

---

### Task 3: Docs, worldserver, operator doc, vault

**Files:**
- Modify: `CLAUDE.md` (planner row), `README.md` (PDv2 note), `docs/superpowers/specs/2026-09-02-pdv2-b0-spine-generator-design.md` §11 (one paragraph "B0b")
- Modify: `C:\wowstuff\dcore\configs\modules\mod_procedural_dungeon.conf` (`V2.LoopChance = 15` → `V2.DetourChance = 33` with a one-line comment)
- Modify: `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde28.md`
- Modify (share-public `main`): `docs/World of Warcraft/12-server-todo.md`, `forgotten-land/15-host-migration-log.md`, `claude_log.md` (END)

- [ ] **Step 1: Module docs**

`CLAUDE.md` planner row: replace "an occasional segment-local shortcut (`V2.LoopChance`)" with "loop rooms beside a straight run (`V2.DetourChance`, 33 % per boss segment, additional to the room count)". `README.md` note: "a few one-room pockets and an occasional shortcut forward" → "a few one-room pockets and, per boss segment, a possible loop room beside the corridor". Spec §11 gains a dated paragraph: B0b — shortcuts withdrawn, loop rooms as built (geometry, draw order position, validator rules, pin format), `V2.DetourChance` replaces `V2.LoopChance`, `gen_loop_pct` carries it.

Commit: `docs: loop rooms replace shortcuts in CLAUDE.md, README and the B0 as-built notes`.

- [ ] **Step 2: Worldserver**

`cmake --build …` (PowerShell), `certutil -hashfile C:\wowstuff\dcore_bin\bin\RelWithDebInfo\worldserver.exe MD5`; the operator's server is running → stage only.

- [ ] **Step 3: Deployed conf + operator document**

In the deployed conf replace the `ProceduralDungeon.V2.LoopChance = 15` line (and its Round-B comment block if one precedes it) with:

```
# Round B (2026-09-03, B0b): loop rooms beside a straight corridor run, 33 % per
# boss segment (additional rooms). Replaces V2.LoopChance (shortcuts, withdrawn).
ProceduralDungeon.V2.DetourChance = 33
```
keep `ProceduralDungeon.V2.Branches = 2`; preserve line endings; verify with `grep -n 'V2.DetourChance\|V2.LoopChance\|V2.Branches'` (LoopChance must be gone).

`pd_testlauf_runde28.md`: the boot line becomes `pockets 2 detour 33%`, the info line `| pockets 2 | detour 33%`; the staged md5 → the new one; §3's *Erwartet* gains the loop room (a side room off a straight corridor, entered from the corridor and left back into it two cells on, a pack inside; at most one per boss segment, roughly one layout in three at the default); the "shortcut" sentences and the `[shortcut -> R#2]` sample output are replaced by fresh `pdblock --path` output that shows a `loops:` line; §5 "Offen": the shortcut-yield item is replaced by "loop-room yield: n of m segments in the batch (the geometry needs a straight three-cell run; the operator may raise `V2.DetourChance`)".

- [ ] **Step 4: Vault**

`12-server-todo.md`: delete the §4 row `**DECISION: PDv2 shortcut yield (Round B)**`; in the PDv2 row (§2) replace the shortcut wording with "loop rooms (B0b, operator 2026-09-03)". `15-host-migration-log.md`: MIG-017's Round B bullet gains the new commit hashes and "`V2.DetourChance = 33` replaces `V2.LoopChance` in the conf (host conf: rename the key)". `claude_log.md` END: `## 2026-09-03 — PDv2 Round B / B0b: loop rooms replace shortcuts (session 0cf92ad4)` — the operator's decision, what changed, gates, cap, staged md5, hashes. `python python_scripts/build_host_runbook.py --check` → 0 errors. One commit: `docs(pdv2): B0b loop rooms - shortcuts withdrawn, DetourChance, runde28 refreshed`. Do not push.

- [ ] **Step 5: Report**

Gate tails, cap, the `--path` outputs with a `loops:` line, staged md5, commits (module + share-public).

---

## Self-review

- Spec coverage: §0 decisions → Tasks 1–3; §1 geometry → Task 2 Step 2 (`DetourCandidates`/`CommitDetour`) and Step 3 (validator reconstruction); §2 draw order → Task 2 Step 2 (segment draws first, detour candidates before ordinary ones, no axis coin for detours) + Task 1 Step 2 (shortcut draw removed); §3 data model/config → Task 1; §4 validator + harness → Task 2 Steps 1 and 3; §5 follow-ups → Task 3 (docs, vault, operator doc; the chest rides with B1).
- Types: `detourOf`, `detourChancePct`, `DetourCandidate{dest,dx,dy,sx,sy}`, `CommitDetour(Field&, Cell const&, DetourCandidate const&, Cell&)`, `Field::loops` as `std::vector<std::pair<Cell,int>>`, `ChainGoal::wantDetour` as `std::vector<bool> const*`, `RunChainChecks(int, bool&, bool&)` are used consistently.
- Placeholders: pins are captured by running (documented procedure); the md5 and hashes are filled at execution.
