# PDv2 Round E / WP3 — the account-wide run cap, one rooms number, one debug switch, the layout preview — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. One Opus agent per task, small and targeted; the planner reviews between tasks (no review loops, Sonnet for the short reviews).

**Goal:** The difficulty dial is capped per account and the cap grows with completed runs (+3 with a death, +5 without); the panel's rooms slider and the HUD's rooms counter mean the same thing; all per-creature/per-tick logging sits behind `ProceduralDungeon.V2.Debug`; the gen panel draws the planned layout before the player enters.

**Architecture:** R1 adds one column to `pdungeon_account` and one to `pdungeon_runs`, a `deaths` field on the run, the cap raise in `FinishRun`, and the cap as the `diffMax` bound the panel already receives. R2 changes the planner's chain arithmetic (entrance on top of the wanted rooms), bumps `PD_LAYOUT_VERSION`, and makes both counters count non-boss rooms. R3 is a config gate around the noisy sites. R4 is Lua-only: the HUD's `BuildMap` gains a target and the panel gets a canvas.

**Tech Stack:** C++17 module, MySQL 8 characters SQL (information_schema-guarded ALTERs), `pdblock` harness, AIO Lua 5.1 addon (`lua_scripts/flpdui.lua`, TAB indentation).

**Spec:** `docs/superpowers/specs/2026-09-10-pdv2-round-e-loot-talents-design.md` §1.2, §2 D13/D15, §3 R1–R4.

---

## Global Constraints

- Same branch as WP1 (`claude/pdv2-round-e-<sessionId>`), after WP1's tasks; Conventional Commits; the WP1 gates (codestyle, harness ×3, staged build + install + boot diff) on every `src/` task. `PD_LAYOUT_VERSION` bump ⇒ every stored layout regenerates on next login (accepted, the server is not live).
- The panel is **display only** (`flpdui.lua` header rule): every bound comes from the `C` payload; no formula, clamp or `or <number>` fallback in Lua.
- `.dist` = code default for every new key. Deploy the Lua to `C:\wowstuff\dcore\lua_scripts\ProcDungeon\flpdui.lua` (mod-ale, no auto-sync) and tell the operator `/reload`.

---

## File Structure

**Created:** `data/sql/db-characters/mod_pdungeon_account_diffcap.sql`, `data/sql/db-characters/mod_pdungeon_runs_deaths.sql`.
**Modified:** `src/PDv2Mgr.h/.cpp` (account state + cap persistence + `V2.Debug` + `V2.Cap.*`), `src/PDv2InstanceScript.h/.cpp` (deaths, cap raise, counters), `src/PDv2UILink.cpp` (`diffMax`, `curRooms`), `src/PDv2Commands.cpp` (`cap`), `src/generator/PDBlockPlan.cpp` (chain total), `src/generator/PDv2GameMath.h` (nothing unless the roomcap measurement forces `PD_GAME_ROOMS_CAP_MEASURED` down), `tests/blockplan_harness.cpp` (pins), `src/PDv2CreatureAI.cpp`, `src/PDClientLink.cpp`, `src/PDv2UILink.cpp` (debug gates), `conf/mod_procedural_dungeon.conf.dist`, `lua_scripts/flpdui.lua`, `CLAUDE.md`, `README.md`.

---

### Task 1: R1 persistence — `diff_cap` on the account, `deaths` on the run history

**Files:**
- Create: `data/sql/db-characters/mod_pdungeon_account_diffcap.sql`, `data/sql/db-characters/mod_pdungeon_runs_deaths.sql`
- Modify: `src/PDv2Mgr.h` (`PDv2AccountState`, `PDv2Config`), `src/PDv2Mgr.cpp` (`LoadAccountState :262-307`, new `RaiseDiffCap`, `LoadConfig`)

