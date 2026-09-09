# PDv2 Round B / B0 — spine generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the scatter + MST labyrinth in `PDBlockPlan` with one chain of rooms through the boss rooms, a few one-room pockets hanging off it, an occasional segment-local shortcut, and make the chain visible and pinned in the harness and persisted in the engine.

**Architecture:** All layout logic stays in the engine-free `src/generator/PDBlockPlan.{h,cpp}` and is proven by `tests/blockplan_harness.cpp` (`pdblock.exe`) before a worldserver is built. The engine glue (`PDv2Mgr`, `PDv2Commands`, one new characters-DB SQL file, the conf) only carries one new generation input, `branches`, through config → planner → persistence. The manifest **format** does not change; only its content does, so every layout-derived pin is re-captured and `PD_LAYOUT_VERSION` moves to 3.

**Tech Stack:** C++17 (MSVC 19.44 `cl.exe`; **no g++ on this box**), AzerothCore 3.3.5a module, MySQL 8.4 characters DB `acore_characters`, Python 3 pipeline scripts (oracle only, unchanged), `FLStream.dll` (**not touched**).

**Spec:** `docs/superpowers/specs/2026-09-02-pdv2-b0-spine-generator-design.md` (this addendum) on top of
`docs/superpowers/specs/2026-09-01-pdv2-content-expansion-design.md` §3 B0 and §1 "Ground truth". Read both before the first line of code.

---

## Global Constraints

Every task's requirements implicitly include all of these.

- **The live checkout is `C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\modules\mod-procedural-dungeon`.** `C:\Users\Anwender\Documents\GitHub\mod-procedural-dungeon` is a stale v1 clone with no PDv2 sources; editing it loses the work silently.
- **Branch:** `claude/pdv2-round-b-0cf92ad4` (already created; spec committed at `339a858`). Never commit to `main`.
- **Conventional Commits. English in code, comments, docs and commit messages.** Commit at the end of every task. Commit with `git -c core.autocrlf=false commit` so the LF/CRLF warning does not rewrite bytes.
- **Determinism contract for `src/generator/`:** no `std::random` distribution, no `unordered_*` iteration, no `std::shuffle`, fixed iteration orders. `PDRandom::UniformInt(lo, hi)` returns `lo` **without drawing** when `lo >= hi` (`src/generator/PDRandom.h:43-46`); `PDRandom::Chance(pct)` short-circuits at `<= 0` and `>= 100` without drawing. Never reason about stream position without this.
- **Every new draw is documented** in the consumption-order comment inside `GenerateBlockPlan`, in the same commit.
- **Pins are captured by running the harness and reading its failure message, never by reasoning about the value.** A pin that moves after a deliberate generator change is the change being noticed; update it in the same commit.
- **Harness gates before any merge:** `pdblock.exe --batch 500`, `pdblock.exe --decor-batch 3000`, `pdblock.exe --roomcap 3000`, all `ALL CHECKS PASS` / 0 failures, plus one real manifest through the Python oracle. The binary must be proven fresh (see below).
- **The manifest format is a three-implementation contract** (`EmitManifest`, `scripts/49_pd_compose_blocks.py`, the DLL parser). Do not change `EmitManifest`.
- **SQL is shipped as NEW files, never as edits to existing ones.** The updater applies each file once by hash. Column adds use the guarded-ALTER shape of `data/sql/db-characters/mod_pdungeon_account_difficulty.sql`.
- **Startup-only.** Any SQL or conf change needs a **worldserver restart**; `.reload config` does nothing for these keys.
- **No task restarts the worldserver, stops it, or writes to a database.** `worldserver.exe` (PID varies) is the operator's running process. Windows refuses to overwrite a running executable, so a freshly built binary is **staged** in `C:\wowstuff\dcore_bin\bin\RelWithDebInfo\` and the operator document opens with stop → copy → start (Round A, Task 18 report).
- **Adding a NEW `.cpp` or `.h` to `src/` requires a `cmake` re-configure.** B0 adds none, so a plain `cmake --build` suffices.
- **Git Bash mangles the build's `/m` flag into a path** and **a `cmd /c` build silently no-ops under the Bash tool**. Run `cl.exe` and `cmake --build` from **PowerShell**.
- **Do not add `/WX`.** `tests/blockplan_harness.cpp` emits one C4456 today (shadowed `colon`, line ~263); that warning is the expected baseline, any *new* warning is a finding.
- **Code style:** run `python "C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\apps\codestyle\codestyle-cpp.py"` from the module root (it scans `<cwd>/src`) before every commit that touches `src/`.
- **Round B deliberately re-rolls every stored layout** (`PD_LAYOUT_VERSION` 2 → 3). Accepted in the spec: the server is not public and character progress is expendable. Do not add compatibility shims.

### Building and running the harness (used by almost every task)

From **PowerShell**, in the module root:

```powershell
cd C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\modules\mod-procedural-dungeon
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp src\generator\PDv2PackDraw.cpp'
```

A line `Der Befehl "vswhere.exe" ... konnte nicht gefunden werden.` is printed first and is harmless.
Expected: the six source filenames echoed, `Generating Code...`, exit 0, and exactly one warning
(`blockplan_harness.cpp: warning C4456`).