**Interfaces:**
- Produces:
  ```cpp
  // PDv2AccountState
  int diffCap = PD_GAME_DIFF_MIN;                       // pdungeon_account.diff_cap, clamped [1, 100] on load
  // PDv2Config
  int capDeathUnlock = 3;  int capCleanUnlock = 5;      // V2.Cap.DeathUnlock / V2.Cap.CleanUnlock
  // PDv2Mgr
  int  RaiseDiffCap(uint32_t accountId, int wanted);    // clamps to [1,100], never lowers, persists "UPDATE pdungeon_account SET diff_cap = GREATEST(diff_cap, {}) WHERE accountId = {}" (INSERT ... ON DUPLICATE KEY like SaveAccountCfg when no row), returns the cap now in force
  ```
  SQL: `diff_cap TINYINT UNSIGNED NOT NULL DEFAULT 1` (account), `deaths TINYINT UNSIGNED NOT NULL DEFAULT 0` (runs) — both the `mod_pdungeon_runs_difficulty.sql` shape: `SET @dbname`, information_schema guard, `PREPARE/EXECUTE`, **no `AFTER` clause** (the 2026-08-09 host lesson in that file's header).

- [ ] **Step 1:** The two SQL files with the house header (why a separate file, why no AFTER, why default 1/0).
- [ ] **Step 2:** `LoadAccountState`: add `diff_cap` to the SELECT (column 7), `state.diffCap = GameClampDiff(fields[7].Get<uint8>())`; `SetAccountCfg`: `state.cfgDifficulty = std::min(GameClampDiff(cfg.cfgDifficulty), state.diffCap)`; `RaiseDiffCap` as above (cache under `_lock`, then the async `Execute`).
- [ ] **Step 3:** `LoadConfig`: the two keys, clamped 0..100; `.conf.dist` blocks.
- [ ] **Step 4:** Build (SQL applies on the staged restart); commit `feat(cap): account-wide difficulty cap column, run deaths column, RaiseDiffCap`.

---

### Task 2: R1 gameplay — count deaths, raise the cap, bound the dial

**Files:**
- Modify: `src/PDv2InstanceScript.h` (`PDv2RunState::deaths`), `src/PDv2InstanceScript.cpp` (`OnUnitDeath :3295-3312`, `FinishRun :758-829`), `src/PDv2UILink.cpp` (`SendCfg :443-445`, `SendRunTick` untouched), `src/PDv2Commands.cpp` (`.pdungeon v2 cap`)

**Interfaces:**
- Consumes: `sPDv2Mgr->RaiseDiffCap`, `sPDv2UILink->SendNotice(player, text)` (the `N` kind), `SendCfg(player)`, `PDv2AccountState::diffCap`.
- Produces: `uint8 deaths` in `PDv2RunState` (saturating at 255); `FinishRun`: after `GrantRunReward`, `int const unlock = _run.deaths ? cfg.capDeathUnlock : cfg.capCleanUnlock; int const before = account.diffCap; int const now = sPDv2Mgr->RaiseDiffCap(_accountId, int(_run.difficulty) + unlock);` then for every run player `SendNotice("Difficulty {} unlocked ({} death(s))", now, deaths)` **only when `now > before`**, and `SendCfg(player)` so the slider's max moves; the history INSERT gains `deaths`. `SendCfg`: the `PD_GAME_DIFF_MAX` field becomes `account.diffCap` (the wire field is `diffMax`; `c.diffMax` already bounds the slider at `flpdui.lua:431`). GM command `.pdungeon v2 cap <n>` (`RBAC_PERM_COMMAND_MODIFY`, the `gen/enter/info/patrol` table at `PDv2Commands.cpp:50-57`): sets the invoking account's cap to `GameClampDiff(n)` (may lower — it is a test tool; say so in its help string) and echoes `SendCfg`.

- [ ] **Step 1:** `OnUnitDeath`: `if (_run.deaths < 255) ++_run.deaths; MarkRunDirty();` (players only, as today).
- [ ] **Step 2:** `FinishRun` changes + the INSERT column; the notice text in the raid-warning voice of the finale lines.
- [ ] **Step 3:** `SendCfg` bound; `.pdungeon v2 cap`. Also `LoadAccountState` already clamps `cfgDifficulty` — add `state.cfgDifficulty = std::min(state.cfgDifficulty, state.diffCap)` there too, so a hand-edited row cannot exceed the cap.
- [ ] **Step 4:** Build, staged boot, `.pdungeon v2 cap 30` → `/pd` slider max 30; `.pdungeon v2 info` prints `cap N`. Codestyle. Commit `feat(cap): deaths per run, cap raised on completion, dial bounded by the cap`.

---

### Task 3: R2 — the entrance stops eating a wanted room; both counters count ordinary rooms

**Files:**
- Modify: `src/generator/PDBlockPlan.cpp` (`PocketCountFor :886-895`, the plan builder `:1636-1648`), `src/PDv2Mgr.h` (`PD_LAYOUT_VERSION 3 → 4`, `:226`), `src/PDv2InstanceScript.cpp` (`SpawnFromPlan :1554-1559/:1670`, `OnMobDied :707-713`), `src/PDv2UILink.cpp` (`SendCfg :391-406`), `tests/blockplan_harness.cpp`, `src/generator/PDv2GameMath.h` (only if the roomcap forces it)

**Interfaces:**
- Produces: planner `total = max(2, rooms + bossRooms + 1)` in **both** places (the pocket budget and the builder; `chainLen = total − pockets`, entrance = chain 0 as before). `roomsTotal` = count of room blocks with `role != RoomEntrance && role != RoomBoss`; `roomsCleared` increments only when the cleared room is `!_roomIsBoss[roomIndex]`; `curRooms` in `SendCfg` uses the same rule (boss rooms excluded, `curBoss` unchanged). `BlockCfg::rooms` comment updated: "ordinary rooms; the entrance and the boss rooms come on top".

- [ ] **Step 1:** Planner arithmetic + comment; `PD_LAYOUT_VERSION = 4`.
- [ ] **Step 2:** Harness pins (batch block): for 200 seeds and `rooms ∈ {1, 5, 14}`, `bossRooms ∈ {1, 2}`: `count(role == Room) == rooms + pockets + loopRooms`, `count(RoomEntrance) == 1`, `count(RoomBoss) == bossRooms`; re-run `pdblock --roomcap 3000` and read the table: if the max manifest at `rooms = 15, bossRooms = 2` exceeds `PD_GAME_MANIFEST_BUDGET_B`, set `PD_GAME_ROOMS_CAP_MEASURED = 14` and say so in the commit message with the measured bytes.
- [ ] **Step 3:** Engine counters + `SendCfg`; the spec's `roomFactorX100` (WP1 Task 6) now reads `_run.roomsTotal` directly — replace the explicit count there.
- [ ] **Step 4:** `pdblock --manifest 297397130 out.txt` then `python scripts\49_pd_compose_blocks.py --manifest out.txt` in the workspace (oracle unchanged: the manifest format did not move, only the block count for a given cfg). Build, staged boot, `/pd`: slider 14 → after gen "Current depths: 14 rooms - 2 boss" (+ pockets/loops), HUD `0/14 rooms`. Commit `feat(rooms): the slider counts ordinary rooms; HUD and panel agree (layout v4)`.

---

### Task 4: R3 — `ProceduralDungeon.V2.Debug`

**Files:**
- Modify: `src/PDv2Mgr.h/.cpp` (`bool debug`, `inline bool PDv2Debug()`), `conf/mod_procedural_dungeon.conf.dist`, `src/PDv2InstanceScript.cpp` (`:491`, `:1515`, `:3310`, `:3422`), `src/PDv2CreatureAI.cpp` (`:726`, `:845`, `:1753`), `src/PDClientLink.cpp` (`:172`), `src/PDv2UILink.cpp` (`:789`, `:798`, `:826`, `:834`, `:915`, `:925`)

**Interfaces:**
- Produces: `PDv2Config::debug = false` from `ProceduralDungeon.V2.Debug` (read live); `inline bool PDv2Debug() { return sPDv2Mgr->GetConfig().debug; }` in `PDv2Mgr.h`; every listed site becomes `if (PDv2Debug()) LOG_INFO(PD_LOG, …)` (INFO under the gate, so an operator who turns it on sees it without touching the logger config); the two patrol re-plan `LOG_WARN`s become debug lines with their text unchanged. Untouched: startup/summary INFO lines, `FinishRun`, WARN/ERROR that name a data fault (missing chest anchor, unknown pack member), everything under `PatrolDebug()`.

- [ ] **Step 1:** Conf field + key + `.dist` block ("what it prints, and that it is per creature/per tick, so off everywhere but a watched run").
- [ ] **Step 2:** The sweep; grep afterwards: `grep -n "LOG_DEBUG" src/*.cpp` must list none of the sites above (they are INFO-under-gate now).
- [ ] **Step 3:** Build, staged boot with `Debug = 0`: one run of a 5-room dungeon writes no per-spawn line (count `PDv2:` lines in the server log during a run: expect only spawn summary, barrier/finale notices, `FinishRun`). Codestyle. Commit `feat(conf): V2.Debug gates every per-creature and per-tick line`.

---

### Task 5: R4 — the layout preview in the gen panel (Lua only)

**Files:**
- Modify: `lua_scripts/flpdui.lua` (`BuildMap :586-637`, `PlaceDot :638-656`, the panel builder `:225-385`, the `M`/`K` dispatch `:825-836`, `LayoutBandRow :391-402`), the deployed copy

**Interfaces:**
- Consumes: the `M` payload (`ParseMap :151-167`) and `K` (`ParseCleared :199-210`) the server already sends after `UI GEN` and at HELLO.
- Produces: `BuildMap(m, target)` where `target = { canvas = <Frame>, pool = <table>, size = <px> }`; the HUD keeps its target; the panel gets `previewCanvas` (160 × 160, below `affixLine`, above `sep2`), `PANEL_H` + 170, `LayoutBandRow` re-anchors through it; the panel canvas shows the last `M` (cleared rooms from `K` too — outside a run the set is empty). `PlaceDot` stays HUD-only.

- [ ] **Step 1:** Refactor `BuildMap` to take the target (no behaviour change for the HUD; the rect pool is per target).
- [ ] **Step 2:** Panel canvas + anchors + height; on `M`/`K`: rebuild both targets; on `C` with no plan (`c.curRooms == 0`) hide the canvas.
- [ ] **Step 3:** Deploy the Lua to `C:\wowstuff\dcore\lua_scripts\ProcDungeon\flpdui.lua`; `/reload`; `/pd` → Generate → the map appears in the panel; Enter → the HUD map matches. Commit `feat(ui): layout preview in the gen panel (M/K already on the wire)`.

---

### Task 6: Docs and the runde31 section

**Files:**
- Modify: `CLAUDE.md`, `README.md` (cap rule, rooms semantic, `V2.Debug`, preview), vault `12-server-todo.md` (WP3 T1), `claude_log.md` (END); `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde31.md` (WP3 section: cap 1 → complete a run without dying → cap 6; `.pdungeon v2 cap 60` then a death-run at 60 → cap 63; slider 14 → HUD 0/14; log quiet; preview canvas)

- [ ] **Step 1:** Docs + checklist; commit `docs(round-e): WP3 cap, rooms, debug, preview - CLAUDE/README, queue, runde31`.