**Prove the binary is newer than the sources before believing any gate** (Git Bash is fine for this):

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && stat -c '%y %n' pdblock.exe src/generator/*.cpp src/generator/*.h tests/blockplan_harness.cpp | sort | tail -3
```

`pdblock.exe` must be the last line. A gate run against a stale binary is not evidence.

Run the gates (PowerShell or Git Bash, module root):

```bash
./pdblock.exe --batch 500
./pdblock.exe --decor-batch 3000
./pdblock.exe --roomcap 3000
```

**A batch that prints no `214 walk mask(s)` line is a weaker gate wearing a passing result** — the
walk-grid checks are skipped rather than faked when the kit metadata is missing.

Measured-green baseline on `main` `5776b17` (2026-09-02, before this plan): `--batch 500` →
`10093 checks, 0 failure(s)`; `--decor-batch 3000` → `168694 checks, 0 failure(s)`; `--roomcap 3000` →
cap 15 both themes, `15 2 17 0 1470 ok`.

---

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `data/sql/db-characters/mod_pdungeon_account_branches.sql` | Adds `pdungeon_account.gen_branches` (guarded ALTER). |
| `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde28.md` | Operator checkpoint document for the B0 look (Task 6). |

**Modified:**

| Path | Change |
|---|---|
| `src/generator/PDBlockPlan.h` | `BlockCfg::branches`; `PlacedBlock::chainIndex/branchOf/shortcutTo`; `PocketCountFor`, `BossChainIndex`, `ChainLength`, `SegmentOf`. |
| `src/generator/PDBlockPlan.cpp` | `ChainRooms` machinery replaces `ScatterRooms` + `SelectCorridorEdges`; pockets + shortcuts; roles from chain positions; validator additions incl. the boss cut property; `AsciiBlockDump` pocket glyph. |
| `tests/blockplan_harness.cpp` | Chain-math checks, chain structure checks (replace `RunBossRoomChecks`), re-pinned freeze/decor/critter pins, `PD_CHAIN_PIN`, chain view in `PrintOne`/`PrintPath`, engine-field sweep, batch summary. |
| `src/generator/PDv2GameMath.h` | Re-measured room-cap comment table (and the constant, if it drops). |
| `src/PDv2Mgr.h` / `.cpp` | `PDv2Config::branches`, config key, `cfg.branches`, persistence, `PD_LAYOUT_VERSION` 3. |
| `src/PDv2Commands.cpp` | `pockets` / `shortcut` on the `.pdungeon v2 info` config line. |
| `conf/mod_procedural_dungeon.conf.dist` | `V2.Branches` block; `V2.LoopChance` description rewritten. |
| `CLAUDE.md`, `README.md` | Planner row and a PDv2 pipeline note. |
| Vault (share-public, `main`): `docs/World of Warcraft/12-server-todo.md`, `procedural-dungeon/02-pdv2-session-resume.md`, `forgotten-land/15-host-migration-log.md`, `claude_log.md` (END) | Standing duties (Task 6). |

---

### Task 1: Data model and the chain arithmetic

**Files:**
- Modify: `src/generator/PDBlockPlan.h` (struct `BlockCfg` ~line 76, struct `PlacedBlock` ~line 88, after `AltCountFor` ~line 74)
- Modify: `src/generator/PDBlockPlan.cpp` (after `AltCountFor`, ~line 291)
- Test: `tests/blockplan_harness.cpp` (new `RunChainMathChecks`, called from `RunBatch` right after `RunGameMathChecks();` ~line 2756)

**Interfaces:**
- Consumes: nothing new.
- Produces (used by Tasks 2–5):
  - `BlockCfg::branches` (int, default 2)
  - `PlacedBlock::chainIndex`, `PlacedBlock::branchOf`, `PlacedBlock::shortcutTo` (all int, default −1)
  - `int PocketCountFor(int rooms, int bossRooms, int branches)`
  - `int BossChainIndex(int chainLen, int bossRooms, int k)` — k = 1..N, returns the chain index
  - `int ChainLength(BlockPlan const&)` — 0 for a plan without chain fields
  - `int SegmentOf(BlockPlan const&, PlacedBlock const&)` — 1..N for spine rooms after the entrance and for pockets (via their host), 0 for the entrance, −1 for corridors

- [ ] **Step 1: Write the failing harness check**

Add `#include <algorithm>` to the harness includes (it uses `std::max`/`std::min` from here on). Then add to `tests/blockplan_harness.cpp` directly before `void RunGameMathChecks()` (~line 758):

```cpp
    // --- Round B chain arithmetic (spec 2026-09-02 §2) ----------------------
    //
    // Pure functions, so they are checked against a table rather than against
    // themselves: pockets = min(branches, total / 3, (total - 1 - N) / 2),
    // chainLen = total - pockets, boss k at round(k * (L - 1) / N).
    void RunChainMathChecks()
    {
        struct Row { int rooms; int boss; int branches; int pockets; int chainLen; int b1; int b2; };
        Row const rows[] = {
            {  1, 1, 2, 0,  2, 1, -1 },
            {  2, 1, 2, 0,  3, 2, -1 },
            {  3, 1, 2, 1,  3, 2, -1 },
            {  5, 1, 2, 2,  4, 3, -1 },
            {  8, 1, 2, 2,  7, 6, -1 },
            { 12, 2, 2, 2, 12, 6, 11 },
            { 15, 2, 2, 2, 15, 7, 14 },
            {  5, 1, 0, 0,  6, 5, -1 },
            {  1, 2, 2, 0,  3, 1,  2 },
            {  5, 1, 9, 2,  4, 3, -1 },
            { 15, 2, 9, 5, 12, 6, 11 },
            {  5, 0, 2, 2,  3, 2, -1 },     // bossRooms 0 still means one boss
        };
        for (Row const& r : rows)
        {
            char msg[160];
            int const pockets = PocketCountFor(r.rooms, r.boss, r.branches);
            std::snprintf(msg, sizeof(msg), "PocketCountFor(%d,%d,%d) = %d, want %d",
                          r.rooms, r.boss, r.branches, pockets, r.pockets);
            Check(pockets == r.pockets, msg, 0);

            int const total = std::max(2, r.rooms + r.boss);
            int const chainLen = total - pockets;
            std::snprintf(msg, sizeof(msg), "chainLen for (%d,%d,%d) = %d, want %d",
                          r.rooms, r.boss, r.branches, chainLen, r.chainLen);
            Check(chainLen == r.chainLen, msg, 0);

            int const b1 = BossChainIndex(chainLen, r.boss, 1);
            std::snprintf(msg, sizeof(msg), "boss 1 for (%d,%d,%d) at %d, want %d",
                          r.rooms, r.boss, r.branches, b1, r.b1);
            Check(b1 == r.b1, msg, 0);
            if (r.b2 >= 0)
            {
                int const b2 = BossChainIndex(chainLen, r.boss, 2);
                std::snprintf(msg, sizeof(msg), "boss 2 for (%d,%d,%d) at %d, want %d",
                              r.rooms, r.boss, r.branches, b2, r.b2);
                Check(b2 == r.b2, msg, 0);
            }
        }

        // Every legal engine request seats N distinct bosses at index >= 1:
        // chainLen - 1 >= N is what the host clamp guarantees.
        for (int rooms = 1; rooms <= 15; ++rooms)
        {
            for (int boss = 1; boss <= 3; ++boss)
            {
                int const total = std::max(2, rooms + boss);
                int const chainLen = total - PocketCountFor(rooms, boss, 2);
                Check(chainLen - 1 >= boss, "the pocket clamp left no room for the bosses", 0);
                int last = 0;
                for (int k = 1; k <= boss; ++k)
                {
                    int const idx = BossChainIndex(chainLen, boss, k);
                    Check(idx > last && idx <= chainLen - 1, "boss positions not strictly increasing", 0);
                    last = idx;
                }
                Check(last == chainLen - 1, "the last boss is not the last chain room", 0);
            }
        }

        // The struct defaults the later tasks rely on.
        PlacedBlock const fresh;
        Check(fresh.chainIndex == -1 && fresh.branchOf == -1 && fresh.shortcutTo == -1,
              "PlacedBlock chain fields must default to -1", 0);
        BlockCfg const cfg;
        Check(cfg.branches == 2, "BlockCfg::branches must default to 2", 0);
    }
```

And in `RunBatch`, right after `RunGameMathChecks();`:

```cpp
        RunChainMathChecks();
```

- [ ] **Step 2: Build to verify it fails**

Run the build command from the Global Constraints (PowerShell).
Expected: compile errors naming `PocketCountFor`, `BossChainIndex`, `chainIndex`, `branches` as undeclared.

- [ ] **Step 3: Add the fields and declarations to the header**

In `src/generator/PDBlockPlan.h`, in `struct BlockCfg` replace the `loopChancePct` line and append `branches`:

```cpp
        int loopChancePct = 15;     // Round B: chance that a pocket carries a shortcut
        int originBX = 256;         // global block coord of the field origin
        int originBY = 256;
        int theme = 1;
        int maxTries = 12;          // seed+n retries before giving up
        int maxDeadEnds = 2;        // stub corridors attached after the loops
        int branches = 2;           // Round B: pocket rooms hanging off the spine (V2.Branches)
```

In `struct PlacedBlock` append after `int alt = 0;`:

```cpp
        // Round B (spec 2026-09-02 §5). Spine rooms carry their chain index;
        // pocket rooms carry the chain index of the room they hang off and,
        // if they have one, the chain index their shortcut lands on. -1 means
        // "not that kind of block". B1/B3/B4 read nothing else.
        int chainIndex = -1;
        int branchOf = -1;
        int shortcutTo = -1;
```

After `int AltCountFor(BlockRole role);` add:

```cpp
    // Round B chain arithmetic (spec 2026-09-02 §2), pure and draw-free so the
    // engine, the planner and the harness agree by construction.
    //   total    = max(2, rooms + bossRooms)
    //   pockets  = min(branches, total / 3, (total - 1 - N) / 2)   N = max(1, bossRooms)
    //   chainLen = total - pockets
    //   boss k   = round(k * (chainLen - 1) / N), k = 1..N
    int PocketCountFor(int rooms, int bossRooms, int branches);
    int BossChainIndex(int chainLen, int bossRooms, int k);

    // Reads of a generated plan. ChainLength is 0 for a plan without chain
    // fields. SegmentOf: 0 for the entrance, k for a spine room in boss k's
    // segment (boss k included), a pocket's host segment, -1 for corridors.
    int ChainLength(BlockPlan const& plan);
    int SegmentOf(BlockPlan const& plan, PlacedBlock const& block);
```

- [ ] **Step 4: Implement the four functions**

In `src/generator/PDBlockPlan.cpp`, directly after the `AltCountFor` definition (before `uint32_t Crc32`):

```cpp
    int PocketCountFor(int rooms, int bossRooms, int branches)
    {
        int const total = std::max(2, rooms + bossRooms);
        int const bosses = bossRooms > 0 ? bossRooms : 1;
        int pockets = std::max(0, branches);
        pockets = std::min(pockets, total / 3);
        // Host clamp: one pocket per spine room that is neither the entrance
        // nor a boss, so pockets <= (total - pockets) - 1 - bosses.
        pockets = std::min(pockets, std::max(0, (total - 1 - bosses) / 2));
        return pockets;
    }

    int BossChainIndex(int chainLen, int bossRooms, int k)
    {
        int const bosses = bossRooms > 0 ? bossRooms : 1;
        // round(k * (L - 1) / N) in integers, half up.
        return (2 * k * (chainLen - 1) + bosses) / (2 * bosses);
    }

    int ChainLength(BlockPlan const& plan)
    {
        int len = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            len = std::max(len, b.chainIndex + 1);
        }
        return len;
    }

    int SegmentOf(BlockPlan const& plan, PlacedBlock const& block)
    {
        int const idx = block.chainIndex >= 0 ? block.chainIndex : block.branchOf;
        if (idx < 0)
        {
            return -1;
        }
        if (idx == 0)
        {
            return 0;
        }
        int const len = ChainLength(plan);
        int const bosses = plan.config.bossRooms > 0 ? plan.config.bossRooms : 1;
        for (int k = 1; k <= bosses; ++k)
        {
            if (idx <= BossChainIndex(len, plan.config.bossRooms, k))
            {
                return k;
            }
        }
        return bosses;
    }
```

- [ ] **Step 5: Build and run the batch**

Build (PowerShell), prove freshness, run `./pdblock.exe --batch 500`.
Expected: `ALL CHECKS PASS`; the check count rises above the 10093 baseline. The old generator is untouched, so the freeze pin still holds.

- [ ] **Step 6: Code style, commit**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && python "C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk/apps/codestyle/codestyle-cpp.py"
git add src/generator/PDBlockPlan.h src/generator/PDBlockPlan.cpp tests/blockplan_harness.cpp
git -c core.autocrlf=false commit -m "feat(generator): chain fields and the Round B pocket/boss arithmetic"
```

---

### Task 2: The chain generator

**Files:**
- Modify: `src/generator/PDBlockPlan.cpp` — replace `GraphEdge`/`DistSq`/`FindRoot`/`ScatterRooms`/`SelectCorridorEdges` (lines 163–271) with the chain helpers; replace the body of `GenerateBlockPlan` (lines 434–739); extend `ValidateBlockPlan`; one glyph in `AsciiBlockDump`.
- Modify: `src/PDv2Mgr.h:151` — `PD_LAYOUT_VERSION` 2 → 3 with its paragraph.
- Test: `tests/blockplan_harness.cpp` — `RunChainChecks` replaces `RunBossRoomChecks` (lines 1320–1380); `RunThemeParityChecks` compares the chain fields; re-pinned `RunLayoutFreezeCheck`, `PD_DECOR_PLAN_PIN`, `PD_CRITTER_PLAN_PIN`; new `PD_CHAIN_PIN`.

**Interfaces:**
- Consumes: Task 1's fields and arithmetic.
- Produces: a `BlockPlan` whose rooms all carry either `chainIndex >= 0` or `branchOf >= 0`; `plan.entranceIndex` = chain 0; `plan.bossIndex` = the last chain room; `ValidateBlockPlan` refuses every violation listed in spec §5 including the boss cut property.

- [ ] **Step 1: Write the failing structural checks**

Replace `RunBossRoomChecks` (the whole function, `tests/blockplan_harness.cpp` ~1320–1380) with:

```cpp
    // --- Round B: the spine (spec 2026-09-02 §7.1) --------------------------
    //
    // Re-derived from the plan's blocks and sockets, never from the planner's
    // own bookkeeping, so a bug in the validator cannot hide here (the same
    // stance EdgesAgree takes). Runs over its own seeds and its own room /
    // boss matrix, because the batch only ever asks for one boss room.
    // Socket flood from `startBlock`, never entering `skipBlock` (-1 = none).
    void FloodFrom(BlockPlan const& plan, int startBlock, int skipBlock, std::vector<bool>& seen)
    {
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        }
        seen.assign(plan.blocks.size(), false);
        if (startBlock < 0 || startBlock == skipBlock)
        {
            return;
        }
        std::vector<size_t> stack;
        stack.push_back(static_cast<size_t>(startBlock));
        seen[static_cast<size_t>(startBlock)] = true;
        struct Dir { unsigned bit; int dx; int dy; };
        Dir const dirs[4] = { { SOCKET_N, 0, -1 }, { SOCKET_E, 1, 0 }, { SOCKET_S, 0, 1 }, { SOCKET_W, -1, 0 } };
        while (!stack.empty())
        {
            size_t const at = stack.back();
            stack.pop_back();
            PlacedBlock const& b = plan.blocks[at];
            for (Dir const& d : dirs)
            {
                if (!(b.socketMask & d.bit)) continue;
                auto it = index.find(std::make_pair(b.bx + d.dx, b.by + d.dy));
                if (it == index.end()) continue;
                if (static_cast<int>(it->second) == skipBlock) continue;
                if (seen[it->second]) continue;
                seen[it->second] = true;
                stack.push_back(it->second);
            }
        }
    }

    void RunChainChecks(int seeds, bool& sawPocket, bool& sawShortcut)
    {
        char msg[200];
        struct Combo { int rooms; int bossRooms; };
        Combo const combos[] = { { 8, 1 }, { 8, 2 }, { 8, 3 }, { 15, 2 }, { 3, 1 }, { 1, 1 } };
        for (Combo const& combo : combos)
        {
            for (int i = 0; i < seeds; ++i)
            {
                uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 7u;
                BlockCfg cfg = MakeCfg(seed, combo.rooms);
                cfg.bossRooms = combo.bossRooms;

                BlockPlan plan;
                if (!GenerateBlockPlan(cfg, &plan))
                {
                    std::snprintf(msg, sizeof(msg), "generation failed with %d rooms + %d boss",
                                  combo.rooms, combo.bossRooms);
                    Check(false, msg, seed);
                    continue;
                }

                // Own arithmetic, deliberately not PocketCountFor.
                int const total = std::max(2, combo.rooms + combo.bossRooms);
                int wantPockets = std::min(2, total / 3);
                wantPockets = std::min(wantPockets, std::max(0, (total - 1 - combo.bossRooms) / 2));
                int const wantChain = total - wantPockets;

                std::vector<int> chainBlock(static_cast<size_t>(wantChain), -1);
                int pockets = 0, rooms = 0, bosses = 0, strays = 0;
                for (size_t k = 0; k < plan.blocks.size(); ++k)
                {
                    PlacedBlock const& b = plan.blocks[k];
                    if (b.roomId < 0)
                    {
                        Check(b.chainIndex < 0 && b.branchOf < 0 && b.shortcutTo < 0,
                              "a corridor block carries chain fields", seed);
                        continue;
                    }
                    ++rooms;
                    if (b.role == BlockRole::RoomBoss) ++bosses;
                    if (b.chainIndex >= 0)
                    {
                        if (b.chainIndex < wantChain && chainBlock[static_cast<size_t>(b.chainIndex)] < 0)
                        {
                            chainBlock[static_cast<size_t>(b.chainIndex)] = static_cast<int>(k);
                        }
                        else
                        {
                            ++strays;
                        }
                    }
                    else if (b.branchOf >= 0)
                    {
                        ++pockets;
                    }
                    else
                    {
                        ++strays;
                    }
                }
                Check(rooms == total, "room count is not rooms + bossRooms", seed);
                Check(strays == 0, "a room is neither on the chain nor a pocket (or a chain index repeats)", seed);
                std::snprintf(msg, sizeof(msg), "%d pocket(s), want %d", pockets, wantPockets);
                Check(pockets == wantPockets, msg, seed);
                Check(bosses == combo.bossRooms, "boss room count does not match the config", seed);
                bool chainComplete = true;
                for (int idx : chainBlock) if (idx < 0) chainComplete = false;
                Check(chainComplete, "a chain index is missing", seed);
                if (!chainComplete) continue;
                if (pockets > 0) sawPocket = true;

                // Entrance, last boss, boss positions by the formula.
                Check(plan.entranceIndex == chainBlock[0] &&
                      plan.blocks[static_cast<size_t>(chainBlock[0])].role == BlockRole::RoomEntrance,
                      "chain 0 is not the entrance", seed);
                Check(plan.bossIndex == chainBlock[static_cast<size_t>(wantChain - 1)] &&
                      plan.blocks[static_cast<size_t>(plan.bossIndex)].role == BlockRole::RoomBoss,
                      "bossIndex is not the last chain room, or it is not a boss", seed);
                std::vector<bool> isBossIdx(static_cast<size_t>(wantChain), false);
                for (int k = 1; k <= combo.bossRooms; ++k)
                {
                    int const want = (2 * k * (wantChain - 1) + combo.bossRooms) / (2 * combo.bossRooms);
                    isBossIdx[static_cast<size_t>(want)] = true;
                    std::snprintf(msg, sizeof(msg), "boss %d is not at chain index %d", k, want);
                    Check(plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(want)])].role == BlockRole::RoomBoss,
                          msg, seed);
                }
                for (int idx = 1; idx < wantChain; ++idx)
                {
                    PlacedBlock const& b = plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(idx)])];
                    Check(isBossIdx[static_cast<size_t>(idx)] == (b.role == BlockRole::RoomBoss),
                          "a boss sits off its formula position", seed);
                    // SegmentOf agrees with the formula.
                    int wantSeg = combo.bossRooms;
                    for (int k = 1; k <= combo.bossRooms; ++k)
                    {
                        if (idx <= (2 * k * (wantChain - 1) + combo.bossRooms) / (2 * combo.bossRooms)) { wantSeg = k; break; }
                    }
                    Check(SegmentOf(plan, b) == wantSeg, "SegmentOf disagrees with the boss positions", seed);
                }
                Check(SegmentOf(plan, plan.blocks[static_cast<size_t>(chainBlock[0])]) == 0,
                      "the entrance is not segment 0", seed);

                // Pockets: host is an ordinary spine room, one pocket per host,
                // shortcut forward, inside the segment, never onto a boss - and
                // physically what the fields claim: with the HOST removed, a
                // flood from the pocket reaches the shortcut target (there is
                // no other way there), and a dead-end pocket reaches no spine
                // room at all.
                std::vector<bool> hosted(static_cast<size_t>(wantChain), false);
                for (size_t at = 0; at < plan.blocks.size(); ++at)
                {
                    PlacedBlock const& b = plan.blocks[at];
                    if (b.branchOf < 0) continue;
                    Check(b.role == BlockRole::Room, "a pocket is not a plain room", seed);
                    bool const hostOk = b.branchOf >= 1 && b.branchOf < wantChain - 1 &&
                                        !isBossIdx[static_cast<size_t>(b.branchOf)];
                    Check(hostOk, "pocket hosted on the entrance, a boss or off the chain", seed);
                    if (!hostOk) continue;
                    Check(!hosted[static_cast<size_t>(b.branchOf)], "two pockets on one host", seed);
                    hosted[static_cast<size_t>(b.branchOf)] = true;
                    Check(SegmentOf(plan, b) ==
                          SegmentOf(plan, plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.branchOf)])]),
                          "a pocket is not in its host's segment", seed);

                    std::vector<bool> seen;
                    FloodFrom(plan, static_cast<int>(at), chainBlock[static_cast<size_t>(b.branchOf)], seen);
                    if (b.shortcutTo >= 0)
                    {
                        sawShortcut = true;
                        bool ok = b.shortcutTo > b.branchOf && b.shortcutTo < wantChain;
                        for (int j = b.branchOf + 1; ok && j <= b.shortcutTo; ++j)
                        {
                            if (isBossIdx[static_cast<size_t>(j)]) ok = false;
                        }
                        Check(ok, "shortcut goes backward, past or onto a boss", seed);
                        if (ok)
                        {
                            Check(seen[static_cast<size_t>(chainBlock[static_cast<size_t>(b.shortcutTo)])],
                                  "a shortcut is declared but its target is not reachable from the pocket around the host",
                                  seed);
                        }
                    }
                    else
                    {
                        bool touchesSpine = false;
                        for (size_t k = 0; k < plan.blocks.size(); ++k)
                        {
                            if (seen[k] && plan.blocks[k].chainIndex >= 0) touchesSpine = true;
                        }
                        Check(!touchesSpine, "a dead-end pocket reaches the spine around its host", seed);
                    }
                }

                // No junction that is not a stub: a corridor block has exactly
                // two sockets leading to non-stub blocks.
                for (PlacedBlock const& b : plan.blocks)
                {
                    if (b.roomId >= 0 || b.role == BlockRole::CorridorDeadEnd) continue;
                    int through = 0;
                    struct Dir { unsigned bit; int dx; int dy; };
                    Dir const dirs[4] = { { SOCKET_N, 0, -1 }, { SOCKET_E, 1, 0 }, { SOCKET_S, 0, 1 }, { SOCKET_W, -1, 0 } };
                    for (Dir const& d : dirs)
                    {
                        if (!(b.socketMask & d.bit)) continue;
                        PlacedBlock const* n = plan.At(b.bx + d.dx, b.by + d.dy);
                        if (n && n->role != BlockRole::CorridorDeadEnd) ++through;
                    }
                    Check(through == 2, "a corridor block is a junction (more than two non-stub sockets)", seed);
                }

                // Boss cut property: without boss block k nothing behind it is
                // reachable from the entrance.
                for (int idx = 1; idx < wantChain; ++idx)
                {
                    if (!isBossIdx[static_cast<size_t>(idx)]) continue;
                    std::vector<bool> seen;
                    FloodFrom(plan, plan.entranceIndex, chainBlock[static_cast<size_t>(idx)], seen);
                    for (size_t k = 0; k < plan.blocks.size(); ++k)
                    {
                        PlacedBlock const& b = plan.blocks[k];
                        bool const behind = b.chainIndex > idx || b.branchOf > idx;
                        if (behind && seen[k])
                        {
                            std::snprintf(msg, sizeof(msg), "boss at chain %d can be bypassed", idx);
                            Check(false, msg, seed);
                            break;
                        }
                    }
                }
            }
        }
    }
```

In `RunBatch`, replace `RunBossRoomChecks(count / 10 + 1);` with:

```cpp
        bool sawPocket = false;
        bool sawShortcut = false;
        RunChainChecks(count / 10 + 1, sawPocket, sawShortcut);
        Check(sawPocket, "no seed in the sample produced a pocket - the pocket pass is dead code", 0);
        Check(sawShortcut, "no seed in the sample produced a shortcut - the shortcut draw is dead code", 0);
```

In `RunThemeParityChecks`, extend the per-block comparison:

```cpp
                if (a.bx != b.bx || a.by != b.by || a.role != b.role ||
                    a.socketMask != b.socketMask || a.alt != b.alt ||
                    a.chainIndex != b.chainIndex || a.branchOf != b.branchOf ||
                    a.shortcutTo != b.shortcutTo ||
                    b.chunkId - a.chunkId != 10000)
```

- [ ] **Step 2: Build and run to verify the new checks fail on the old generator**

Build (PowerShell), prove freshness, `./pdblock.exe --batch 500`.
Expected: `FAILURES`, dominated by `a chain index is missing` / `a room is neither on the chain nor a pocket` (the old planner leaves every chain field at −1). The freeze pin still passes at this point.

- [ ] **Step 3: Replace the graph helpers with the chain helpers**

In `src/generator/PDBlockPlan.cpp` add `#include <cstddef>` and `#include <cstdlib>` to the includes, and replace everything from `// --- graph helpers ---` through the end of `SelectCorridorEdges` (lines 161–271, i.e. `GraphEdge`, `DistSq`, `FindRoot`, `ScatterRooms`, `SelectCorridorEdges`) with:

```cpp
        // --- Round B chain helpers (spec 2026-09-02 §3, §4) ------------------

        int Manhattan(Cell const& a, Cell const& b)
        {
            return std::abs(a.x - b.x) + std::abs(a.y - b.y);
        }

        // The planning field while a chain is being laid: occupancy, the
        // socket bits as they accumulate, every room cell placed so far (for
        // the gap rule) and the spine in chain order. Small enough (<= 64
        // cells) that the depth-first search copies it per level.
        struct Field
        {
            int size = 0;
            std::vector<uint8_t> occ;       // 0 free, 1 room, 2 corridor
            std::vector<unsigned> masks;    // socket bits per cell
            std::vector<Cell> rooms;
            std::vector<Cell> chain;

            explicit Field(int n)
                : size(n), occ(static_cast<size_t>(n) * static_cast<size_t>(n), 0),
                  masks(static_cast<size_t>(n) * static_cast<size_t>(n), 0u) { }

            size_t Index(Cell const& c) const
            {
                return static_cast<size_t>(c.y) * static_cast<size_t>(size) + static_cast<size_t>(c.x);
            }

            bool Inside(Cell const& c) const
            {
                return c.x >= 0 && c.y >= 0 && c.x < size && c.y < size;
            }

            bool Free(Cell const& c) const
            {
                return Inside(c) && occ[Index(c)] == 0;
            }
        };

        // Interior cells of the L-route from a to b (endpoints excluded), in
        // walking order. xFirst walks the x axis to b.x, then the y axis - the
        // same two walks the v2 planner routed its MST edges with.
        std::vector<Cell> LRouteInterior(Cell const& a, Cell const& b, bool xFirst)
        {
            std::vector<Cell> out;
            Cell cur = a;
            auto walk = [&](int targetX, int targetY)
            {
                while (cur.x != targetX || cur.y != targetY)
                {
                    if (cur.x != targetX)
                    {
                        cur.x += (targetX > cur.x) ? 1 : -1;
                    }
                    else
                    {
                        cur.y += (targetY > cur.y) ? 1 : -1;
                    }
                    if (!(cur == b))
                    {
                        out.push_back(cur);
                    }
                }
            };
            if (xFirst)
            {
                walk(b.x, a.y);
                walk(b.x, b.y);
            }
            else
            {
                walk(a.x, b.y);
                walk(b.x, b.y);
            }
            return out;
        }

        enum : int
        {
            ORDER_NONE = 0,
            ORDER_X_FIRST = 1,
            ORDER_Y_FIRST = 2
        };

        bool InteriorFree(Field const& f, std::vector<Cell> const& interior)
        {
            for (Cell const& c : interior)
            {
                if (!f.Free(c))
                {
                    return false;
                }
            }
            return true;
        }

        // Which L-orders between a and b run over free cells only. This is the
        // rule that makes the layout one path by construction: a corridor is
        // never laid through a room or across another corridor. A straight
        // route (same row or column) is ONE route and reports x-first only,
        // so it never costs an axis draw.
        int FeasibleOrders(Field const& f, Cell const& a, Cell const& b)
        {
            if (a.x == b.x || a.y == b.y)
            {
                return InteriorFree(f, LRouteInterior(a, b, true)) ? ORDER_X_FIRST : ORDER_NONE;
            }
            int orders = ORDER_NONE;
            if (InteriorFree(f, LRouteInterior(a, b, true)))
            {
                orders |= ORDER_X_FIRST;
            }
            if (InteriorFree(f, LRouteInterior(a, b, false)))
            {
                orders |= ORDER_Y_FIRST;
            }
            return orders;
        }

        // The axis coin, drawn ONLY when both orders are open.
        bool ChooseXFirst(PDRandom& rng, int orders)
        {
            if (orders == (ORDER_X_FIRST | ORDER_Y_FIRST))
            {
                return rng.Chance(50);
            }
            return orders == ORDER_X_FIRST;
        }

        // Claims the corridor cells between two room cells and opens the
        // sockets along the way.
        void CommitRoute(Field& f, Cell const& a, Cell const& b, bool xFirst)
        {
            std::vector<Cell> const interior = LRouteInterior(a, b, xFirst);
            std::vector<Cell> path = interior;
            path.push_back(b);
            Cell cur = a;
            for (Cell const& next : path)
            {
                unsigned const outBit = BitForStep(next.x - cur.x, next.y - cur.y);
                f.masks[f.Index(cur)] |= outBit;
                f.masks[f.Index(next)] |= OppositeBit(outBit);
                cur = next;
            }
            for (Cell const& c : interior)
            {
                f.occ[f.Index(c)] = 2;
            }
        }

        struct StepCandidate
        {
            Cell cell;
            int orders = ORDER_NONE;
        };

        int const STEP_MIN = 2;     // Manhattan: one corridor block
        int const STEP_MAX = 3;     // two corridor blocks

        // Cells the next room may take, seen from `from`: the step rule, the
        // room gap against every room placed so far, at least one free
        // L-route. Enumerated in the field's (y, x) order so a draw index
        // means the same on every compiler. `prev` is the room before `from`
        // (nullptr at the entrance and for pockets): candidates heading back
        // toward it are dropped unless they are all there is.
        std::vector<StepCandidate> StepCandidates(Field const& f, Cell const& from, Cell const* prev)
        {
            std::vector<StepCandidate> forward;
            std::vector<StepCandidate> backward;
            for (int y = 0; y < f.size; ++y)
            {
                for (int x = 0; x < f.size; ++x)
                {
                    Cell c;
                    c.x = x;
                    c.y = y;
                    int const d = Manhattan(from, c);
                    if (d < STEP_MIN || d > STEP_MAX || !f.Free(c))
                    {
                        continue;
                    }
                    bool gapOk = true;
                    for (Cell const& r : f.rooms)
                    {
                        if (Manhattan(r, c) < MIN_ROOM_GAP)
                        {
                            gapOk = false;
                            break;
                        }
                    }
                    if (!gapOk)
                    {
                        continue;
                    }
                    StepCandidate cand;
                    cand.cell = c;
                    cand.orders = FeasibleOrders(f, from, c);
                    if (cand.orders == ORDER_NONE)
                    {
                        continue;
                    }
                    bool reversal = false;
                    if (prev)
                    {
                        int const px = from.x - prev->x;
                        int const py = from.y - prev->y;
                        int const sx = c.x - from.x;
                        int const sy = c.y - from.y;
                        reversal = (px * sx + py * sy) < 0;
                    }
                    (reversal ? backward : forward).push_back(cand);
                }
            }
            return forward.empty() ? backward : forward;
        }

        // Commits per attempt before the search gives up on this seed.
        int const CHAIN_BUDGET = 4000;

        // Depth-first over chain positions. Every commit draws; a failed
        // subtree removes the drawn candidate and draws again from what is
        // left, so the draw sequence is a pure function of the seed whatever
        // path the search takes. Every level checks the budget, so an
        // exhausted attempt unwinds at once.
        bool ExtendChain(PDRandom& rng, int chainLen, Field& f, int& budget)
        {
            if (static_cast<int>(f.chain.size()) == chainLen)
            {
                return true;
            }
            Cell const from = f.chain.back();
            Cell const* prev = f.chain.size() >= 2 ? &f.chain[f.chain.size() - 2] : nullptr;
            std::vector<StepCandidate> cands = StepCandidates(f, from, prev);
            while (!cands.empty())
            {
                if (budget <= 0)
                {
                    return false;
                }
                --budget;
                size_t const pick = static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(cands.size()) - 1));
                StepCandidate const cand = cands[pick];

                Field next = f;
                CommitRoute(next, from, cand.cell, ChooseXFirst(rng, cand.orders));
                next.occ[next.Index(cand.cell)] = 1;
                next.rooms.push_back(cand.cell);
                next.chain.push_back(cand.cell);
                if (ExtendChain(rng, chainLen, next, budget))
                {
                    f = next;
                    return true;
                }
                cands.erase(cands.begin() + static_cast<std::ptrdiff_t>(pick));
            }
            return false;
        }

        struct Pocket
        {
            Cell cell;
            int host = -1;          // chain index
            int shortcutTo = -1;    // chain index, -1 = dead end
        };

        bool IsBossIndex(std::vector<int> const& bosses, int idx)
        {
            return std::find(bosses.begin(), bosses.end(), idx) != bosses.end();
        }

        int NextBossAfter(std::vector<int> const& bosses, int idx)
        {
            for (int b : bosses)        // ascending by construction
            {
                if (b > idx)
                {
                    return b;
                }
            }
            return -1;
        }

        // Pockets hang off ordinary spine rooms, one each, placed like a chain
        // step without the direction bias. A pocket may carry a shortcut to a
        // later spine room of its own segment (never onto or past a boss -
        // the boss room must stay a cut of the graph for B4's barrier).
        bool PlacePockets(BlockCfg const& cfg, PDRandom& rng, std::vector<int> const& bosses,
                          int pockets, Field& f, std::vector<Pocket>& out)
        {
            int const chainLen = static_cast<int>(f.chain.size());
            std::vector<bool> hosted(static_cast<size_t>(chainLen), false);
            for (int p = 0; p < pockets; ++p)
            {
                std::vector<int> hosts;
                for (int i = 1; i < chainLen - 1; ++i)
                {
                    if (IsBossIndex(bosses, i) || hosted[static_cast<size_t>(i)])
                    {
                        continue;
                    }
                    if (StepCandidates(f, f.chain[static_cast<size_t>(i)], nullptr).empty())
                    {
                        continue;
                    }
                    hosts.push_back(i);
                }
                if (hosts.empty())
                {
                    return false;
                }
                int const host = hosts[static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(hosts.size()) - 1))];
                Cell const from = f.chain[static_cast<size_t>(host)];
                std::vector<StepCandidate> const cands = StepCandidates(f, from, nullptr);
                StepCandidate const cand = cands[static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(cands.size()) - 1))];
                CommitRoute(f, from, cand.cell, ChooseXFirst(rng, cand.orders));
                f.occ[f.Index(cand.cell)] = 1;
                f.rooms.push_back(cand.cell);
                hosted[static_cast<size_t>(host)] = true;

                Pocket pocket;
                pocket.cell = cand.cell;
                pocket.host = host;

                if (rng.Chance(cfg.loopChancePct))
                {
                    int const segmentBoss = NextBossAfter(bosses, host);
                    std::vector<std::pair<int, int>> targets;      // (chain index, orders)
                    for (int j = host + 1; j < segmentBoss; ++j)
                    {
                        int const orders = FeasibleOrders(f, cand.cell, f.chain[static_cast<size_t>(j)]);
                        if (orders != ORDER_NONE)
                        {
                            targets.push_back(std::make_pair(j, orders));
                        }
                    }
                    if (!targets.empty())
                    {
                        std::pair<int, int> const target = targets[static_cast<size_t>(
                            rng.UniformInt(0, static_cast<int>(targets.size()) - 1))];
                        CommitRoute(f, cand.cell, f.chain[static_cast<size_t>(target.first)],
                                    ChooseXFirst(rng, target.second));
                        pocket.shortcutTo = target.first;
                    }
                }
                out.push_back(pocket);
            }
            return true;
        }
```

- [ ] **Step 4: Replace the body of `GenerateBlockPlan`**

Replace the whole function (from `bool GenerateBlockPlan(BlockCfg const& cfg, BlockPlan* out)` to its closing brace before `std::string EmitManifest`) with:

```cpp
    bool GenerateBlockPlan(BlockCfg const& cfg, BlockPlan* out)
    {
        if (!out || cfg.fieldBlocks < 2)
        {
            return false;
        }

        // Round B (spec 2026-09-02): the budget is arithmetic, not a draw.
        int const total = std::max(2, cfg.rooms + cfg.bossRooms);
        int const bosses = cfg.bossRooms > 0 ? cfg.bossRooms : 1;
        int const pocketsWanted = PocketCountFor(cfg.rooms, cfg.bossRooms, cfg.branches);
        int const chainLen = total - pocketsWanted;
        if (chainLen - 1 < bosses)
        {
            return false;       // cannot seat N distinct bosses on the spine
        }
        std::vector<int> bossIdx;
        for (int k = 1; k <= bosses; ++k)
        {
            bossIdx.push_back(BossChainIndex(chainLen, cfg.bossRooms, k));
        }

        // DRAW ORDER (the contract every stored seed depends on; the layout
        // version is bumped when it changes):
        //   1. start cell: x then y                     (2 draws)
        //   2. each chain step: a candidate index, then the axis coin only if
        //      both L-orders are open; backtracking re-draws from the
        //      shrunken list (ExtendChain)
        //   3. per pocket: host index, candidate index, axis coin (if both),
        //      the shortcut Chance(loopChancePct), then target index and
        //      axis coin (if both) only when the Chance hit AND a target exists
        //   4. dead-end stubs: count, then one index per stub (unchanged code)
        //   5. visual alternates, one per multi-alt block, last (unchanged)
        // Nothing else draws. Theme moves no draw.
        for (int attempt = 0; attempt < cfg.maxTries; ++attempt)
        {
            uint32_t const seed = cfg.seed + static_cast<uint32_t>(attempt);
            PDRandom rng(seed);

            Field field(cfg.fieldBlocks);
            Cell start;
            start.x = rng.UniformInt(0, cfg.fieldBlocks - 1);
            start.y = rng.UniformInt(0, cfg.fieldBlocks - 1);
            field.occ[field.Index(start)] = 1;
            field.rooms.push_back(start);
            field.chain.push_back(start);

            int budget = CHAIN_BUDGET;
            if (!ExtendChain(rng, chainLen, field, budget))
            {
                continue;       // this seed cannot lay the chain on this field
            }

            std::vector<Pocket> pockets;
            if (!PlacePockets(cfg, rng, bossIdx, pocketsWanted, field, pockets))
            {
                continue;       // no host with room for a pocket - next seed
            }

            // Hand over to the ordered (y, x) map the rest of the pipeline has
            // always worked on: the stub pass, the depth BFS and the
            // materialisation all iterate it, which is what keeps the block
            // order and the alt draws reproducible.
            std::map<Cell, unsigned> masks;
            std::map<Cell, int> roomOf;         // cell -> room id, rooms only
            std::map<Cell, int> chainOf;        // cell -> chain index, spine only
            std::map<Cell, size_t> pocketOf;    // cell -> index into `pockets`
            for (int y = 0; y < field.size; ++y)
            {
                for (int x = 0; x < field.size; ++x)
                {
                    Cell c;
                    c.x = x;
                    c.y = y;
                    if (field.occ[field.Index(c)] != 0)
                    {
                        masks[c] = field.masks[field.Index(c)];
                    }
                }
            }
            for (size_t i = 0; i < field.chain.size(); ++i)
            {
                roomOf[field.chain[i]] = static_cast<int>(i);
                chainOf[field.chain[i]] = static_cast<int>(i);
            }
            for (size_t p = 0; p < pockets.size(); ++p)
            {
                roomOf[pockets[p].cell] = chainLen + static_cast<int>(p);
                pocketOf[pockets[p].cell] = p;
            }

            // Dead-end stubs, AFTER every routing draw: the whole layout up to
            // here consumes exactly the draws it consumed before, so the stub
            // pass is additive to the stream, never a reshuffle.
            // A stub is one extra block hanging off an existing cell through a
            // socket the host did not have - a side passage worth peeking into
            // (the kit puts a chest there and no spawns).
            if (cfg.maxDeadEnds > 0)
            {
                int const wantStubs = rng.UniformInt(0, cfg.maxDeadEnds);
                for (int placedStubs = 0; placedStubs < wantStubs; ++placedStubs)
                {
                    // Candidates recomputed per stub over the ordered map, so
                    // a placed stub both claims its cell and becomes a host
                    // itself; the order is the map's own (y, x) order.
                    std::vector<std::pair<Cell, unsigned>> candidates;
                    for (auto const& kv : masks)
                    {
                        for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                        {
                            if (kv.second & bit)
                            {
                                continue;   // that side already leads somewhere
                            }
                            int dx = 0, dy = 0;
                            StepFor(bit, dx, dy);
                            Cell n;
                            n.x = kv.first.x + dx;
                            n.y = kv.first.y + dy;
                            if (n.x < 0 || n.y < 0 || n.x >= cfg.fieldBlocks ||
                                n.y >= cfg.fieldBlocks)
                            {
                                continue;
                            }
                            if (masks.find(n) != masks.end())
                            {
                                continue;   // occupied - that would be a loop, not a stub
                            }
                            candidates.push_back(std::make_pair(kv.first, bit));
                        }
                    }
                    if (candidates.empty())
                    {
                        break;
                    }
                    auto const& pick = candidates[static_cast<size_t>(
                        rng.UniformInt(0, static_cast<int>(candidates.size()) - 1))];
                    int dx = 0, dy = 0;
                    StepFor(pick.second, dx, dy);
                    Cell stub;
                    stub.x = pick.first.x + dx;
                    stub.y = pick.first.y + dy;
                    masks[pick.first] |= pick.second;
                    masks[stub] = OppositeBit(pick.second);
                }
            }

            // Depth over the block graph from the entrance (chain 0). Nothing
            // downstream reads it today; it stays the BFS depth it always was.
            std::map<Cell, int> depth;
            std::queue<Cell> q;
            Cell const entranceCell = field.chain[0];
            depth[entranceCell] = 0;
            q.push(entranceCell);
            while (!q.empty())
            {
                Cell const c = q.front();
                q.pop();
                unsigned const mask = masks[c];
                for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                {
                    if (!(mask & bit)) continue;
                    int dx = 0, dy = 0;
                    StepFor(bit, dx, dy);
                    Cell n;
                    n.x = c.x + dx;
                    n.y = c.y + dy;
                    if (masks.find(n) == masks.end()) continue;
                    if (depth.find(n) != depth.end()) continue;
                    depth[n] = depth[c] + 1;
                    q.push(n);
                }
            }

            // Materialise. Fixed iteration order (masks is an ordered map keyed
            // by (y, x)), so the block list is reproducible.
            BlockPlan plan;
            plan.config = cfg;
            plan.effectiveSeed = seed;
            for (auto const& kv : masks)
            {
                Cell const& c = kv.first;
                unsigned const mask = kv.second;

                PlacedBlock b;
                b.bx = cfg.originBX + c.x;
                b.by = cfg.originBY + c.y;
                b.socketMask = mask;

                auto rit = roomOf.find(c);
                if (rit != roomOf.end())
                {
                    b.roomId = rit->second;
                    auto cit = chainOf.find(c);
                    if (cit != chainOf.end())
                    {
                        b.chainIndex = cit->second;
                        b.role = (cit->second == 0)             ? BlockRole::RoomEntrance
                               : IsBossIndex(bossIdx, cit->second) ? BlockRole::RoomBoss
                                                                   : BlockRole::Room;
                    }
                    else
                    {
                        Pocket const& pocket = pockets[pocketOf[c]];
                        b.role = BlockRole::Room;
                        b.branchOf = pocket.host;
                        b.shortcutTo = pocket.shortcutTo;
                    }
                }
                else
                {
                    b.role = CorridorRoleFor(mask);
                }

                auto dit = depth.find(c);
                b.depth = (dit == depth.end()) ? -1 : dit->second;

                // Visual alternate, drawn LAST of all draws (stubs included)
                // and only where the kit ships more than one look - a
                // single-variant family must not consume a draw, or adding an
                // alt to one role would reshuffle every other block's choice.
                int const altCount = AltCountFor(b.role);
                b.alt = altCount > 1 ? rng.UniformInt(0, altCount - 1) : 0;
                // The theme moves only the id BASE, never a draw: the same
                // seed lays out the same dungeon in every theme, and stored
                // layouts stay draw-stable across a theme config change.
                b.chunkId = ChunkIdFor(cfg.theme, b.role, b.socketMask, b.alt);

                if (b.role == BlockRole::RoomEntrance)
                {
                    plan.entranceIndex = static_cast<int>(plan.blocks.size());
                }
                else if (b.chainIndex == chainLen - 1)
                {
                    // The END of the dungeon: the last chain room, always a
                    // boss. The manifest, the harness and the HUD read
                    // bossIndex as the landmark the run finishes at.
                    plan.bossIndex = static_cast<int>(plan.blocks.size());
                }
                plan.blocks.push_back(b);
            }

            std::string error;
            if (!ValidateBlockPlan(plan, &error))
            {
                continue;       // try the next seed rather than ship a broken layout
            }

            *out = plan;
            return true;
        }
        return false;
    }
```

The `Node` struct near the top of the anonymous namespace is now unused: delete it (MSVC `/W4` does not warn on an unused struct, but dead code is a review finding).

- [ ] **Step 5: Extend `ValidateBlockPlan`**

Inside `ValidateBlockPlan`, after the per-block loop and before the socket-agreement loop (`// Every open socket must be answered by the neighbour.`), insert:

```cpp
        // Round B: the spine (spec 2026-09-02 §5). Chain indices are exactly
        // 0..L-1 once each, the entrance is chain 0, the last chain room is a
        // boss, bosses sit at their formula positions, pockets hang off
        // ordinary spine rooms (one each) and a shortcut lands forward on a
        // non-boss room of the same segment.
        std::vector<int> chainBlock;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            PlacedBlock const& b = plan.blocks[i];
            if (b.chainIndex < 0)
            {
                continue;
            }
            if (b.roomId < 0)
            {
                return fail("a corridor block carries a chain index");
            }
            if (static_cast<size_t>(b.chainIndex) >= chainBlock.size())
            {
                chainBlock.resize(static_cast<size_t>(b.chainIndex) + 1, -1);
            }
            if (chainBlock[static_cast<size_t>(b.chainIndex)] != -1)
            {
                return fail("two blocks share a chain index");
            }
            chainBlock[static_cast<size_t>(b.chainIndex)] = static_cast<int>(i);
        }
        if (chainBlock.size() < 2)
        {
            return fail("the chain has fewer than two rooms");
        }
        for (int idx : chainBlock)
        {
            if (idx < 0)
            {
                return fail("a chain index is missing");
            }
        }
        int const chainLen = static_cast<int>(chainBlock.size());
        int const wantBosses = plan.config.bossRooms > 0 ? plan.config.bossRooms : 1;
        if (plan.entranceIndex != chainBlock[0] ||
            plan.blocks[static_cast<size_t>(chainBlock[0])].role != BlockRole::RoomEntrance)
        {
            return fail("chain 0 is not the entrance");
        }
        if (plan.bossIndex != chainBlock[static_cast<size_t>(chainLen - 1)] ||
            plan.blocks[static_cast<size_t>(plan.bossIndex)].role != BlockRole::RoomBoss)
        {
            return fail("bossIndex is not the last chain room, or it is not a boss");
        }
        int bossCount = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.role == BlockRole::RoomBoss)
            {
                ++bossCount;
                if (b.chainIndex < 1)
                {
                    return fail("a boss room is off the spine");
                }
            }
        }
        if (bossCount != wantBosses)
        {
            return fail("boss room count does not match the config");
        }
        for (int k = 1; k <= wantBosses; ++k)
        {
            int const at = BossChainIndex(chainLen, plan.config.bossRooms, k);
            if (at < 1 || at >= chainLen ||
                plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(at)])].role != BlockRole::RoomBoss)
            {
                return fail("a boss room is off its formula position");
            }
        }
        std::vector<bool> hosted(static_cast<size_t>(chainLen), false);
        int pocketCount = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId < 0 || b.chainIndex >= 0)
            {
                continue;
            }
            if (b.branchOf < 0)
            {
                return fail("a room is neither on the chain nor a pocket");
            }
            ++pocketCount;
            if (b.role != BlockRole::Room)
            {
                return fail("a pocket room carries the wrong role");
            }
            if (b.branchOf < 1 || b.branchOf >= chainLen - 1 ||
                plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.branchOf)])].role != BlockRole::Room)
            {
                return fail("a pocket hangs off the entrance, a boss or nothing");
            }
            if (hosted[static_cast<size_t>(b.branchOf)])
            {
                return fail("two pockets on one host");
            }
            hosted[static_cast<size_t>(b.branchOf)] = true;
            if (b.shortcutTo >= 0)
            {
                if (b.shortcutTo <= b.branchOf || b.shortcutTo >= chainLen)
                {
                    return fail("a shortcut does not lead forward");
                }
                for (int j = b.branchOf + 1; j <= b.shortcutTo; ++j)
                {
                    if (plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(j)])].role == BlockRole::RoomBoss)
                    {
                        return fail("a shortcut crosses or lands on a boss room");
                    }
                }
            }
        }
        if (pocketCount != PocketCountFor(plan.config.rooms, plan.config.bossRooms, plan.config.branches))
        {
            return fail("pocket count does not match the config");
        }
```

Then replace the connectivity block at the end of the function (from `// Connectivity: every block must be reachable` to the final `return true;`) with a version that also proves the boss cut property:

```cpp
        // Connectivity: every block must be reachable from the entrance through
        // open sockets, or part of the dungeon is unplayable. The same flood,
        // run once more per boss room with that room removed, proves the boss
        // cut property: nothing behind a boss is reachable around it, so B4's
        // barrier on its doorway is a real gate.
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        }
        auto flood = [&](int skipBlock, std::vector<bool>& visited)
        {
            visited.assign(plan.blocks.size(), false);
            std::queue<size_t> q;
            q.push(static_cast<size_t>(plan.entranceIndex));
            visited[static_cast<size_t>(plan.entranceIndex)] = true;
            size_t reached = 1;
            while (!q.empty())
            {
                PlacedBlock const& b = plan.blocks[q.front()];
                q.pop();
                for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                {
                    if (!(b.socketMask & bit)) continue;
                    int dx = 0, dy = 0;
                    StepFor(bit, dx, dy);
                    auto it = index.find(std::make_pair(b.bx + dx, b.by + dy));
                    if (it == index.end()) continue;
                    if (static_cast<int>(it->second) == skipBlock) continue;
                    if (visited[it->second]) continue;
                    visited[it->second] = true;
                    ++reached;
                    q.push(it->second);
                }
            }
            return reached;
        };

        std::vector<bool> visited;
        if (flood(-1, visited) != plan.blocks.size())
        {
            return fail("some blocks are unreachable from the entrance");
        }
        for (int idx = 1; idx < chainLen; ++idx)
        {
            int const bossBlock = chainBlock[static_cast<size_t>(idx)];
            if (plan.blocks[static_cast<size_t>(bossBlock)].role != BlockRole::RoomBoss)
            {
                continue;
            }
            flood(bossBlock, visited);
            for (size_t i = 0; i < plan.blocks.size(); ++i)
            {
                PlacedBlock const& b = plan.blocks[i];
                if (visited[i] && (b.chainIndex > idx || b.branchOf > idx))
                {
                    return fail("a boss room can be bypassed");
                }
            }
        }
        return true;
```

- [ ] **Step 6: The pocket glyph**

In `AsciiBlockDump`, replace `case BlockRole::Room: out += 'R'; break;` with:

```cpp
                    case BlockRole::Room:         out += (b->branchOf >= 0) ? 'r' : 'R'; break;
```

- [ ] **Step 7: Build, run, read the pins that moved**

Build (PowerShell), prove freshness, `./pdblock.exe --batch 500 > batch.txt; head -60 batch.txt`.
Expected: the chain checks pass; **exactly** these failures remain, all by design: `pinned manifest is N bytes / E;xxxxxxxx, was 571 / E;13df5510 - the bossRooms=1 layout MOVED` (twice, same message), `the decor plan moved: <string>`, `the critter plan moved: <string>`. Any other failure is a generator bug — fix it before touching a pin.

If `generation failed` appears for a combo, raise nothing yet: note the seed and combo in the task report; Task 3's sweep decides the lever.

- [ ] **Step 8: Re-pin, bump the layout version**

In `RunLayoutFreezeCheck` replace the three constants and the comment header with the values read from the message:

```cpp
        // Re-pinned 2026-09-02 with PD_LAYOUT_VERSION 3 (Round B: the chain
        // generator replaces scatter + MST; every stored seed rerolls once,
        // by design). The v2 pin was 571 / E;85fc0e4c, the v1 pin
        // 551 / E;13df5510.
        uint32_t const PINNED_SEED = 12345u;
        int const PINNED_ROOMS = 5;
        size_t const PINNED_BYTES = <N from the message>;
        char const* const PINNED_TRAILER = "E;<xxxxxxxx from the message>\n";
```

and in the `snprintf` of that function replace the literal `"E;13df5510"` with `"E;85fc0e4c"` (the message names the previous pin).

Replace `PD_DECOR_PLAN_PIN` and `PD_CRITTER_PLAN_PIN` with the strings printed after `the decor plan moved: ` and `the critter plan moved: `, verbatim, and update their comment header's first line to `// Re-captured 2026-09-02 for the Round B chain layout (the seed-12345 plan moved).`

In `src/PDv2Mgr.h` replace the version constant and add its paragraph:

```cpp
    // v2 (2026-08-30, Phase 2): dead-end stubs and visual alternates draw
    // from the stream, so a v1 seed no longer reproduces its stored layout.
    // Every stored dungeon rerolls once on first entry; dlvl/dxp are
    // untouched by design (layout columns update via ON DUPLICATE KEY only).
    //
    // v3 (2026-09-02, Round B): the chain generator replaces scatter + MST -
    // rooms are laid as one path through the boss rooms with pockets and
    // segment-local shortcuts, `gen_branches` joins the generation inputs,
    // and LoopChance now means "a pocket carries a shortcut". Every stored
    // layout rerolls once; dlvl/dxp untouched, as before.
    constexpr uint32_t PD_LAYOUT_VERSION = 3;
```

- [ ] **Step 9: Add the chain pin**

Below `CheckCritterPlanPinned` add:

```cpp
    // Round B: the chain itself, pinned. RunLayoutFreezeCheck pins the
    // manifest bytes and would notice most draw-order moves, but two
    // different chains can in principle emit the same block set; this pin
    // reads the chain order, the pockets and the shortcuts directly.
    // Captured by RUNNING `pdblock --batch` and reading the "the chain moved"
    // message, never by reasoning about the value.
    char const* const PD_CHAIN_PIN = "";

    std::string ChainPinString(BlockPlan const& plan)
    {
        int const len = ChainLength(plan);
        std::vector<PlacedBlock const*> chain(static_cast<size_t>(len), nullptr);
        std::vector<PlacedBlock const*> pockets;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.chainIndex >= 0) chain[static_cast<size_t>(b.chainIndex)] = &b;
            if (b.branchOf >= 0) pockets.push_back(&b);
        }
        std::string got;
        char buf[64];
        for (PlacedBlock const* b : chain)
        {
            std::snprintf(buf, sizeof(buf), "%d,%d;", b ? b->bx : -1, b ? b->by : -1);
            got += buf;
        }
        got += '|';
        for (PlacedBlock const* p : pockets)
        {
            std::snprintf(buf, sizeof(buf), "%d>%d,%d>%d;", p->branchOf, p->bx, p->by, p->shortcutTo);
            got += buf;
        }
        return got;
    }

    bool CheckChainPinned(std::string& why)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(12345u, 5), &plan))
        {
            why = "the pinned chain could not generate a layout";
            return false;
        }
        std::string const got = ChainPinString(plan);
        if (got != PD_CHAIN_PIN)
        {
            why = "the chain moved: " + got;
            return false;
        }
        return true;
    }
```

In `RunBatch`, right after the `CheckNoBossSpawnDrawPinned` block:

```cpp
        {
            std::string why;
            bool const ok = CheckChainPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
```

Build, run `--batch 500`, read `the chain moved: <string>`, paste the string into `PD_CHAIN_PIN`, rebuild.

- [ ] **Step 10: All three gates green**

Build (PowerShell), prove freshness. Run `--batch 500`, `--decor-batch 3000`, `--roomcap 3000`.
Expected: `ALL CHECKS PASS` / `0 failure(s)` for the first two. For `--roomcap`, record the printed table and the two cap lines in the task report; **if it prints `THE ENCODED CAP IS TOO HIGH`, that is Task 3's job, not a failure of this task** — report the largest clean room count per theme.

- [ ] **Step 11: Code style, commit**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && python "C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk/apps/codestyle/codestyle-cpp.py"
git add src/generator/PDBlockPlan.cpp src/PDv2Mgr.h tests/blockplan_harness.cpp
git -c core.autocrlf=false commit -m "feat(generator): chain generator - one path through the boss rooms, pockets, segment-local shortcuts"
```

---

### Task 3: Chain view, engine-field sweep, room-cap re-measurement

**Files:**
- Modify: `tests/blockplan_harness.cpp` — `PrintOne` (~292), `PrintPath` (~626), new `ChainSummary`, new `RunEngineFieldSweep`, `RunBatch` summary lines.
- Modify: `src/generator/PDv2GameMath.h:105-135` — the measurement comment table and, only if the measurement says so, `PD_GAME_ROOMS_CAP_MEASURED` / `GameFieldBlocksForRooms`.

**Interfaces:**
- Consumes: Task 1's `ChainLength`; Task 2's plan fields.
- Produces: `std::string ChainSummary(BlockPlan const&)` (harness-local); the `--path` output shape the operator document quotes.

- [ ] **Step 1: The chain summary and the two views**

Add before `void PrintOne(uint32_t seed, int rooms)`:

```cpp
    // Round B: the spine in one glance - the chain in order, every pocket
    // with its host and its shortcut, the segments. Printed by the single
    // layout and by --path, and quoted by the operator document.
    std::string ChainSummary(BlockPlan const& plan)
    {
        int const len = ChainLength(plan);
        std::vector<PlacedBlock const*> chain(static_cast<size_t>(len), nullptr);
        std::vector<PlacedBlock const*> pockets;
        int bosses = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.chainIndex >= 0) chain[static_cast<size_t>(b.chainIndex)] = &b;
            if (b.branchOf >= 0) pockets.push_back(&b);
            if (b.role == BlockRole::RoomBoss) ++bosses;
        }

        std::string out;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "chain (%d rooms, %d boss): ", len, bosses);
        out += buf;
        for (int i = 0; i < len; ++i)
        {
            PlacedBlock const* b = chain[static_cast<size_t>(i)];
            if (!b)
            {
                out += (i ? " -> ?" : "?");
                continue;
            }
            char const tag = b->role == BlockRole::RoomEntrance ? 'E'
                           : b->role == BlockRole::RoomBoss     ? 'B' : 'R';
            std::snprintf(buf, sizeof(buf), "%s%c#%d (%d,%d)", i ? " -> " : "", tag, i, b->bx, b->by);
            out += buf;
        }
        out += '\n';

        if (!pockets.empty())
        {
            out += "pockets:";
            for (PlacedBlock const* p : pockets)
            {
                if (p->shortcutTo >= 0)
                {
                    std::snprintf(buf, sizeof(buf), "  R#%d + pocket (%d,%d) [shortcut -> R#%d]",
                                  p->branchOf, p->bx, p->by, p->shortcutTo);
                }
                else
                {
                    std::snprintf(buf, sizeof(buf), "  R#%d + pocket (%d,%d) [dead end]",
                                  p->branchOf, p->bx, p->by);
                }
                out += buf;
            }
            out += '\n';
        }

        int segStart = 1;
        int k = 0;
        for (int i = 1; i < len; ++i)
        {
            PlacedBlock const* b = chain[static_cast<size_t>(i)];
            if (!b || b->role != BlockRole::RoomBoss) continue;
            ++k;
            int inSegment = 0;
            for (PlacedBlock const* p : pockets)
            {
                if (p->branchOf >= segStart && p->branchOf < i) ++inSegment;
            }
            std::snprintf(buf, sizeof(buf), "segment %d: chain %d..%d, pockets %d, boss B#%d\n",
                          k, segStart, i, inSegment, i);
            out += buf;
            segStart = i + 1;
        }
        return out;
    }
```

In `PrintOne`, replace the legend line and print the summary before the dump:

```cpp
        std::printf("E = entrance, B = boss, R = spine room, r = pocket room, D = dead end, | - + = corridor\n\n");
        std::printf("%s\n", ChainSummary(plan).c_str());
        std::printf("%s\n", AsciiBlockDump(plan).c_str());
```

In `PrintPath`, right after the walk grid is built and its line printed (after the `grid %dx%d cells` printf), add:

```cpp
        std::printf("\n%s\n", ChainSummary(plan).c_str());
```

- [ ] **Step 2: Build and look at three seeds**

Build (PowerShell), prove freshness, then:

```bash
./pdblock.exe --path 12345 5
./pdblock.exe --path 777 8
./pdblock.exe 297397130 13
```

Expected: a `chain (...)` line, `pockets:` (when any), `segment k:` lines, then the grid with `*` from `E#0` to the last `B#`. The ASCII dump shows `r` for pockets. Paste all three outputs into the task report.

- [ ] **Step 3: The engine-field sweep (failing first)**

Add before `int RunBatch(int count, int rooms)`:

```cpp
    // Round B: the field the ENGINE runs. PDv2Mgr::GeneratePlan shrinks the
    // field to GameFieldBlocksForRooms(rooms) (capped at the configured 8),
    // which the fixed 8x8 batch never exercised. A chain packs tighter than
    // an MST, so every (rooms, field, bossRooms) row the engine can ask for
    // must generate on every seed, validate, and fit the manifest budget.
    void RunEngineFieldSweep(int seeds)
    {
        std::printf("engine-field sweep, %d seeds per row (branches 2)\n", seeds);
        std::printf("  rooms  field  boss  genfail  maxManifest\n");
        for (int rooms = PD_GAME_ROOMS_MIN; rooms <= PD_GAME_ROOMS_CAP_MEASURED; ++rooms)
        {
            int const field = std::min(PD_GAME_FIELD_BLOCKS_HARD_MAX, GameFieldBlocksForRooms(rooms));
            int const boss = GameBossRooms(rooms > 3 ? rooms - 3 : 0);
            int failures = 0;
            size_t maxManifest = 0;
            for (int i = 0; i < seeds; ++i)
            {
                uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 5u;
                BlockCfg cfg = MakeCfg(seed, rooms);
                cfg.bossRooms = boss;
                cfg.fieldBlocks = field;
                BlockPlan plan;
                if (!GenerateBlockPlan(cfg, &plan))
                {
                    ++failures;
                    continue;
                }
                std::string err;
                Check(ValidateBlockPlan(plan, &err), err.empty() ? "sweep layout failed validation" : err.c_str(), seed);
                std::string const m = EmitManifest(plan, 99);
                maxManifest = (m.size() > maxManifest) ? m.size() : maxManifest;
            }
            std::printf("  %5d  %5d  %4d  %7d  %11d\n", rooms, field, boss, failures,
                        static_cast<int>(maxManifest));
            char msg[200];
            std::snprintf(msg, sizeof(msg),
                          "engine field %dx%d cannot seat %d room(s) + %d boss on %d of %d seeds",
                          field, field, rooms, boss, failures, seeds);
            Check(failures == 0, msg, 0);
            Check(maxManifest <= static_cast<size_t>(PD_GAME_MANIFEST_BUDGET_B),
                  "an engine-field layout is over the manifest budget", 0);
        }
        std::printf("\n");
    }
```

In `RunBatch`, right after the `measuredCap` block (before `size_t maxManifest = 0;`):

```cpp
        RunEngineFieldSweep(count);
```

And extend the batch summary: track pockets and shortcuts in the per-seed loop —

```cpp
            int pocketsHere = 0;
            int shortcutsHere = 0;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.branchOf >= 0) ++pocketsHere;
                if (b.shortcutTo >= 0) ++shortcutsHere;
            }
            minPockets = (pocketsHere < minPockets) ? pocketsHere : minPockets;
            maxPockets = (pocketsHere > maxPockets) ? pocketsHere : maxPockets;
            shortcutLayouts += shortcutsHere ? 1 : 0;
```

with `int minPockets = 1 << 30, maxPockets = 0, shortcutLayouts = 0;` declared beside `minBlocks`, and one summary line after `blocks per layout`:

```cpp
        std::printf("pockets per layout: %d..%d, %d of %d layouts carry a shortcut\n",
                    minPockets, maxPockets, shortcutLayouts, count);
```

- [ ] **Step 4: Build, run, read the sweep**

Build (PowerShell), prove freshness, `./pdblock.exe --batch 500`.
Expected: the sweep table with 15 rows. **If every row has `genfail 0`**: `ALL CHECKS PASS`, go to Step 6. **If a row fails**, go to Step 5.

- [ ] **Step 5 (only if the sweep fails): move the field lever**

The lever is cells per room in `GameFieldBlocksForRooms` (`src/generator/PDv2GameMath.h`, `int const wantCells = rooms * 4;`). Change it to `rooms * 5`, update the comment above it ("Target: keep cells-per-room roughly constant at ~5 — raised from 4 for the Round B chain, which lays a corridor per step instead of sharing MST edges"), rebuild, re-run. The existing field-size checks (`RunGameMathChecks`, ~1049–1090) test monotonicity and bounds, not the constant, and stay green. If the 8×8 rows (rooms ≥ 13) are the ones failing, the lever is instead `PD_GAME_ROOMS_CAP_MEASURED` in Step 6 — do not widen the field past 8.

- [ ] **Step 6: Re-measure the room cap and record it**

```bash
./pdblock.exe --roomcap 3000 > roomcap.txt; cat roomcap.txt
```

Replace the measurement table in the `PD_GAME_ROOMS_CAP_MEASURED` comment (`src/generator/PDv2GameMath.h`, the `//   rooms  bossRooms  room cells  gen failures  max manifest B` block) with the rows for 12–15 from `roomcap.txt`, dated `Measured 2026-09-02 (Round B chain generator), 3000 seeds per row:`, and rewrite the two paragraphs below it to what the sweep showed (where the manifest saturates; at which room count packing first fails). If the printed `largest room count clean on every seed` is below 15 for either theme, set `PD_GAME_ROOMS_CAP_MEASURED` to the smaller of the two and state so in the comment. Rebuild and re-run `--batch 500` and `--roomcap 3000`: both must end green.

- [ ] **Step 7: Commit**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && python "C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk/apps/codestyle/codestyle-cpp.py"
git add tests/blockplan_harness.cpp src/generator/PDv2GameMath.h
git -c core.autocrlf=false commit -m "test(harness): chain view, engine-field sweep, room cap re-measured for the chain"
```

---

### Task 4: Engine glue — config key, persistence, info line

**Files:**
- Modify: `src/PDv2Mgr.h:64` (`PDv2Config`), `src/PDv2Mgr.cpp:49` (`LoadConfig`), `:123` (`GeneratePlan`), `:168-179` (`SavePlanToDB`), `:313-345` (`LoadPlanFromDB`)
- Modify: `src/PDv2Commands.cpp:196-202` (`HandleV2InfoCommand`)
- Modify: `conf/mod_procedural_dungeon.conf.dist:298-304`
- Create: `data/sql/db-characters/mod_pdungeon_account_branches.sql`

**Interfaces:**
- Consumes: `BlockCfg::branches` (Task 1).
- Produces: `PDv2Config::branches`; column `pdungeon_account.gen_branches`; the `.pdungeon v2 info` config line ending in `| pockets N | shortcut P%`.

- [ ] **Step 1: The SQL file**

Create `data/sql/db-characters/mod_pdungeon_account_branches.sql`:

```sql
-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.gen_branches (characters database)
--
-- Round B (2026-09-02): the chain generator takes one more generation input,
-- the number of pocket rooms (ProceduralDungeon.V2.Branches). A layout is
-- stored as its seed plus the inputs it was generated with and regenerated on
-- login, so the input has to be stored beside gen_loop_pct - read live from
-- the conf it would reshape every stored dungeon the day the operator tunes
-- it and trip the "PD_LAYOUT_VERSION should have been bumped" error path.
--
-- No heal: every row that exists today is stamped layout_version 2 and is
-- rejected at load anyway (the reroll the version bump asks for), so the
-- default of 0 is never read into a live plan.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_runs_gameplay.sql states: the updater applies each SQL
-- file exactly once and remembers it by hash, so touching the base file would
-- leave every existing database without this column while claiming to be up
-- to date. MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on
-- information_schema like mod_pdungeon_account_difficulty.sql, which also
-- makes this file safe to re-apply by hand.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_account'
               AND COLUMN_NAME = 'gen_branches');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `gen_branches` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `gen_loop_pct`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
```

Verify it read-only against the live characters DB (the column must not exist yet, and the base file must be the one that owns `gen_loop_pct`):

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_characters -N -B -e "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='acore_characters' AND TABLE_NAME='pdungeon_account' AND COLUMN_NAME IN ('gen_loop_pct','gen_branches')"
```

Expected: exactly one row, `gen_loop_pct`.

- [ ] **Step 2: Config struct and key**

`src/PDv2Mgr.h`, in `PDv2Config` after `int loopChancePct = 15;`:

```cpp
        int         loopChancePct = 15;  // Round B: chance that a pocket carries a shortcut
        int         branches = 2;        // Round B: pocket rooms per layout (V2.Branches)
```

`src/PDv2Mgr.cpp`, in `LoadConfig` after the `loopChancePct` line:

```cpp
        // Round B: pocket rooms hanging off the spine. Clamped at 0 on the way
        // in; the planner's own arithmetic bounds it from above.
        _config.branches = std::max(0, sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.Branches", 2));
```

and extend the boot line so the ritual can read it:

```cpp
        LOG_INFO(PD_LOG, "PDv2: {} map {} floorZ {} rooms {}+{} field {} origin ({},{}) pockets {} shortcut {}%",
                 _config.enabled ? "enabled" : "disabled", _config.mapId, _config.floorZ,
                 _config.rooms, _config.bossRooms, _config.fieldBlocks,
                 _config.originBX, _config.originBY, _config.branches, _config.loopChancePct);
```

In `GeneratePlan`, after `cfg.loopChancePct = _config.loopChancePct;`:

```cpp
        cfg.branches = _config.branches;
```

- [ ] **Step 3: Persistence**

`SavePlanToDB` — the statement becomes:

```cpp
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, theme, layout_seed, layout_version, "
            "gen_rooms, gen_boss_rooms, gen_field_blocks, gen_origin_bx, gen_origin_by, "
            "gen_loop_pct, gen_branches, cfg_rooms, cfg_difficulty, cfg_caster_pct, cfg_mob_level_min, "
            "cfg_packs) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}') "
            "ON DUPLICATE KEY UPDATE theme = VALUES(theme), "
            "layout_seed = VALUES(layout_seed), layout_version = VALUES(layout_version), "
            "gen_rooms = VALUES(gen_rooms), gen_boss_rooms = VALUES(gen_boss_rooms), "
            "gen_field_blocks = VALUES(gen_field_blocks), gen_origin_bx = VALUES(gen_origin_bx), "
            "gen_origin_by = VALUES(gen_origin_by), gen_loop_pct = VALUES(gen_loop_pct), "
            "gen_branches = VALUES(gen_branches)",
            accountId, cfg.theme, cfg.seed, PD_LAYOUT_VERSION, cfg.rooms, cfg.bossRooms,
            cfg.fieldBlocks, cfg.originBX, cfg.originBY, cfg.loopChancePct, cfg.branches,
            state.cfgRooms, state.cfgDifficulty, state.cfgCasterPct, state.cfgBandMin, packs);
```

`LoadPlanFromDB` — the query and the read:

```cpp
        QueryResult result = CharacterDatabase.Query(
            "SELECT layout_seed, layout_version, theme, gen_rooms, gen_boss_rooms, "
            "gen_field_blocks, gen_origin_bx, gen_origin_by, gen_loop_pct, gen_branches "
            "FROM pdungeon_account WHERE accountId = {}", accountId);
```

```cpp
        cfg.loopChancePct = fields[8].Get<uint8>();
        cfg.branches = fields[9].Get<uint8>();
```

- [ ] **Step 4: The info line**

`src/PDv2Commands.cpp`, `HandleV2InfoCommand`:

```cpp
        handler->PSendSysMessage("pdungeon v2: {} | map {} | floorZ {:.2f} | rooms {}+{} | "
                                 "field {} blocks | origin ({},{}) | pockets {} | shortcut {}%",
                                 cfg.enabled ? "enabled" : "disabled", cfg.mapId, cfg.floorZ,
                                 cfg.rooms, cfg.bossRooms, cfg.fieldBlocks,
                                 cfg.originBX, cfg.originBY, cfg.branches, cfg.loopChancePct);
```

- [ ] **Step 5: The conf.dist**

Replace the `LoopChance` block (`conf/mod_procedural_dungeon.conf.dist` ~298–304) with:

```
#
#    ProceduralDungeon.V2.LoopChance
#        Description: Round B: percent chance that a pocket room, instead of
#                     dead-ending, carries a shortcut forward to a later room
#                     of the same segment - so the way out of a cleared pocket
#                     is not the way in. Never onto or past a boss room.
#                     Stored per account as gen_loop_pct with the layout.
#        Default:     15
#

ProceduralDungeon.V2.LoopChance = 15

#
#    ProceduralDungeon.V2.Branches
#        Description: Round B: pocket rooms hanging off the main path, at most
#                     one per ordinary spine room and never more than a third
#                     of the rooms. Pockets come out of the same room budget,
#                     so the total room count does not change. Stored per
#                     account as gen_branches with the layout.
#        Default:     2
#

ProceduralDungeon.V2.Branches = 2
```

- [ ] **Step 6: Compile the worldserver**

From **PowerShell**:

```powershell
cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m
```

Expected: `worldserver.vcxproj -> C:\wowstuff\dcore_bin\bin\RelWithDebInfo\worldserver.exe`, exit 0, no new warnings in `mod-procedural-dungeon` files. (No new `.cpp` was added, so no re-configure.) This takes minutes; do not run it in the background and forget it.

- [ ] **Step 7: Code style, commit**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && python "C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk/apps/codestyle/codestyle-cpp.py"
git add src/PDv2Mgr.h src/PDv2Mgr.cpp src/PDv2Commands.cpp conf/mod_procedural_dungeon.conf.dist data/sql/db-characters/mod_pdungeon_account_branches.sql
git -c core.autocrlf=false commit -m "feat(v2): V2.Branches config key, gen_branches persistence, info line"
```

---

### Task 5: Docs, full gates, oracle parity

**Files:**
- Modify: `CLAUDE.md` (the `src/generator/PDBlockPlan.*` row), `README.md:7-9`
- Test: all gates + the oracle + the DLL parity leg

- [ ] **Step 1: Module docs**

`CLAUDE.md`, the `src/generator/PDBlockPlan.*` row — replace its text with:

```
| `src/generator/PDBlockPlan.*` | **PDv2 block planner**: decides which kit block sits at which block coordinate and emits the `FLPD2` manifest the client composes from. Engine-free like the rest of `src/generator/`. Rooms are single blocks (66.67 yd). **Since Round B (2026-09-02) the layout is a chain**: one path from the entrance through the boss rooms (boss *k* at `round(k·(L−1)/N)`), one-room pockets hanging off ordinary spine rooms, an occasional segment-local shortcut (`V2.LoopChance`), then the Phase-2 chest stubs; `chainIndex` / `branchOf` / `shortcutTo` on `PlacedBlock` are what B1/B3/B4 read. Every boss room is a cut of the block graph (validator + harness). Design: `docs/superpowers/specs/2026-09-02-pdv2-b0-spine-generator-design.md` |
```

`README.md`, after the paragraph that ends `tile grid → decoration.` (line 9), add:

```
> **PDv2 (map 760, the client-composed kit) plans differently since Round B:** one chain of
> rooms from the entrance through the boss rooms, a few one-room pockets and an occasional
> shortcut forward - see `docs/superpowers/specs/2026-09-02-pdv2-b0-spine-generator-design.md`.
> The v1 pipeline below still describes the GameObject-assembled prototype.
```

- [ ] **Step 2: Fresh build, all gates**

Build `pdblock.exe` (PowerShell), prove freshness, then:

```bash
./pdblock.exe --batch 500 > gate_batch.txt; tail -8 gate_batch.txt
./pdblock.exe --decor-batch 3000 > gate_decor.txt; tail -4 gate_decor.txt
./pdblock.exe --roomcap 3000 > gate_roomcap.txt; tail -4 gate_roomcap.txt
```

Expected: `ALL CHECKS PASS` (batch, with the `214 walk mask(s)` line present earlier in the file), `0 failure(s)` (decor), `the encoded cap holds for both themes` (roomcap). Paste the tails into the task report.

- [ ] **Step 3: One real manifest through the oracle and the DLL**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && ./pdblock.exe --manifest 297397130 C:\wowstuff\ForgottenLand2.0\output\pd_live_manifest.txt 13 256 256 2 2
```

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/49_pd_compose_blocks.py --manifest C:\wowstuff\ForgottenLand2.0\output\pd_live_manifest.txt
```

Expected: `ALL PASS` from the oracle.

```bash
"/c/Users/Anwender/Documents/GitHub/fl-stream-client/build/Release/flstream_tests.exe"
```

Expected: the line `composer vs the Python oracle` **and both** `byte-identical to the oracle` lines, `497` tests, `0` failures. The suite's exit code alone is not the criterion (the parity leg vanishes silently when the manifest file is absent).

- [ ] **Step 4: Commit**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && git add CLAUDE.md README.md && git -c core.autocrlf=false commit -m "docs: chain planner in CLAUDE.md and README"
```

---

### Task 6: Stage the worldserver, deployed conf, operator checkpoint, vault sync

**Files:**
- Modify: `C:\wowstuff\dcore\configs\modules\mod_procedural_dungeon.conf` (one key)
- Create: `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde28.md`
- Modify (share-public, branch `main`): `docs/World of Warcraft/12-server-todo.md` (the PDv2 Round A/B row), `docs/World of Warcraft/procedural-dungeon/02-pdv2-session-resume.md` (state line), `docs/World of Warcraft/forgotten-land/15-host-migration-log.md` (MIG-017 scope), `claude_log.md` (**append at the very END**)

- [ ] **Step 1: Build and stage the worldserver**

From PowerShell (no new `.cpp`, plain build):

```powershell
cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m
certutil -hashfile C:\wowstuff\dcore_bin\bin\RelWithDebInfo\worldserver.exe MD5
```

Then check whether the operator's server is running:

```bash
tasklist //FI "IMAGENAME eq worldserver.exe"
```

If it is **not** running: back up and install —

```bash
cp /c/wowstuff/dcore/worldserver.exe /c/wowstuff/dcore/worldserver.exe.pre_roundB_20260902 && cp /c/wowstuff/dcore_bin/bin/RelWithDebInfo/worldserver.exe /c/wowstuff/dcore/worldserver.exe && certutil -hashfile C:\wowstuff\dcore\worldserver.exe MD5
```

If it **is** running (the normal case): leave `C:\wowstuff\dcore\worldserver.exe` alone, record the staged md5, and the operator document's start section carries stop → copy → start.

- [ ] **Step 2: The deployed conf key**

In `C:\wowstuff\dcore\configs\modules\mod_procedural_dungeon.conf`, directly after `ProceduralDungeon.V2.LoopChance = 15`, add:

```
# Round B (2026-09-02): pocket rooms hanging off the main path; LoopChance is
# now the chance that a pocket carries a shortcut forward. See the conf.dist.
ProceduralDungeon.V2.Branches = 2
```

Verify: `grep -n 'V2.Branches\|V2.LoopChance' /c/wowstuff/dcore/configs/modules/mod_procedural_dungeon.conf` shows both keys once.

- [ ] **Step 3: The operator checkpoint document**

Create `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde28.md` in the shape of `pd_testlauf_runde27.md` (read it first, copy its structure; German prose like its predecessors, English identifiers), with these sections:

1. **Stand** table: kit `t1b-v35` (unchanged) · patch-9 `97fc5cd5` (unchanged) · `FLStream.dll` unchanged · SQL `kitVersion` 23 (unchanged) · worldserver: staged `dcore_bin\bin\RelWithDebInfo\worldserver.exe` md5 `<from Step 1>`, running `5f4bbbd1` until the swap · `PD_LAYOUT_VERSION` 3 · new characters-DB column `gen_branches` (applied by the updater on the next start) · conf key `V2.Branches = 2`.
2. **Start** sequence: stop the worldserver (nobody online) → `copy dcore_bin\bin\RelWithDebInfo\worldserver.exe dcore\worldserver.exe` (backup first as `worldserver.exe.pre_roundB_20260902`) → start → in the boot log the line `PDv2: enabled map 760 ... pockets 2 shortcut 15%` and `214 walk mask(s)` → in game `.pdungeon v2 info` shows `pockets 2 | shortcut 15%` and `214 walk mask(s) loaded`.
3. **B0 — one path**: `.pdungeon v2 gen` (any seed), then `.pdungeon v2 enter`. *Erwartet:* the HUD map and the walk read as one path from the entrance to the boss with one or two side pockets; every boss room has exactly one corridor into it (plus at most a chest stub); a pocket with a shortcut leads forward, not back. *Falls doch* a loop or a second way into a boss room is seen: note the seed (`.pdungeon v2 info` prints it) — the harness reproduces it with `pdblock <seed> <rooms>`.
4. **Regression probe**: `.pdungeon v2 gen 0 1` (mine theme, 1 room) still generates and enters.
5. **Offen**: B1–B4 (altars, pads, barriers, patrols) follow on the same branch; the kit is untouched by B0.
6. **Rollback** table: worldserver back to `worldserver.exe.pre_roundB_20260902` (rows stamped `layout_version 3` are then "reroll needed", nothing else breaks); the column stays (harmless); the conf key can stay (unknown to the old build).

- [ ] **Step 4: Vault sync (share-public, `main`)**

In `docs/World of Warcraft/12-server-todo.md`, the row `**PDv2 Round A + kit rounds - ACCEPTED 2026-09-02; host owed (deferred)**`: append to its text — `**Round B started 2026-09-02 on `claude/pdv2-round-b-0cf92ad4`: B0 spine generator T1 (harness green, cap re-measured, oracle + DLL parity), worldserver staged, operator checkpoint `tools/pd_testlauf_runde28.md` (optional look); next B1–B4.**` and set its Owed to `T1 (B0) -> T2`.

In `docs/World of Warcraft/procedural-dungeon/02-pdv2-session-resume.md`, the `Block planner (`PDBlockPlan`)` tier row: `**T1** — chain generator since Round B (2026-09-02), pdblock --batch 500 green, spec in the module repo`.

In `docs/World of Warcraft/forgotten-land/15-host-migration-log.md`, MIG-017: add one bullet to its scope — `Round B / B0 (2026-09-02): module commits on `claude/pdv2-round-b-0cf92ad4` (chain generator, `gen_branches` column via `mod_pdungeon_account_branches.sql`, `PD_LAYOUT_VERSION` 3), conf key `ProceduralDungeon.V2.Branches = 2`. Same pull, same restart.` Run `python python_scripts/build_host_runbook.py --check` from the share-public root; expected `0 errors`.

Append at the **very END** of `claude_log.md`:

```markdown

## 2026-09-02 — PDv2 Round B / B0: the chain generator (session 0cf92ad4)

- **Repo**: mod-procedural-dungeon (branch `claude/pdv2-round-b-0cf92ad4`), share-public main
- **Problem**: the block planner scattered rooms and connected them by an MST plus loop edges, which reads as a labyrinth; Round B wants one path through the boss rooms with pockets.
- **Changes**: `ChainRooms` (backtracking chain walk, free-route rule) replaces scatter + MST; pockets (`V2.Branches`, persisted as `gen_branches`) with segment-local shortcuts (`LoopChance` repurposed); bosses at `round(k·(L−1)/N)`; `chainIndex`/`branchOf`/`shortcutTo` on `PlacedBlock`; validator proves the boss cut property; `PD_LAYOUT_VERSION` 3; harness: chain checks, chain pin, `--path` chain view, engine-field sweep, room cap re-measured (<value>); pins re-captured.
- **Evidence**: T1 — `pdblock --batch 500` / `--decor-batch 3000` / `--roomcap 3000` green on the fresh binary, oracle ALL PASS, `flstream_tests` 497/0 with both byte-identical lines; worldserver built, staged (md5 `<value>`), not swapped (operator's process running). T2 owed: operator look per `tools/pd_testlauf_runde28.md`.
- **Affected files**: `src/generator/PDBlockPlan.{h,cpp}`, `src/generator/PDv2GameMath.h`, `src/PDv2Mgr.{h,cpp}`, `src/PDv2Commands.cpp`, `conf/mod_procedural_dungeon.conf.dist`, `data/sql/db-characters/mod_pdungeon_account_branches.sql`, `tests/blockplan_harness.cpp`, `CLAUDE.md`, `README.md`, `docs/superpowers/{specs,plans}/2026-09-02-*`
- **Commits**: `<list the module hashes>`
```

Commit share-public on `main`:

```bash
cd /c/Users/Anwender/Documents/GitHub/share-public && git add "docs/World of Warcraft/12-server-todo.md" "docs/World of Warcraft/procedural-dungeon/02-pdv2-session-resume.md" "docs/World of Warcraft/forgotten-land/15-host-migration-log.md" claude_log.md && git -c core.autocrlf=false commit -m "docs(pdv2): Round B B0 chain generator - T1 done, worldserver staged, operator checkpoint 28"
```

- [ ] **Step 5: Report**

The task report lists: staged md5, whether the swap happened, the exact gate tails, the room cap value, the three `--path` outputs, and the share-public commit hash. Do **not** push either repo; pushing is the session's call.

---

## Self-review (done while writing)

- **Spec coverage:** §2 arithmetic → Task 1; §3/§4 chain, pockets, shortcuts, stubs, roles → Task 2; §5 data model, validator, cut property, version 3 → Tasks 1–2; §5 config/persistence/info → Task 4; §6 views → Task 3; §7.1 invariants → Task 2; §7.2 non-vacuity → Task 2 (`sawPocket`, `sawShortcut`); §7.3 sweep → Task 3; §7.4 cap → Task 3; §7.5 pins → Tasks 2–3; §8 acceptance → Tasks 5–6; §10 files → all.
- **Types:** `PocketCountFor(int,int,int)`, `BossChainIndex(int,int,int)`, `ChainLength(BlockPlan const&)`, `SegmentOf(BlockPlan const&, PlacedBlock const&)` are used with those signatures in Tasks 2–3; `PlacedBlock::chainIndex/branchOf/shortcutTo` and `BlockCfg::branches` are the names used everywhere; the SQL column is `gen_branches` in the file, the INSERT and the SELECT.
- **Placeholders:** the pin values are captured by running (the documented procedure), the md5 and hashes are filled at execution time; no other blanks.
