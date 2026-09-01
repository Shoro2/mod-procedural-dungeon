# PDv2 Round A — "the look" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the procedurally generated dungeon look like a dressed, inhabited ruin instead of a
grey grid — a floor without hard cell borders, clutter in corners and on open ground, ambient
critters, and rooms populated by one coherent faction of undead or demons.

**Architecture:** Two independent tracks that share only the final deploy. The **server track**
(Tasks 2–13) is C++ in the engine-free `src/generator/` layer plus world SQL, and is covered by the
offline `pdblock` harness. The **client track** (Tasks 14–17) is three Python scripts that bake the
ADT kit the client streams; its test is the kit's own verify gate plus three-way byte parity against
the Python oracle and the DLL. Nothing in Round A changes the layout, the manifest format or
`KIT_VERSION`.

**Tech Stack:** C++17 (MSVC 19.44, `cl.exe`; **g++ does not exist on this box**), AzerothCore
3.3.5a module, MySQL 8.4 world DB `acore_world`, Python 3 pipeline scripts, a client-side C++ DLL
(`FLStream.dll`, **not touched by this round**).

**Spec:** `docs/superpowers/specs/2026-09-01-pdv2-content-expansion-design.md` §2 (Round A). Read
§1 "Ground truth" before the first line of code.

---

## Global Constraints

Every task's requirements implicitly include all of these.

- **The live checkout is `C:\Users\Anwender\Documents\GitHub\azerothcore-wotlk\modules\mod-procedural-dungeon`.** `C:\Users\Anwender\Documents\GitHub\mod-procedural-dungeon` is a stale v1 clone with no PDv2 sources; editing it loses the work silently and without an error.
- **Branch:** `claude/pdv2-expansion-9583c476` (already created, spec committed at `ff39f44`).
- **Conventional Commits. English in code, comments, docs and commit messages.** Commit at the end of every task.
- **No `std::random` distribution, no `unordered_map` iteration, no `std::shuffle` in `src/generator/`.** Determinism must hold across MSVC and gcc — the Ubuntu host builds the same source.
- **`PDRandom::UniformInt(lo, hi)` returns `lo` WITHOUT drawing when `lo >= hi`** (`src/generator/PDRandom.h:43-46`). A rule with `minPerBlock == maxPerBlock` costs zero draws, and a one-candidate pool costs zero draws. Never reason about stream position without this.
- **Generated files are never hand-edited:** `data/sql/db-world/mod_pdungeon_chunk_meta.sql` (written by `48_gen_t1_blockkit.py`) and `mod_pdungeon_prop_displays.sql` (written by `57_pd_prop_displays.py`).
- **SQL is shipped as NEW files, never as edits to existing ones.** The updater applies each file once by hash; every shipped file's idempotent `DELETE` covers only its own id range. `DELETE` by an explicit id list or a range you own — **never** `REPLACE`, **never** `DROP`.
- **Startup-only tables.** Chunk meta, decor rules and packs load once in `PDWorldScript::OnStartup`. Any SQL change needs a **worldserver restart**; `.reload config` silently does nothing.
- **Never edit `creature_template` 84263–84290** (owned by fl-underground-dungeon's live map 741) or spells 900050–900060 (owned by mod-dungeon-challenge).
- **Adding a NEW `.cpp` or `.h` to `src/` requires a `cmake` re-configure**, not just a build: AzerothCore globs module sources at configure time and this module ships no `CMakeLists.txt`. An incremental build silently does not ship the new file.
- **Git Bash mangles the build's `/m` flag into a path** (measured in Task 9: `/m` becomes `M:/` and the build fails for a reason that has nothing to do with the code). Prefix the command with `MSYS2_ARG_CONV_EXCL="*"`, or run it from PowerShell.
- **Do not add `/WX`.** `tests/blockplan_harness.cpp:263` emits C4456 today (shadowed `colon`); `/WX` would fail the build for a reason unrelated to Round A.
- **Reserved id ranges:** GameObject `910000-910099` — `910000-910033` allocated, `910040-910049` reserved for kit props by a harness assert (`tests/blockplan_harness.cpp:1724`) with only `910048`/`910049` free, **`910050-910099` free and verified empty in the live DB**. `creature_template` `910500-910549`.
- **World DB access:** `"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "<QUERY>"`
- **No task restarts the worldserver, and no task writes to the database.** Added 2026-09-01: the
  local `worldserver.exe` is running and is the operator's. Every SQL file this round ships is
  applied by the updater on the **next** start, which happens once, in Task 18's deploy. Tasks that
  ship SQL verify it **read-only** — that every id it references exists, that every id it claims is
  free really is free, and that its idempotency clause covers only its own range. Verification that
  genuinely needs a running server (a boot-log line, a row count after the updater ran, an
  `.pdungeon v2 info` line) is deferred to Task 18 and named there as owed.
- **Round A deliberately re-rolls every stored layout's decor and spawns.** Accepted in the spec: the server is not public and character progress is expendable. Do not add compatibility shims for it.

### Building and running the harness (used by almost every task)

Open a shell with the MSVC toolchain once per session:

```bash
cmd /k "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

A line `Der Befehl "vswhere.exe" ... konnte nicht gefunden werden.` is printed first and is harmless —
measured; `cl.exe` 19.44 initialises anyway.

**`cmd /k` is the interactive form and does not chain** (measured in Task 3). To compile from a
non-interactive shell, chain in one `cmd /c` instead, and run the built `pdblock.exe` as its own
command afterwards — chaining the run into the same invocation has been seen to report the
freshly-built exe as "not found":

```bash
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp'
```

Build `pdblock` with **today's true source set — four generator sources**, not the two the vault
documents nor the three `CLAUDE.md` documents:

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
```

Expected: the five source filenames echoed, `Generating Code...`, exit 0, and exactly one warning —
`blockplan_harness.cpp(263): warning C4456`.

**The `pdblock.exe` checked into the module root is stale (2026-08-30).** Rebuild before every gate
run or you are testing pre-merge code.

**A `cmd /c` build silently no-ops under the Bash tool** (measured in Task 4: bare banner, exit 0,
nothing built) — run it through PowerShell instead. That failure mode is dangerous precisely because
it looks like a pass: the previous binary runs and prints `ALL CHECKS PASS`. **Prove the binary is
newer than the sources before believing any gate:**

```bash
stat -c '%y %n' pdblock.exe src/generator/*.cpp tests/blockplan_harness.cpp | sort
```

`pdblock.exe` must sort last. A gate run against a stale binary is not evidence.

### The four offline gates, and their measured-green baseline (2026-09-01)

| Gate | Command | Baseline |
|---|---|---|
| Planner / walk grid / link state | `pdblock.exe --batch 500` | `214 walk mask(s)`, `largest manifest : 880 bytes`, `ALL CHECKS PASS` |
| Decor placement | `pdblock.exe --decor-batch 3000` | `props per layout : 6..51`, `27691 checks, 0 failure(s)` |
| Room cap vs manifest budget | `pdblock.exe --roomcap 3000` | `15 2 17 0 1470 ok`, cap 15 both themes |
| v1 generator | `pdgen.exe --batch 500` | `failures=0` |

`pdgen` needs a fourth, non-obvious source — `PDLayout.cpp`:

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdgen.exe tests\ascii_harness.cpp src\generator\PDDungeonGenerator.cpp src\generator\PDGridPath.cpp src\generator\PDWallPlan.cpp src\generator\PDLayout.cpp
```

**A gate that prints no `214 walk mask(s)` line is a weaker gate wearing a passing result** — the
walk-grid checks are skipped rather than faked when the kit metadata is missing
(`blockplan_harness.cpp:2073`).

---

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `data/sql/db-world/mod_pdungeon_decor_clutter.sql` | The `pdungeon_decor_rules` rows (id ≥ 4) that place the new clutter. **Rules only** — see the ordering note below. |
| `data/sql/db-world/mod_pdungeon_templates_fix.sql` | **Every** `gameobject_template` row in 910040-910099: the 8 kit props *and* the 22 clutter props. It exists because `mod_pdungeon_templates.sql` opens with a range `DELETE` over that whole block, and this is the only module file that sorts **after** it. See Task 7. |
| `data/sql/db-world/mod_pdungeon_critters.sql` | `pdungeon_critter_rules` schema + rows. |
| `data/sql/db-world/mod_pdungeon_packs_undead_demon.sql` | Packs 4 and 5 and their 24 members. |
| `data/sql/db-world/mod_pdungeon_member_spells_undead_demon.sql` | The 72 combat-kit rows for those 24 creatures. |
| `src/generator/PDv2PackDraw.h` / `.cpp` | The **pure** spawn draw, lifted out of `PDv2PackMgr` so it can be linked into the harness. Engine-free by the same rule as the rest of `src/generator/`. |

**Modified:**

| Path | Change |
|---|---|
| `src/generator/PDv2DecorPlan.h` | Two placement-kind constants, the budget constant, `CritterRule`/`CritterSpot`, `BuildCritterPlan`. |
| `src/generator/PDv2DecorPlan.cpp` | `CollectCorners`, `CollectScatter`, per-placement pools in `BuildDecorPlan`, the budget cut, `BlockRoleName` dead-end split, `BuildCritterPlan`. |
| `src/PDv2Mgr.h` / `.cpp` | `LoadCritterRules` + `_critterRules` + accessor; the decor loader's placement validation. |
| `src/PDWorldScript.cpp` | One `LoadCritterRules()` call in `OnStartup`. |
| `src/PDv2Commands.cpp` | One row-count line in `.pdungeon v2 info`. |
| `src/PDv2InstanceScript.h` / `.cpp` | `SpawnCritters` + `_critterGuids`. |
| `src/PDv2CreatureAI.cpp` | Critter exclusion in the AI binder. |
| `src/PDv2Scaling.cpp` | Critter exclusion in `IsDungeonCreature`. |
| `src/PDv2PackMgr.h` / `.cpp` | Delegate to `PDv2PackDraw`; the pack-per-room draw; the rewritten draw-order comment. |
| `tests/blockplan_harness.cpp` | Placement-aware `CheckDecorSpots`, theme-2 coverage, critter checks, pack-draw checks. |
| `C:\wowstuff\ForgottenLand2.0\scripts\51_texture_blockkit.py` | Alpha feather, corridor track, per-role texture ids, MCLY effect id. |
| `C:\wowstuff\ForgottenLand2.0\scripts\52_punch_kit_holes.py` | MCSH feather, MCNK header 0x40. |
| `C:\wowstuff\ForgottenLand2.0\scripts\48_gen_t1_blockkit.py` | Enlarged `TEXTURE_SUPERSET`, per-role layer table. |
| `C:\wowstuff\FL2-Client\FLStream.ini` | Back-filled ladder paragraphs v25–v28, then the v29 paragraph and the `KitDir` flip. |

---

## Task 1: Rollback net and measured baseline

Nothing in this round may overwrite client state until there is a way back. Today there is not:
kits **v25, v26, v27 and v28 have no ladder paragraph**, v28 additionally has **no git commit and no
operator report** despite differing from v27 in 215 of its 217 files, and the live
`patch-9.MPQ` (md5 `97fc5cd5`) has **no `pre_*` backup**.

**Files:**
- Modify: `C:\wowstuff\FL2-Client\FLStream.ini` (append four paragraphs to the ladder; the ladder is 320 lines of comment before the 4-line `[FLStream]` section, and its last entry today is v23/v24 at line 283)
- Create: `C:\wowstuff\ForgottenLand2.0\tools\pd_roundA_baseline.md`

**Interfaces:**
- Consumes: nothing.
- Produces: the four ladder paragraphs and the baseline file that Task 18's operator document and rollback table cite.

- [ ] **Step 1: Record the four missing kits' facts**

Run each and paste the output into the baseline file. Do not write a paragraph from memory — v28 has
no commit to read it from.

```bash
powershell -c "Get-ChildItem 'C:\wowstuff\FL2-Client\Data\FLPD-Kit' -Directory | Select-Object Name,LastWriteTime,@{n='Files';e={(Get-ChildItem $_.FullName -File).Count}} | Sort-Object Name"
```

Expected: 26 directories `t1b` .. `t1b-v28`, 217 files each from v20 on. The four this task covers:
v25 built 2026-09-01 02:41:56 (internal kitVersion 21), v26 03:20:58 (kitVersion **22** — this one
moved the SQL), v27 03:59:26 (kitVersion **23**), v28 11:22:28 (kitVersion 23, unchanged).

- [ ] **Step 2: Confirm each kit's matching commit, and that v28 has none**

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && git log --since="2026-09-01 02:00" --until="2026-09-01 13:00" --date=iso --pretty=format:"%h %ad %s"
```

Expected: `c3ebe24` 02:42:37 (75 s after v25), `1139876` 03:21:38 (40 s after v26), `fc872ae`
03:59:29 (3 s after v27, and its subject names kitVersion 23), then `6598a8a` and `0f27808`.
**Nothing at or after 11:22** — that is the v28 gap, and it must be written into the paragraph as an
honest "no commit; reconstructed by diffing v28 against v27".

- [ ] **Step 3: Append the four paragraphs to the FLStream.ini ladder**

Match the existing paragraph shape exactly (read lines 258–283 for the v23/v24 entries first). Each
ends with its `kitVersion`, its worldserver relationship and its rollback target:

```
; v25 (round 18, 2026-09-01 02:41) - halve the terrain crest 12 -> 6 yd, decouple the
;   facade inset band from it. Commit c3ebe24. patch-9 in force: pre_ambience_20260901
;   (md5 b7eb4a22); kitVersion 21; same worldserver; SQL applies on restart.
;   Rollback: v24.
; v26 (round 19, 2026-09-01 03:20) - corner seats cap the convex terrain noses; the
;   small-pad house is centred. Commit 1139876; kitVersion 22 - the SQL MOVED, so a
;   worldserver restart applies it; same worldserver binary. Rollback: v25.
; v27 (round 20, 2026-09-01 03:59) - arc-laid street cobbles, per-model sinkZ, tower
;   sunk 6 yd. Commit fc872ae; kitVersion 23 - this is the chunk-meta still live in the
;   world DB; same worldserver; SQL applies on restart. Rollback: v26.
; v28 (2026-09-01 11:22) - THE LIVE KIT, and the one gap in this ladder: no git commit
;   and no operator report exist for it. 215 of its 217 files differ from v27, so it is
;   a substantive change, not a re-stamp; its content is only recoverable by diffing the
;   bytes against v27. kitVersion 23 (unchanged, no SQL move); same worldserver.
;   patch-9 in force: the live md5 97fc5cd5, which itself has no pre_* backup.
;   Rollback: v27.
```

- [ ] **Step 4: Take the missing backups**

```bash
cp "/c/wowstuff/FL2-Client/Data/patch-9.MPQ" "/c/wowstuff/FL2-Client/Data/patch-9.MPQ.pre_roundA_20260901"
```

The client must be **closed** — a running client holds `patch-9.MPQ` so hard it cannot even be
renamed. Verify with `certutil -hashfile` that the copy is `97fc5cd5...`.

**And back up the four pipeline scripts Tasks 14–17 rewrite.** They live in
`C:\wowstuff\ForgottenLand2.0\scripts`, which is **not a git repo**, so an edit there is not
revertable by any other means:

```bash
cd /c/wowstuff/ForgottenLand2.0/scripts && for f in 48_gen_t1_blockkit.py 51_texture_blockkit.py 52_punch_kit_holes.py 30_build_hot_dbc_patch.py; do cp "$f" "$f.pre_roundA_20260901"; done && ls -la *.pre_roundA_20260901
```

Expected: four `.pre_roundA_20260901` files. Record their sizes in the baseline; they are the only
rollback the client track has.

- [ ] **Step 5: Record the measured-green baseline**

Rebuild `pdblock` and `pdgen` per the Global Constraints section, run all four gates, and paste the
verbatim output into `pd_roundA_baseline.md` under a heading `Before Round A (2026-09-01)`. This is
what every later task diffs against, and what Task 18's operator document cites.

Also record the **dead rollback entry**. Corrected 2026-09-01 after measurement — the earlier draft
of this step misattributed it: the prohibition *"Verboten: ein v5+-Kit unter
`FLStream.dll.pre_phase3_20260830`"* appears in the **operator round documents'** rollback tables
(`pd_testlauf_runde14/15/16.md`, `pd_t2_checkliste_*.md`), **not** in the `FLStream.ini` ladder,
which contains no occurrence of "Verboten" at all — it references that DLL once, at line 17, as a
general note that the DLL and the `KitDir` flip move together.

The substance stands either way: **`FLStream.dll.pre_phase3_20260830` does not exist anywhere on
this box.** Note in the baseline that the constraint is unenforceable, and attribute it to the
round documents. Do **not** manufacture a replacement DLL backup — a "backup" that was never
actually taken from the running system is worse than a documented gap. Verify the attribution
against the live files before writing it down.

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/plans/ && git commit -m "docs(plan): Round A implementation plan and rollback baseline"
```

`FLStream.ini` and the workspace `tools/` file live outside the repo; note their paths in the commit
body so the round is reconstructable.

---

## Task 2: Split the dead-end role name

`BlockRoleName` maps **both** `CorridorCross` (6) and `CorridorDeadEnd` (7) through `default:` to
`"corridor_cross"`. A `roleFilter` aimed at dead ends silently matches nothing, and one aimed at
crosses silently also fires on every dead-end stub — which already carries a cave-in prop and the
chest. This lands first because every later decor task writes `roleFilter` values.

**Files:**
- Modify: `src/generator/PDv2DecorPlan.cpp:246-258`
- Test: `tests/blockplan_harness.cpp` (new check inside `RunDecorBatch`)

**Interfaces:**
- Consumes: nothing.
- Produces: the role string `"corridor_dead_end"`, usable as a `roleFilter` by Tasks 5 and 7.

- [ ] **Step 1: Write the failing test**

Add to `tests/blockplan_harness.cpp`, in the same anonymous namespace as the other decor helpers:

```cpp
        // A dead-end stub must report its own role name. Until the split it
        // reported "corridor_cross", which made both filters wrong at once.
        bool CheckRoleNamesDistinct(BlockPlan const& plan, std::string& why)
        {
            bool sawDeadEnd = false;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.role != BlockRole::CorridorDeadEnd) continue;
                sawDeadEnd = true;
                if (std::strcmp(BlockRoleName(b.role), "corridor_dead_end") != 0)
                {
                    why = "a dead-end stub does not report role name corridor_dead_end";
                    return false;
                }
            }
            if (sawDeadEnd && std::strcmp(BlockRoleName(BlockRole::CorridorCross),
                                          "corridor_cross") != 0)
            {
                why = "corridor_cross lost its own name";
                return false;
            }
            return true;
        }
```

and call it inside `RunDecorBatch`'s per-seed loop, beside the existing `CheckDecorSpots` call:

```cpp
                Check(CheckRoleNamesDistinct(plan, why),
                      why.empty() ? "role naming broken" : why.c_str(), seed);
```

`BlockRoleName` must be declared in `src/generator/PDv2DecorPlan.h` for the harness to call it — it
is currently defined in the `.cpp` at namespace scope. Add the declaration beside `PDv2Classify`:

```cpp
    // The kit's role name for a block, which is what `DecorRule::roleFilter`
    // prefix-matches against.
    char const* BlockRoleName(BlockRole role);
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 200
```

Expected: `FAIL seed <n>: a dead-end stub does not report role name corridor_dead_end` on the seeds
whose layouts drew a stub (the stub count is `UniformInt(0, 2)`, so most seeds have one), then
`FAILURES`, exit 1.

- [ ] **Step 3: Write the implementation**

Replace `src/generator/PDv2DecorPlan.cpp:246-258` with:

```cpp
    char const* BlockRoleName(BlockRole role)
    {
        switch (role)
        {
            case BlockRole::Room:             return "room";
            case BlockRole::RoomEntrance:     return "room_entrance";
            case BlockRole::RoomBoss:         return "room_boss";
            case BlockRole::CorridorStraight: return "corridor_straight";
            case BlockRole::CorridorCorner:   return "corridor_corner";
            case BlockRole::CorridorT:        return "corridor_t";
            case BlockRole::CorridorCross:    return "corridor_cross";
            // A stub is walked into for its chest and never fought in, so it
            // wants its own dressing. It reported "corridor_cross" through the
            // old `default:`, which made a corridor_cross filter fire on stubs
            // and a corridor_dead_end filter match nothing at all.
            case BlockRole::CorridorDeadEnd:  return "corridor_dead_end";
        }
        return "corridor_cross";
    }
```

The trailing `return` keeps `/W4` quiet about a non-void function without adding a `default:` that
would swallow a future role.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000 && pdblock.exe --batch 500
```

Expected: `ALL CHECKS PASS` from both, exit 0. The decor check count rises above 27691 by one per
(seed × room count).

**Expect the props-per-layout range to change**: rule 3's `roleFilter` is `"corridor"`, which
prefix-matches `corridor_dead_end` exactly as it did `corridor_cross`, so no rule changes hands —
but confirm `props per layout` against the Task 1 baseline and record any movement.

- [ ] **Step 5: Commit**

```bash
git add src/generator/PDv2DecorPlan.h src/generator/PDv2DecorPlan.cpp tests/blockplan_harness.cpp
git commit -m "fix(decor): a dead-end stub reports its own role name"
```

---

## Task 3: The `corner` placement kind

**Files:**
- Modify: `src/generator/PDv2DecorPlan.h:59-70` (constant), `src/generator/PDv2DecorPlan.cpp:91-124` (new collector beneath `CollectWallFeet`), `:426-455` (per-placement pools)
- Test: `tests/blockplan_harness.cpp:1607-1645` (`CheckDecorSpots` becomes placement-aware)

**Interfaces:**
- Consumes: `BlockRoleName` from Task 2.
- Produces: `DECOR_PLACEMENT_CORNER` (`"corner"`); `Candidate::dir2` (a second `WALL_DIRS` index, `-1` when unused); `CollectCorners(std::string const& classes, std::vector<Candidate>& out)`.

- [ ] **Step 1: Write the failing test**

`CheckDecorSpots` currently rejects any spot whose cell touches no wall, which is right for
`wall_foot` and will be wrong for `scatter` in Task 4. Make it placement-aware now, and add the
corner assertion. Replace the `touchesWall` block at `tests/blockplan_harness.cpp:1626-1645`:

```cpp
            // Which placement kind put this spot here. The rule vector is
            // already in hand for `spacingOf`, so this is the same lookup.
            std::string placement = PDungeon::DECOR_PLACEMENT_WALL_FOOT;
            for (DecorRule const& r : rules)
            {
                if (r.id == spot.ruleId) { placement = r.placement; break; }
            }

            int wallSides = 0;
            int const drow[4] = { -1, 0, 1, 0 };
            int const dcol[4] = { 0, 1, 0, -1 };
            bool wallAt[4] = { false, false, false, false };
            for (int d = 0; d < 4; ++d)
            {
                int const r = row + drow[d];
                int const c = col + dcol[d];
                if (r < 0 || c < 0 ||
                    r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                {
                    continue;
                }
                wallAt[d] =
                    classes[static_cast<size_t>(r) * PD_CELLS_PER_BLOCK + c] == 'L';
                if (wallAt[d]) ++wallSides;
            }

            if (placement == PDungeon::DECOR_PLACEMENT_WALL_FOOT)
            {
                if (!wallSides)
                {
                    why = "a wall_foot spot's cell touches no wall cell";
                    return false;
                }
            }
            else if (placement == PDungeon::DECOR_PLACEMENT_CORNER)
            {
                // Two ADJACENT walls, i.e. N+E, E+S, S+W or W+N. Two OPPOSITE
                // walls make a passage, not a corner, and a prop wedged there
                // would block it.
                bool adjacent = (wallAt[0] && wallAt[1]) || (wallAt[1] && wallAt[2]) ||
                                (wallAt[2] && wallAt[3]) || (wallAt[3] && wallAt[0]);
                if (!adjacent)
                {
                    why = "a corner spot's cell has no two adjacent wall sides";
                    return false;
                }
            }
```

- [ ] **Step 2: Run the test to verify it fails**

The harness will not compile yet — `DECOR_PLACEMENT_CORNER` does not exist.

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
```

Expected: `error C2065: 'DECOR_PLACEMENT_CORNER': nicht deklarierter Bezeichner`. That is the failing
state for this step — a compile failure is a legitimate red, and the next step turns it green.

- [ ] **Step 3: Write the implementation**

**3a.** In `src/generator/PDv2DecorPlan.h`, beside `DECOR_PLACEMENT_WALL_FOOT` (replacing its
"only placement kind" comment):

```cpp
    // The placement kinds the planner implements. A rule naming anything else
    // is skipped rather than guessed at - a prop placed by a rule nobody wrote
    // is worse than a prop that never appears.
    char const* const DECOR_PLACEMENT_WALL_FOOT = "wall_foot";
    // A walk cell with wall cells on two ADJACENT sides. Two opposite walls
    // are a passage, not a corner, and are deliberately not candidates.
    char const* const DECOR_PLACEMENT_CORNER    = "corner";
```

**3b.** In `src/generator/PDv2DecorPlan.cpp`, extend `Candidate` (`:69-74`) with a second wall side:

```cpp
        struct Candidate
        {
            int row = 0;
            int col = 0;
            int dir = 0;
            // The SECOND wall side, for a corner candidate; -1 when the cell
            // has only one. The nudge for a corner is the sum of both sides'
            // offsets, so the prop sits in the angle rather than against one
            // face, and the facing bisects them.
            int dir2 = -1;
        };
```

**3c.** Add `CollectCorners` directly beneath `CollectWallFeet` (after `:124`):

```cpp
        // Every WALK cell with WALL cells on two ADJACENT sides, row-major,
        // socket track excluded. The two sides are recorded in WALL_DIRS order,
        // so a cell with three walls always resolves to the same pair.
        void CollectCorners(std::string const& classes,
                            std::vector<Candidate>& out)
        {
            out.clear();
            for (int row = 0; row < PD_CELLS_PER_BLOCK; ++row)
            {
                for (int col = 0; col < PD_CELLS_PER_BLOCK; ++col)
                {
                    if (row == SOCKET_TRACK || col == SOCKET_TRACK) continue;
                    if (classes[Index(row, col)] != DECOR_CLASS_WALK) continue;

                    bool wall[4] = { false, false, false, false };
                    for (int d = 0; d < 4; ++d)
                    {
                        int const r = row + WALL_DIRS[d].drow;
                        int const c = col + WALL_DIRS[d].dcol;
                        if (r < 0 || c < 0 ||
                            r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                        {
                            continue;
                        }
                        wall[d] = classes[Index(r, c)] == DECOR_CLASS_WALL;
                    }

                    // N,E,S,W: adjacency is d and (d+1)%4. The first pair in
                    // this order wins, so a three-walled alcove is stable.
                    for (int d = 0; d < 4; ++d)
                    {
                        int const e = (d + 1) % 4;
                        if (!wall[d] || !wall[e]) continue;

                        Candidate cand;
                        cand.row = row;
                        cand.col = col;
                        cand.dir = d;
                        cand.dir2 = e;
                        out.push_back(cand);
                        break;
                    }
                }
            }
        }
```

**3d.** In `BuildDecorPlan`, replace the single pool with one pool per placement kind. At `:426`,
where `CollectWallFeet(classes, pool);` stands today:

```cpp
            CollectWallFeet(classes, poolWallFoot);
            CollectCorners(classes, poolCorner);
```

Declare `poolWallFoot` and `poolCorner` beside the existing `pool` declaration at `:252` (rename
`pool` to `poolWallFoot` throughout and add the new vector). Then make the rule match and the share
arithmetic per-kind. Replace the matching loop and `poolAtStart` (`:287-307`):

```cpp
            // Which rules speak for this block, per placement kind. Weights are
            // a share of THAT kind's candidate cells: a corner rule competes
            // with corner rules, never with the wall-foot torches.
            matching.clear();
            int totalWeight[PD_DECOR_PLACEMENT_COUNT] = { 0, 0 };
            for (size_t const i : byId)
            {
                DecorRule const& rule = rules[i];
                if (rule.theme != 0 && rule.theme != plan.config.theme)
                    continue;
                int const kind = PlacementIndex(rule.placement);
                if (kind < 0) continue;
                if (!DecorRoleMatches(rule.roleFilter, roleName)) continue;
                matching.push_back(i);
                totalWeight[kind] += RuleWeight(rule);
            }
            if (matching.empty())
            {
                continue;
            }

            int const poolAtStart[PD_DECOR_PLACEMENT_COUNT] = {
                static_cast<int>(poolWallFoot.size()),
                static_cast<int>(poolCorner.size())
            };
```

with the helper in the anonymous namespace:

```cpp
        // Placement name -> pool index, or -1 for a kind no planner implements.
        // The order here is the order of the pool arrays and must not change.
        int PlacementIndex(std::string const& placement)
        {
            if (placement == DECOR_PLACEMENT_WALL_FOOT) return 0;
            if (placement == DECOR_PLACEMENT_CORNER)    return 1;
            return -1;
        }
```

and, in `PDv2DecorPlan.h` beside the constants:

```cpp
    // How many placement kinds the planner implements. The pools and the
    // per-kind weight totals are sized by it.
    int const PD_DECOR_PLACEMENT_COUNT = 2;
```

**3e.** In the draw loop (`:456` onward), select the pool per rule and build the corner geometry:

```cpp
            for (size_t const i : matching)
            {
                DecorRule const& rule = rules[i];
                int const kind = PlacementIndex(rule.placement);
                std::vector<Candidate>& pool =
                    (kind == 1) ? poolCorner : poolWallFoot;

                int want = rng.UniformInt(rule.minPerBlock, rule.maxPerBlock);
                if (want < 0)
                {
                    want = 0;
                }
                int const share = (poolAtStart[kind] * RuleWeight(rule) +
                                   totalWeight[kind] - 1) / totalWeight[kind];
                int const take = want < share ? want : share;
```

and, where the spot's `u`/`v`/`orientation` are set:

```cpp
                    WallDir const& dir = WALL_DIRS[cand.dir];
                    DecorSpot spot;
                    spot.bx = block.bx;
                    spot.by = block.by;
                    spot.ruleId = rule.id;
                    spot.goEntry = rule.goEntry;
                    spot.u = (static_cast<double>(cand.row) + 0.5) *
                             PD_CELL_SIZE_YD + dir.du;
                    spot.v = (static_cast<double>(cand.col) + 0.5) *
                             PD_CELL_SIZE_YD + dir.dv;
                    spot.orientation = dir.facing;

                    if (cand.dir2 >= 0)
                    {
                        // A corner: nudged into the angle of BOTH walls, and
                        // facing out along their bisector. Derived, never
                        // drawn, exactly like the single-wall case.
                        WallDir const& dir2 = WALL_DIRS[cand.dir2];
                        spot.u += dir2.du;
                        spot.v += dir2.dv;
                        spot.orientation = BisectFacing(dir.facing, dir2.facing);
                    }
```

with:

```cpp
        // The angle halfway between two wall facings, on the short arc. The
        // pairs are always 90 degrees apart, so this is the corner's diagonal.
        double BisectFacing(double a, double b)
        {
            double diff = b - a;
            while (diff > DECOR_PI)  diff -= 2.0 * DECOR_PI;
            while (diff < -DECOR_PI) diff += 2.0 * DECOR_PI;
            double out = a + diff * 0.5;
            while (out < 0.0)             out += 2.0 * DECOR_PI;
            while (out >= 2.0 * DECOR_PI) out -= 2.0 * DECOR_PI;
            return out;
        }
```

**Note the geometry budget:** the wall nudge is 2.5 yd per side and a cell is 8.33 yd, so a corner
nudge of 2.5 yd on each axis leaves the spot 1.67 yd inside its own cell on both axes. That is what
keeps "every spot is on a walkable cell" true, and it is why the nudge is not doubled.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000 && pdblock.exe --batch 500
```

Expected: `ALL CHECKS PASS`, exit 0, from both. With no corner rule in the fixture yet, the corner
pool is collected but never drawn from, so `props per layout` must be **unchanged** from the Task 2
run — if it moved, the pool split changed the wall-foot share arithmetic and the bug is in step 3d.

- [ ] **Step 5: Commit**

```bash
git add src/generator/PDv2DecorPlan.h src/generator/PDv2DecorPlan.cpp tests/blockplan_harness.cpp
git commit -m "feat(decor): corner placement kind, one candidate pool per kind"
```

---

## Task 4: The `scatter` placement kind

**Files:**
- Modify: `src/generator/PDv2DecorPlan.h` (constant, count), `src/generator/PDv2DecorPlan.cpp` (`CollectScatter`, third pool)
- Test: `tests/blockplan_harness.cpp` (`CheckDecorSpots` gains the scatter branch)

**Interfaces:**
- Consumes: `PlacementIndex`, the pool arrays and `PD_DECOR_PLACEMENT_COUNT` from Task 3.
- Produces: `DECOR_PLACEMENT_SCATTER` (`"scatter"`); `CollectScatter(std::string const&, std::vector<Candidate>&)`.

- [ ] **Step 1: Write the failing test**

Add the third branch to the placement dispatch written in Task 3, after the `DECOR_PLACEMENT_CORNER`
case:

```cpp
            else if (placement == PDungeon::DECOR_PLACEMENT_SCATTER)
            {
                // The inverse of wall_foot: open floor. A scatter prop that
                // ends up against a wall duplicates the wall_foot pass and
                // leaves the middle of the room as empty as it was.
                if (wallSides)
                {
                    why = "a scatter spot's cell touches a wall cell";
                    return false;
                }
            }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
```

Expected: `error C2065: 'DECOR_PLACEMENT_SCATTER': nicht deklarierter Bezeichner`.

- [ ] **Step 3: Write the implementation**

**3a.** In `src/generator/PDv2DecorPlan.h`:

```cpp
    // Open floor: a walk cell with NO wall on any of its four sides. The
    // inverse of wall_foot, so the two kinds can never fight over a cell.
    char const* const DECOR_PLACEMENT_SCATTER   = "scatter";
```

and raise the count:

```cpp
    int const PD_DECOR_PLACEMENT_COUNT = 3;
```

**3b.** `PlacementIndex` gains its third line:

```cpp
            if (placement == DECOR_PLACEMENT_SCATTER)   return 2;
```

**3c.** `CollectScatter`, beneath `CollectCorners`:

```cpp
        // Every WALK cell with no WALL on any side, row-major, socket track
        // excluded. `dir` stays 0 and is never applied: a scattered prop sits
        // on its cell centre and faces north, because there is no wall to turn
        // away from and a drawn angle would cost the stream a draw per prop.
        void CollectScatter(std::string const& classes,
                            std::vector<Candidate>& out)
        {
            out.clear();
            for (int row = 0; row < PD_CELLS_PER_BLOCK; ++row)
            {
                for (int col = 0; col < PD_CELLS_PER_BLOCK; ++col)
                {
                    if (row == SOCKET_TRACK || col == SOCKET_TRACK) continue;
                    if (classes[Index(row, col)] != DECOR_CLASS_WALK) continue;

                    bool touchesWall = false;
                    for (int d = 0; d < 4 && !touchesWall; ++d)
                    {
                        int const r = row + WALL_DIRS[d].drow;
                        int const c = col + WALL_DIRS[d].dcol;
                        if (r < 0 || c < 0 ||
                            r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                        {
                            continue;
                        }
                        touchesWall = classes[Index(r, c)] == DECOR_CLASS_WALL;
                    }
                    if (touchesWall) continue;

                    Candidate cand;
                    cand.row = row;
                    cand.col = col;
                    out.push_back(cand);
                }
            }
        }
```

**3d.** Collect it beside the other two into `poolScatter`, declared next to `poolWallFoot` and
`poolCorner`, and selected by the same `kind` switch:

```cpp
            CollectWallFeet(classes, poolWallFoot);
            CollectCorners(classes, poolCorner);
            CollectScatter(classes, poolScatter);
```

```cpp
            int const poolAtStart[PD_DECOR_PLACEMENT_COUNT] = {
                static_cast<int>(poolWallFoot.size()),
                static_cast<int>(poolCorner.size()),
                static_cast<int>(poolScatter.size())
            };
```

```cpp
                std::vector<Candidate>& pool =
                    (kind == 2) ? poolScatter :
                    (kind == 1) ? poolCorner  : poolWallFoot;
```

and `int totalWeight[PD_DECOR_PLACEMENT_COUNT] = { 0, 0, 0 };`.

Note the nudge must **not** be applied:
a scatter candidate has `dir == 0` and `dir2 == -1`, so guard the nudge on the kind rather than on
the candidate:

```cpp
                    DecorSpot spot;
                    spot.bx = block.bx;
                    spot.by = block.by;
                    spot.ruleId = rule.id;
                    spot.goEntry = rule.goEntry;
                    spot.u = (static_cast<double>(cand.row) + 0.5) * PD_CELL_SIZE_YD;
                    spot.v = (static_cast<double>(cand.col) + 0.5) * PD_CELL_SIZE_YD;
                    spot.orientation = 0.0;

                    if (kind != 2)
                    {
                        WallDir const& dir = WALL_DIRS[cand.dir];
                        spot.u += dir.du;
                        spot.v += dir.dv;
                        spot.orientation = dir.facing;

                        if (cand.dir2 >= 0)
                        {
                            WallDir const& dir2 = WALL_DIRS[cand.dir2];
                            spot.u += dir2.du;
                            spot.v += dir2.dv;
                            spot.orientation = BisectFacing(dir.facing, dir2.facing);
                        }
                    }
```

**The pad matters here.** Script 48 vetoes the centre-pad cells out of the walk mask
(`48:2956-2959`), so `PDv2Classify` derives them as `L` (WALL) — a 2×2 fountain pad, or a WALL ring
around a `V` centre for the 4×4 boss pad. That means pad-adjacent cells are already wall feet and
corner candidates, and are correctly **excluded** from scatter. No extra pad handling is needed;
this is stated so the next reader does not add any.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000 && pdblock.exe --batch 500
```

Expected: `ALL CHECKS PASS`, exit 0. `props per layout` still unchanged — no rule uses the new kinds
until Task 7's SQL and Task 6's fixture.

- [ ] **Step 5: Commit**

```bash
git add src/generator/PDv2DecorPlan.h src/generator/PDv2DecorPlan.cpp tests/blockplan_harness.cpp
git commit -m "feat(decor): scatter placement kind for open floor"
```

---

## Task 5: A decor budget the planner owns

PDv2 has **no GameObject cap anywhere** — v1's decor-first truncation was never carried over, and
`SpawnDecor` summons every spot in one synchronous loop. Measured from 30 live 14-room runs, a
top-end layout summons 42–66 GameObjects today; the arithmetic worst case at the true room cap is
~102 decor spots before this round adds three more rule families. The cap belongs in the **planner**,
not the instance: there it is deterministic, harness-testable, and cannot desynchronise the stream.

**Files:**
- Modify: `src/generator/PDv2DecorPlan.h` (constant), `src/generator/PDv2DecorPlan.cpp` (the cut at the end of `BuildDecorPlan`)
- Test: `tests/blockplan_harness.cpp` (`RunDecorBatch`)

**Interfaces:**
- Consumes: `BuildDecorPlan` from Tasks 3–4.
- Produces: `PD_DECOR_MAX_SPOTS`; the guarantee that `BuildDecorPlan` never returns more than that many spots.

- [ ] **Step 1: Write the failing test**

In `RunDecorBatch`, beside the existing checks:

```cpp
                Check(first.size() <= static_cast<size_t>(PD_DECOR_MAX_SPOTS),
                      "a layout exceeded the decor spot budget", seed);
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
```

Expected: `error C2065: 'PD_DECOR_MAX_SPOTS': nicht deklarierter Bezeichner`.

- [ ] **Step 3: Write the implementation**

In `src/generator/PDv2DecorPlan.h`:

```cpp
    // Hard ceiling on the props one layout may plan. v1 had a GameObject cap
    // and v2 lost it; a 17-room layout can already ask for ~100 spots, and
    // this round adds three rule families on top. 250 is roughly 2.5x the
    // measured worst case, so it never bites a normal layout and always bites
    // a runaway rule. The cut is taken at the END, in plan order, so which
    // props survive is a property of the plan and not of the draw.
    int const PD_DECOR_MAX_SPOTS = 250;
```

At the end of `BuildDecorPlan`, replacing the bare `return out;`:

```cpp
        // Truncate in plan order: blocks near the entrance keep their dressing
        // and the tail is what is lost, which is the same bias v1's decor-first
        // truncation had. Never truncate by drawing - that would make the
        // survivors depend on the budget.
        if (out.size() > static_cast<size_t>(PD_DECOR_MAX_SPOTS))
        {
            out.resize(static_cast<size_t>(PD_DECOR_MAX_SPOTS));
        }
        return out;
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000
```

Expected: `ALL CHECKS PASS`. `props per layout` unchanged — today's maximum is 51, far under 250,
so the budget must not bite yet. **If the maximum moved, the cut is firing and 250 is wrong.**

- [ ] **Step 5: Commit**

```bash
git add src/generator/PDv2DecorPlan.h src/generator/PDv2DecorPlan.cpp tests/blockplan_harness.cpp
git commit -m "feat(decor): a spot budget the planner owns, cut in plan order"
```

---

## Task 6: Teach the loader and the fixture the new kinds

Two things still believe `wall_foot` is the only kind. `PDv2Mgr::LoadDecorRules` logs a **LOG_ERROR**
for any other placement — which would fire on every new rule and turn the operator's boot-log check
into a false alarm. And the harness fixture `DecorFixture` writes `theme = 1` for all three rules
while the live DB holds `theme = 0`, so **`--decor-batch` has only ever exercised theme 1** and has
never decorated the padded theme-2 rooms the live server actually generates.

**Files:**
- Modify: `src/PDv2Mgr.cpp:556-566` (the placement validation)
- Modify: `tests/blockplan_harness.cpp:1481-1519` (`DecorFixture`), `:78-94` (`MakeCfg`), `RunDecorBatch`

**Interfaces:**
- Consumes: the three placement constants.
- Produces: a decor batch that runs both themes and covers all three kinds.

- [ ] **Step 1: Write the failing test**

**1a.** Set the theme on the config **after** construction, in `RunDecorBatch`.

**Corrected 2026-09-01 during implementation.** An earlier draft of this step had you rewrite
`MakeCfg` with a `theme` parameter and a verbatim body — that body was wrong and would have deleted
the real `MakeCfg`'s `originBX` / `originBY` parameters (`tests/blockplan_harness.cpp:78`), which
`WriteManifest` genuinely depends on. **Leave `MakeCfg` untouched.** The file already has the right
pattern at `:323-324`:

```cpp
        BlockCfg cfg = MakeCfg(seed, rooms, obx, oby);
        cfg.theme = theme;
```

Mirror exactly that in `RunDecorBatch` — one call site changed, no signature moved.

**1b.** In `RunDecorBatch`, run every seed through both themes:

```cpp
            int const THEMES[2] = { 1, 2 };
            for (int const theme : THEMES)
            {
                for (int const rooms : ROOM_MATRIX)
                {
                    BlockCfg cfg = MakeCfg(seed, rooms);
                    cfg.theme = theme;

                    BlockPlan plan;
                    if (!GenerateBlockPlan(cfg, &plan))
                    {
                        Check(false, "generation failed", seed);
                        continue;
                    }
                    // ... the existing body, unchanged ...
                }
            }
```

**1c.** Correct `DecorFixture` (`tests/blockplan_harness.cpp:1481-1519`) to `theme = 0` — matching
the three live DB rows — and add one rule per new kind. The fixture builds each rule inline with
named locals; keep that style rather than introducing a helper. Set `theme = 0` on the three
existing rules and append:

```cpp
        DecorRule crateCorner;
        crateCorner.id = 4;
        crateCorner.theme = 0;
        crateCorner.roleFilter = "room";
        crateCorner.goEntry = 910060;
        crateCorner.placement = DECOR_PLACEMENT_CORNER;
        crateCorner.minPerBlock = 1;
        crateCorner.maxPerBlock = 2;
        crateCorner.weight = 100;
        crateCorner.minSpacingYd = 8.0;
        rules.push_back(crateCorner);

        DecorRule rubbleScatter;
        rubbleScatter.id = 5;
        rubbleScatter.theme = 0;
        rubbleScatter.roleFilter = "room";
        rubbleScatter.goEntry = 910070;
        rubbleScatter.placement = DECOR_PLACEMENT_SCATTER;
        rubbleScatter.minPerBlock = 1;
        rubbleScatter.maxPerBlock = 3;
        rubbleScatter.weight = 100;
        rubbleScatter.minSpacingYd = 12.0;
        rules.push_back(rubbleScatter);
```

`DecorRule::placement` defaults to `DECOR_PLACEMENT_WALL_FOOT`, which is why the three existing
rules never set it and the two new ones must.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000
```

Expected: it now runs **twice** as many seeds-with-themes and places corner and scatter props. This
is the run that proves Tasks 3–5 against real kit masks. Any `FAIL ... a corner spot's cell has no
two adjacent wall sides` or `a scatter spot's cell touches a wall cell` here is a real bug in the
collectors, not in the fixture — fix the collector.

Record the new `props per layout` range and check count; both will move, and both go in the Task 18
operator document.

- [ ] **Step 3: Write the implementation**

`src/PDv2Mgr.cpp`, replacing the single-kind test at `:556-566`:

```cpp
            if (rule.placement != DECOR_PLACEMENT_WALL_FOOT &&
                rule.placement != DECOR_PLACEMENT_CORNER &&
                rule.placement != DECOR_PLACEMENT_SCATTER)
            {
                // Kept in the list all the same: the planner skips it by the
                // same test, and dropping it here would hide a typo that the
                // operator can only find by counting props.
                LOG_ERROR(PD_LOG, "PDv2: decor rule {} asks for placement '{}', which "
                                  "no planner implements - it will place nothing",
                          rule.id, rule.placement);
            }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000 && pdblock.exe --batch 500 && pdblock.exe --roomcap 3000
```

Expected: `ALL CHECKS PASS` from all three, exit 0. `--roomcap` must still report cap 15 and
`1470` bytes — Round A does not touch the layout, so any movement there is a bug.

- [ ] **Step 5: Commit**

```bash
git add src/PDv2Mgr.cpp tests/blockplan_harness.cpp
git commit -m "feat(decor): loader accepts corner and scatter; decor batch covers both themes"
```

---

## Task 7: The clutter content, and the landmine under it

**Files:**
- Create: `data/sql/db-world/mod_pdungeon_decor_clutter.sql`
- Create: `data/sql/db-world/mod_pdungeon_templates_fix.sql`

**Interfaces:**
- Consumes: the three placement kinds and the `corridor_dead_end` role name.
- Produces: `gameobject_template` entries 910050–910077 and `pdungeon_decor_rules` ids 4–14.

- [ ] **Step 1: Defuse the landmine first**

`data/sql/db-world/mod_pdungeon_templates.sql:18` carries
`DELETE FROM gameobject_template WHERE entry BETWEEN 910000 AND 910099` and then re-inserts only
`910000-910033`. The updater re-applies a module SQL file whose content changed — `decor.sql` and
`packs.sql` were both re-applied on 2026-08-31 for exactly that reason — so **any future edit to
that file silently deletes the kit props 910040-910047 and everything this task adds.**

Do not edit `mod_pdungeon_templates.sql` (the updater would skip it on existing databases). Ship a
new file that re-inserts what the range delete would take.

**Which file that is, is not a free choice — corrected 2026-09-01 after review.** The updater
applies files in **filename order**, each gated independently by its own content hash
(`src/server/database/Updater/UpdateFetcher.h:133` `PathCompare`, which compares
`filename().string()`). A file whose hash is unchanged is skipped; there is no cascading re-apply of
siblings. The module's files therefore sort:

```
mod_pdungeon_decor.sql  <  mod_pdungeon_decor_clutter.sql  <  mod_pdungeon_templates.sql  <  mod_pdungeon_templates_fix.sql
```

`mod_pdungeon_decor_clutter.sql` sorts **before** the file that wipes the range, so it can never
restore itself. Only `mod_pdungeon_templates_fix.sql` sorts after it. **So every
`gameobject_template` row in 910040-910099 — the 8 kit props and all 22 clutter props — lives in
`mod_pdungeon_templates_fix.sql`, and `mod_pdungeon_decor_clutter.sql` carries only the
`pdungeon_decor_rules` rows** (a different table, untouched by the range delete, therefore safe
where it is).

The failure this prevents: a future edit to `mod_pdungeon_templates.sql` re-applies it, its range
delete takes 910040-910077, and the decor rules survive pointing at `goEntry` values with no
template. `Map::SummonGameObject` then fails silently per spawn — log spam, no object — which is the
"dark and bare" outcome this task exists to prevent, just moved to the new range.

**The row values below came from research and were measured wrong for 910040-910043.** Read the
live rows and copy them verbatim; the live value wins: `PD Fountain` (92040), `PD Rock Column`
(5073, size 1.7), `PD Stalagmite` (5073), `PD Cave-In` (2230, size 0.4).

```sql
-- mod_pdungeon_templates_fix.sql
--
-- mod_pdungeon_templates.sql opens with
--   DELETE FROM gameobject_template WHERE entry BETWEEN 910000 AND 910099
-- and re-inserts only 910000-910033. Since then the kit props (910040-910047)
-- and this round's clutter (910050+) moved into that range, so a re-apply of
-- that file would wipe them and every PDv2 dungeon would go dark and bare.
--
-- This file runs AFTER it by filename order and restores the kit props. The
-- clutter rows restore themselves the same way, from
-- mod_pdungeon_decor_clutter.sql. Neither file may ever use a range DELETE.
--
-- The real fix is to narrow the range delete in mod_pdungeon_templates.sql to
-- 910000-910033; that is deferred because the updater will not re-apply an
-- edited base file to a database that already has it. Recorded in the global
-- queue.

DELETE FROM `gameobject_template` WHERE `entry` IN (910040,910041,910042,910043,910044,910045,910046,910047);
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`size`,`Data0`,`Data1`,`ScriptName`) VALUES
(910040, 5, 6961, 'PD Broken Cart Kit', 1,   0, 0, ''),
(910041, 5, 4592, 'PD Stalagmite A',    1,   0, 0, ''),
(910042, 5, 4593, 'PD Stalagmite B',    1,   0, 0, ''),
(910043, 5, 2891, 'PD Cave In',         1,   0, 0, ''),
(910044, 5, 6961, 'PD Broken Cart',     1,   0, 0, ''),
(910045, 5,  251, 'PD Haystack',        1.2, 0, 0, ''),
(910046, 5, 2890, 'PD Sack Pile',       1,   0, 0, ''),
(910047, 5,  150, 'PD Signpost',        1,   0, 0, '');
```

**Before writing this file, read the live rows and copy them verbatim** — the values above are from
research and must be confirmed, not trusted:

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT entry,type,displayId,name,size,Data0,Data1,ScriptName FROM gameobject_template WHERE entry BETWEEN 910040 AND 910049 ORDER BY entry;"
```

- [ ] **Step 2: Write the clutter templates and rules**

`data/sql/db-world/mod_pdungeon_decor_clutter.sql`. Every displayId below was verified three ways:
against `C:\wowstuff\dcore\Data\dbc\GameObjectDisplayInfo.dbc` (the model path), against
`C:\wowstuff\dcore\Data\vmaps\GameObjectModels.dtree` (proof of a collision model and its true
bounding box), and against stock `gameobject_template` rows (proof it is used as indoor dressing).

```sql
-- mod_pdungeon_decor_clutter.sql
--
-- Broken furniture, crates, rubble and bones for PDv2. Data only.
--
-- Every entry is type 5 GENERIC: it is the only GameObject class measured to
-- block a player on map 760 (MDDF/MODF doodads do not), which is why decor is
-- GameObjects at all. `size` is the only scale dial available.
--
-- Idempotent by an explicit entry list, NEVER by range: 910000-910099 is
-- shared with the kit props and mod_pdungeon_templates.sql already has one
-- range delete too many (see mod_pdungeon_templates_fix.sql).
--
-- Rule ids start at 4 so the shipped mod_pdungeon_decor.sql's
-- `DELETE ... WHERE id BETWEEN 1 AND 3` can never touch them.
--
-- theme 0 = any look, the same sentinel the packs use. Scoping a rule to one
-- theme is what once left the city with no server decor at all.

DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 910050 AND 910077;
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`size`,`Data0`,`Data1`,`ScriptName`) VALUES
-- wall feet: things that stand against a wall
(910050, 5,  288, 'PD Barrel',              1.0,  0, 0, ''),
(910051, 5,  275, 'PD Crate',               1.0,  0, 0, ''),
(910052, 5, 7470, 'PD Plague Barrel',       1.0,  0, 0, ''),
(910053, 5,  130, 'PD Weapon Rack',         1.0,  0, 0, ''),
(910054, 5,  187, 'PD Bookshelf',           1.0,  0, 0, ''),
(910055, 5, 4391, 'PD Alchemy Bench',       0.9,  0, 0, ''),
(910056, 5,  234, 'PD Long Table',          0.8,  0, 0, ''),
(910057, 5, 5511, 'PD Grain Sack',          1.5,  0, 0, ''),
-- corners: things that wedge into an angle
(910060, 5, 1868, 'PD Crate Stack',         0.8,  0, 0, ''),
(910061, 5, 1869, 'PD Crate Stack Alt',     0.8,  0, 0, ''),
(910062, 5, 7680, 'PD Broken Crate Stack',  0.75, 0, 0, ''),
(910063, 5, 6036, 'PD Tall Barrel',         1.2,  0, 0, ''),
(910064, 5, 7526, 'PD Broken Keg',          0.6,  0, 0, ''),
(910065, 5, 8480, 'PD Cart Wheel',          1.0,  0, 0, ''),
(910066, 5, 6926, 'PD Rubble Heap',         1.0,  0, 0, ''),
-- scatter: flat things you walk over on open floor
(910070, 5, 7911, 'PD Rubble Low',          1.0,  0, 0, ''),
(910071, 5, 6736, 'PD Broken Boards',       0.7,  0, 0, ''),
(910072, 5,    9, 'PD Broken Barrel',       1.0,  0, 0, ''),
(910073, 5, 7311, 'PD Skeleton',            1.0,  0, 0, ''),
(910074, 5, 7312, 'PD Skeleton Alt',        1.0,  0, 0, ''),
(910075, 5,  293, 'PD Bone Pile',           1.0,  0, 0, ''),
(910076, 5, 7225, 'PD Coffin',              1.0,  0, 0, '');

DELETE FROM `pdungeon_decor_rules` WHERE `id` BETWEEN 4 AND 14;
INSERT INTO `pdungeon_decor_rules`
    (`id`,`theme`,`roleFilter`,`goEntry`,`placement`,`minPerBlock`,`maxPerBlock`,`weight`,`minSpacingYd`) VALUES
-- Wall feet. Weight is a SHARE of the block's wall-foot cells, competing with
-- the shipped torch (rule 1, weight 100) and brazier (rule 2, weight 40) - so
-- these are deliberately light: the room keeps its ring of torches and gains
-- furniture, not the other way round.
( 4, 0, 'room',              910050, 'wall_foot', 0, 2, 30, 8),
( 5, 0, 'room',              910051, 'wall_foot', 0, 2, 30, 8),
( 6, 0, 'room',              910054, 'wall_foot', 0, 1, 20, 8),
( 7, 0, 'room',              910055, 'wall_foot', 0, 1, 15, 8),
( 8, 0, 'room',              910056, 'wall_foot', 0, 1, 15, 8),
( 9, 0, 'corridor',          910052, 'wall_foot', 0, 1, 40, 8),
-- Corners. Two variants under one weight class is what stops a 15-room layout
-- from showing the same corner twelve times.
(10, 0, 'room',              910060, 'corner',    0, 2, 50, 8),
(11, 0, 'room',              910062, 'corner',    0, 2, 50, 8),
(12, 0, 'corridor_corner',   910063, 'corner',    0, 1, 60, 8),
-- Scatter. minSpacing 12 is one and a half cells, so scattered props never
-- clump; all three are flat enough to be walked over rather than walled by.
(13, 0, 'room',              910070, 'scatter',   0, 3, 60, 12),
(14, 0, 'room_boss',         910073, 'scatter',   1, 3, 80, 12);
```

- [ ] **Step 3: Verify every displayId before the file is committed**

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT displayId, COUNT(*) AS stock_rows, MIN(name) FROM gameobject_template WHERE displayId IN (288,275,7470,130,187,4391,234,5511,1868,1869,7680,6036,7526,8480,6926,7911,6736,9,7311,7312,293,7225) GROUP BY displayId ORDER BY displayId;"
```

Expected: a row for every id except **6926** (`PD Rubble Heap`), which Blizzard uses only as an
ADT/WMO doodad and which therefore has no stock `gameobject_template` row — it is verified from the
client DBC and the collision list instead. If any *other* id returns no row, do not ship it.

Two flags to carry into the operator document rather than hide:
- **910066 (display 6926)** has no stock GO row — weakest verification in the set.
- Six displays are also used by stock **quest objects** (130, 293, 7470 among them). Ours are type 5
  GENERIC and therefore not clickable, so this is visual confusion only, never a functional clash.
- Display **8026** (`BrokenBarrel01`) was deliberately **not** used: it has no entry in
  `GameObjectModels.dtree`, i.e. no collision model at all. `9` (`BrokenBarrel02`) does. Record this
  so nobody "fixes" it later.

- [ ] **Step 4: Apply and verify against a running server**

The SQL is applied by the updater on worldserver start. Restart the worldserver, then:

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT COUNT(*) FROM gameobject_template WHERE entry BETWEEN 910040 AND 910077; SELECT COUNT(*) FROM pdungeon_decor_rules;"
```

Expected: `30` (8 kit props + 22 clutter) and `14`. Then check the boot log for
`PDv2: loaded 14 decor rule(s) from pdungeon_decor_rules` and — critically — **no**
`asks for placement '...' which no planner implements` lines. One such line means Task 6's loader
change did not ship.

- [ ] **Step 5: Commit**

```bash
git add data/sql/db-world/mod_pdungeon_decor_clutter.sql data/sql/db-world/mod_pdungeon_templates_fix.sql
git commit -m "feat(decor): 22 clutter props in corners and on open floor; defuse the 910000-910099 range delete"
```

---

## Task 8: The critter placement planner

A critter must be planned in the engine-free layer for the same reason decor is: that is where it
can be tested. It is deliberately **not** a `PackRole` — the spawn draw's documented order must not
learn about ambient life.

**Files:**
- Modify: `src/generator/PDv2DecorPlan.h` (`CritterRule`, `CritterSpot`, `BuildCritterPlan`), `src/generator/PDv2DecorPlan.cpp` (implementation)
- Test: `tests/blockplan_harness.cpp` (new `--critter-batch`, or checks inside `RunDecorBatch`)

**Interfaces:**
- Consumes: `PDv2Classify`, `CollectScatter` from Task 4.
- Produces:
  - `struct CritterRule { int id; int theme; std::string roleFilter; int creatureEntry; int minPerBlock; int maxPerBlock; int weight; };`
  - `struct CritterSpot { int bx; int by; int ruleId; int creatureEntry; double u; double v; double orientation; };`
  - `std::vector<CritterSpot> BuildCritterPlan(BlockPlan const& plan, DecorMaskProvider const& maskFor, std::vector<CritterRule> const& rules, uint32_t layoutSeed);`
  - `uint32_t const PD_CRITTER_SEED_MIX = 0xC817E12Bu;` and `int const PD_CRITTER_MAX_SPOTS = 60;`

- [ ] **Step 1: Write the failing test**

```cpp
        // Critters live on open floor, never on the socket track, never on a
        // wall foot, and never off the walkable set - the same veto SplitOnDeath
        // applies to affix children, for the same reason: a gravity-less
        // creature past the platform edge hovers where nobody can reach it.
        bool CheckCritterSpots(BlockPlan const& plan,
                               std::vector<CritterSpot> const& spots,
                               std::string& why)
        {
            for (CritterSpot const& spot : spots)
            {
                PlacedBlock const* const block = plan.At(spot.bx, spot.by);
                if (!block)
                {
                    why = "a critter stands on no block of the plan";
                    return false;
                }
                uint8_t const* const mask = MaskFor(block->chunkId);
                if (!mask)
                {
                    why = "a critter was planned on a chunk with no walk mask";
                    return false;
                }
                std::string const classes = PDv2Classify(mask);

                int const row = static_cast<int>(std::floor(spot.u / PD_CELL_SIZE_YD));
                int const col = static_cast<int>(std::floor(spot.v / PD_CELL_SIZE_YD));
                if (row < 0 || col < 0 ||
                    row >= PD_CELLS_PER_BLOCK || col >= PD_CELLS_PER_BLOCK)
                {
                    why = "a critter is outside its own block";
                    return false;
                }
                if (row == PD_CELLS_PER_BLOCK / 2 || col == PD_CELLS_PER_BLOCK / 2)
                {
                    why = "a critter stands on the socket track";
                    return false;
                }
                if (classes[static_cast<size_t>(row) * PD_CELLS_PER_BLOCK + col] != 'W')
                {
                    why = "a critter is not on a walkable cell";
                    return false;
                }
            }
            if (spots.size() > static_cast<size_t>(PD_CRITTER_MAX_SPOTS))
            {
                why = "a layout exceeded the critter budget";
                return false;
            }
            return true;
        }
```

and, in `RunDecorBatch`'s per-seed body:

```cpp
                std::vector<CritterSpot> const critters = BuildCritterPlan(
                    plan, MaskFor, critterRules, plan.effectiveSeed);
                std::vector<CritterSpot> const crittersAgain = BuildCritterPlan(
                    plan, MaskFor, critterRules, plan.effectiveSeed);
                Check(SameCritters(critters, crittersAgain),
                      "two critter builds of the same plan differ", seed);
                Check(CheckCritterSpots(plan, critters, why),
                      why.empty() ? "critter placement broken" : why.c_str(), seed);
```

with a `critterRules` fixture beside `DecorFixture`:

```cpp
        std::vector<CritterRule> CritterFixture()
        {
            std::vector<CritterRule> rules;
            CritterRule r;
            r.id = 1; r.theme = 0; r.roleFilter = "room";
            r.creatureEntry = 32428; r.minPerBlock = 0; r.maxPerBlock = 2; r.weight = 100;
            rules.push_back(r);
            r.id = 2; r.theme = 0; r.roleFilter = "corridor";
            r.creatureEntry = 26525; r.minPerBlock = 0; r.maxPerBlock = 1; r.weight = 100;
            rules.push_back(r);
            return rules;
        }
```

and `SameCritters`, beside the existing `SameSpots`:

```cpp
        bool SameCritters(std::vector<CritterSpot> const& a,
                          std::vector<CritterSpot> const& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (a[i].bx != b[i].bx || a[i].by != b[i].by ||
                    a[i].ruleId != b[i].ruleId ||
                    a[i].creatureEntry != b[i].creatureEntry ||
                    a[i].u != b[i].u || a[i].v != b[i].v)
                {
                    return false;
                }
            }
            return true;
        }
```

Exact `double` comparison is correct here and matches `SameSpots`: the coordinates are computed by
the same code from the same integers, so two runs must produce bit-identical values or the planner
is not deterministic — which is the thing being tested.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
```

Expected: `error C2065: 'BuildCritterPlan': nicht deklarierter Bezeichner` and the same for
`CritterRule` / `CritterSpot` / `PD_CRITTER_MAX_SPOTS`.

- [ ] **Step 3: Write the implementation**

In `src/generator/PDv2DecorPlan.h`, after the decor declarations:

```cpp
    // Ambient life. A critter is NOT a pack role: it must never touch the
    // spawn draw's documented order, and it must never be counted by the run.
    // Its own stream, its own budget, its own table - the decor shape, applied
    // to creatures.
    uint32_t const PD_CRITTER_SEED_MIX = 0xC817E12Bu;
    int const PD_CRITTER_MAX_SPOTS = 60;

    // One row of `pdungeon_critter_rules`. `roleFilter` is the same prefix
    // match `DecorRule` uses.
    struct CritterRule
    {
        int         id = 0;
        int         theme = 0;
        std::string roleFilter;
        int         creatureEntry = 0;
        int         minPerBlock = 0;
        int         maxPerBlock = 0;
        int         weight = 1;
    };

    struct CritterSpot
    {
        int    bx = 0;
        int    by = 0;
        int    ruleId = 0;
        int    creatureEntry = 0;
        double u = 0.0;
        double v = 0.0;
        double orientation = 0.0;
    };

    // Critters for a layout, in the same fixed order BuildDecorPlan uses:
    // blocks in plan order, rules by ascending id, candidate cells row-major.
    // Placed on OPEN floor only (the scatter candidate set), so a critter never
    // stands inside a prop and never on the line every player walks.
    std::vector<CritterSpot> BuildCritterPlan(BlockPlan const& plan,
                                              DecorMaskProvider const& maskFor,
                                              std::vector<CritterRule> const& rules,
                                              uint32_t layoutSeed);
```

In `src/generator/PDv2DecorPlan.cpp`, at the end of the file, a function that mirrors
`BuildDecorPlan`'s structure exactly — including the warm-up draw, the ascending-id order, the
unconditional `want` draw per matching rule and the consume-on-rejection rule — but drawing from
`CollectScatter`'s pool and with no anchor or spacing gate:

```cpp
    std::vector<CritterSpot> BuildCritterPlan(BlockPlan const& plan,
                                              DecorMaskProvider const& maskFor,
                                              std::vector<CritterRule> const& rules,
                                              uint32_t layoutSeed)
    {
        std::vector<CritterSpot> out;

        std::vector<size_t> byId(rules.size());
        for (size_t i = 0; i < rules.size(); ++i) byId[i] = i;
        std::sort(byId.begin(), byId.end(), [&rules](size_t l, size_t r)
        {
            return rules[l].id < rules[r].id;
        });

        // Its OWN stream, mixed with its own constant: adding or removing a
        // critter rule must never move a single prop.
        PDRandom rng(layoutSeed ^ PD_CRITTER_SEED_MIX);
        rng.NextUInt32();

        std::vector<Candidate> pool;
        std::vector<size_t> matching;

        for (PlacedBlock const& block : plan.blocks)
        {
            uint8_t const* const mask = maskFor ? maskFor(block.chunkId) : nullptr;
            if (!mask)
            {
                continue;
            }

            std::string const classes = PDv2Classify(mask);
            char const* const roleName = BlockRoleName(block.role);
            CollectScatter(classes, pool);

            matching.clear();
            int totalWeight = 0;
            for (size_t const i : byId)
            {
                CritterRule const& rule = rules[i];
                if (rule.theme != 0 && rule.theme != plan.config.theme) continue;
                if (!DecorRoleMatches(rule.roleFilter, roleName)) continue;
                matching.push_back(i);
                totalWeight += (rule.weight > 0 ? rule.weight : 1);
            }
            if (matching.empty())
            {
                continue;
            }

            int const poolAtStart = static_cast<int>(pool.size());
            for (size_t const i : matching)
            {
                CritterRule const& rule = rules[i];
                int want = rng.UniformInt(rule.minPerBlock, rule.maxPerBlock);
                if (want < 0)
                {
                    want = 0;
                }
                int const w = rule.weight > 0 ? rule.weight : 1;
                int const share = (poolAtStart * w + totalWeight - 1) / totalWeight;
                int const take = want < share ? want : share;

                for (int n = 0; n < take && !pool.empty(); ++n)
                {
                    int const k =
                        rng.UniformInt(0, static_cast<int>(pool.size()) - 1);
                    Candidate const cand = pool[static_cast<size_t>(k)];
                    pool.erase(pool.begin() + k);

                    CritterSpot spot;
                    spot.bx = block.bx;
                    spot.by = block.by;
                    spot.ruleId = rule.id;
                    spot.creatureEntry = rule.creatureEntry;
                    spot.u = (static_cast<double>(cand.row) + 0.5) * PD_CELL_SIZE_YD;
                    spot.v = (static_cast<double>(cand.col) + 0.5) * PD_CELL_SIZE_YD;
                    spot.orientation = 0.0;
                    out.push_back(spot);
                }
            }
        }

        if (out.size() > static_cast<size_t>(PD_CRITTER_MAX_SPOTS))
        {
            out.resize(static_cast<size_t>(PD_CRITTER_MAX_SPOTS));
        }
        return out;
    }
```

`CollectScatter`, `DecorRoleMatches`, `BlockRoleName` and `Candidate` are all in the anonymous
namespace above; no forward declaration is needed if this function is placed after them.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp && pdblock.exe --decor-batch 3000
```

Expected: `ALL CHECKS PASS`, exit 0. **`props per layout` must be unchanged** from the Task 6 run —
the critter stream is separate, so decor cannot move. If it did, `BuildCritterPlan` is sharing an
`rng` it should not.

- [ ] **Step 5: Commit**

```bash
git add src/generator/PDv2DecorPlan.h src/generator/PDv2DecorPlan.cpp tests/blockplan_harness.cpp
git commit -m "feat(critters): an engine-free critter placement planner on its own stream"
```

---

## Task 9: The critter table, loader and info line

**Files:**
- Create: `data/sql/db-world/mod_pdungeon_critters.sql`
- Modify: `src/PDv2Mgr.h` (`_critterRules`, `CritterRules()`, `LoadCritterRules()`), `src/PDv2Mgr.cpp` (the loader, modelled on `LoadDecorRules` at `:530-577`), `src/PDWorldScript.cpp:45-81` (the `OnStartup` call), `src/PDv2Commands.cpp:197-222` (the row-count line)

**Interfaces:**
- Consumes: `CritterRule` from Task 8.
- Produces: `sPDv2Mgr->CritterRules()` returning `std::vector<CritterRule> const&`.

- [ ] **Step 1: Write the SQL**

Ten stock critters, all verified: `type = 8` CREATURE_TYPE_CRITTER, `unit_class 1`, neutral
faction templates (31 / 188 / 190, all with `ourMask = friendMask = enemyMask = 0`), `unit_flags`
free of `NOT_SELECTABLE` and `IMMUNE_TO_PC`, `lootid 0`, `skinloot 0`, `VerifiedBuild 12340`, and
every `CreatureDisplayID` present in `C:\wowstuff\dcore\Data\dbc\CreatureDisplayInfo.dbc`.

```sql
-- mod_pdungeon_critters.sql
--
-- Ambient life. A critter is NOT a pack member and NOT dungeon content: it is
-- excluded from the AI binder, from the level/HP scaling, and from every run
-- counter. See PDv2CreatureAIBinder and PDv2Scaling::IsDungeonCreature.
--
-- Every entry is a STOCK creature_template - nothing here is edited or added,
-- which is the same rule the packs follow. All ten are unit_class 1 on a
-- neutral faction template, carry no loot and no skinning, and give no XP.
--
-- Startup-only, like every other PDv2 table: a change needs a worldserver
-- restart, never `.reload config`.

CREATE TABLE IF NOT EXISTS `pdungeon_critter_rules` (
    `id` INT UNSIGNED NOT NULL,
    `theme` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `roleFilter` VARCHAR(32) NOT NULL DEFAULT '',
    `creatureEntry` INT UNSIGNED NOT NULL,
    `minPerBlock` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `maxPerBlock` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `weight` INT UNSIGNED NOT NULL DEFAULT 100,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `pdungeon_critter_rules` WHERE `id` BETWEEN 1 AND 10;
INSERT INTO `pdungeon_critter_rules`
    (`id`,`theme`,`roleFilter`,`creatureEntry`,`minPerBlock`,`maxPerBlock`,`weight`) VALUES
( 1, 0, 'room',            32428, 0, 2, 100),  -- Underbelly Rat
( 2, 0, 'room',            23086, 0, 2,  80),  -- Sewer Rat
( 3, 0, 'room',             2110, 0, 1,  60),  -- Black Rat
( 4, 0, 'corridor',        26525, 0, 2, 100),  -- Cockroach
( 5, 0, 'corridor_dead_end', 2110, 0, 1, 100), -- Black Rat, stub dressing
( 6, 0, 'room_boss',       26525, 0, 1,  60);  -- Cockroach
```

**Verify every entry before shipping** — the six above are a subset of the ten researched; confirm
each against the live DB and drop any that does not answer:

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT ct.entry, ct.name, ct.type, ct.unit_class, ct.faction, ct.unit_flags, ct.lootid, ct.skinloot, ctm.CreatureDisplayID FROM creature_template ct JOIN creature_template_model ctm ON ctm.CreatureID = ct.entry WHERE ct.entry IN (32428,23086,2110,26525) ORDER BY ct.entry;"
```

Expected: `type` 8 for all, `unit_class` 1, `faction` in (31,188,190), `lootid` and `skinloot` 0.
`23086` returns **two** model rows and `2110` has `flags_extra = 2` (CIVILIAN) — both are fine and
both are noted in the file's comments.

- [ ] **Step 2: Write the loader**

`src/PDv2Mgr.cpp`, a near-copy of `LoadDecorRules`:

```cpp
    void PDv2Mgr::LoadCritterRules()
    {
        _critterRules.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, theme, roleFilter, creatureEntry, minPerBlock, "
            "maxPerBlock, weight FROM pdungeon_critter_rules ORDER BY id");
        if (!result)
        {
            LOG_INFO(PD_LOG, "PDv2: pdungeon_critter_rules has no rows - dungeons "
                             "will be built without ambient life");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            CritterRule rule;
            rule.id = static_cast<int>(fields[0].Get<uint32>());
            rule.theme = fields[1].Get<uint8>();
            rule.roleFilter = fields[2].Get<std::string>();
            rule.creatureEntry = static_cast<int>(fields[3].Get<uint32>());
            rule.minPerBlock = fields[4].Get<uint8>();
            rule.maxPerBlock = fields[5].Get<uint8>();
            rule.weight = static_cast<int>(fields[6].Get<uint32>());
            _critterRules.push_back(std::move(rule));
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} critter rule(s) from pdungeon_critter_rules",
                 uint32(_critterRules.size()));
    }
```

`ORDER BY id` is not cosmetic: it is the iteration order `BuildCritterPlan`'s determinism rests on.

In `src/PDv2Mgr.h`, beside `_decorRules` (`:273`) and its accessor (`:260`):

In the **public** section, beside `DecorRules()`:

```cpp
        std::vector<CritterRule> const& CritterRules() const { return _critterRules; }
        void LoadCritterRules();
```

In the **private** section, beside `_decorRules`:

```cpp
        std::vector<CritterRule> _critterRules;
```

- [ ] **Step 3: Call it at startup, and report it in `info`**

`src/PDWorldScript.cpp`, inside `OnStartup`'s `if (sPDv2Mgr->IsEnabled())` block, after
`LoadDecorRules()`:

```cpp
            // Same startup-only rule as the decor rules: read-only after this
            // point, so a map thread can build a layout's ambient life without
            // a lock.
            sPDv2Mgr->LoadCritterRules();
```

`src/PDv2Commands.cpp`, in `HandleV2InfoCommand` beside the existing walk-mask and pack/affix lines:

```cpp
        handler->PSendSysMessage("  {} decor rule(s), {} critter rule(s) loaded",
                                 uint32(sPDv2Mgr->DecorRules().size()),
                                 uint32(sPDv2Mgr->CritterRules().size()));
```

This line is not optional. It is the whole reason the operator's ritual can catch "the SQL never
reached the DB" — a `0` here is the same class of signal as `0 walk mask(s)`.

- [ ] **Step 4: Build and verify**

A new `.cpp` was **not** added, so no cmake re-configure is needed; a plain build suffices.

```bash
cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m
```

Expected: `worldserver.vcxproj -> C:\wowstuff\dcore_bin\bin\RelWithDebInfo\worldserver.exe`, exit 0.

Back up and install, then restart and check:

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT COUNT(*) FROM pdungeon_critter_rules;"
```

Expected `6`, the boot log line `PDv2: loaded 6 critter rule(s)`, and `.pdungeon v2 info` printing
`14 decor rule(s), 6 critter rule(s) loaded`.

- [ ] **Step 5: Commit**

```bash
git add data/sql/db-world/mod_pdungeon_critters.sql src/PDv2Mgr.h src/PDv2Mgr.cpp src/PDWorldScript.cpp src/PDv2Commands.cpp
git commit -m "feat(critters): pdungeon_critter_rules, its startup loader and its info line"
```

---

## Task 10: Spawn critters, and keep them out of everything

Four gates decide whether a creature on map 760 is dungeon content, and **all four gate on the map,
not on the tag** — because the tag is written after `SummonCreature` returns and is invisible to
anything that runs inside it. A critter must fail two of them and never enter the run's counters.

The right shared shape is **`Creature::IsCritter()`** (`Unit.h:833`), which reads
`creature_template.type == CREATURE_TYPE_CRITTER` and, unlike the tag, is already true inside
`SummonCreature`. Do **not** reuse the void-zone rule (`UNIT_FLAG_NOT_SELECTABLE` →
`FACTION_FRIENDLY` 35): faction 35 is unattackable, and "you can kill one" is part of the
acceptance criterion.

**Files:**
- Modify: `src/PDv2CreatureAI.cpp:768-789` (binder), `src/PDv2Scaling.cpp:65-81` (`IsDungeonCreature`), `src/PDv2InstanceScript.h` (`SpawnCritters`, `_critterGuids`), `src/PDv2InstanceScript.cpp` (implementation + the `if (!_spawned)` guard at `:161-173` + `DespawnAll` at `:530-554`)

**Interfaces:**
- Consumes: `BuildCritterPlan` (Task 8), `sPDv2Mgr->CritterRules()` (Task 9).
- Produces: nothing other tasks consume.

- [ ] **Step 1: Exclude critters from the AI binder**

`src/PDv2CreatureAI.cpp`, in `PDv2CreatureAIBinder::GetCreatureAI`, after the owner check:

```cpp
        // Ambient life keeps its own AI. This binder hands PDv2MobAI to every
        // ownerless creature on the map, and PDv2MobAI::UpdateProximityAggro
        // calls AttackStart - which UnitAI::AttackStart (UnitAI.cpp:29-33)
        // does NOT gate on REACT_PASSIVE. So the passive react state every
        // critter gets from Creature::InitializeReactState would not protect
        // it: without this, a rat would hunt the player.
        if (creature->IsCritter())
        {
            return nullptr;
        }
```

- [ ] **Step 2: Exclude critters from the scaling**

`src/PDv2Scaling.cpp`, in `IsDungeonCreature`, before the final owner check:

```cpp
        // A critter is decoration, not content: it must not be forced to level
        // 80 and must not carry the difficulty health multiplier. This gate is
        // on the TEMPLATE type, which - unlike the spawn tag - is already true
        // inside SummonCreature, where OnBeforeCreatureSelectLevel runs.
        if (creature->IsCritter())
        {
            return false;
        }
```

The damage hooks need no change: `DungeonDamageMultX100` and `ReduceIncoming` already gate on the
spawn tag, which a critter never gets.

- [ ] **Step 3: Spawn them**

`src/PDv2InstanceScript.h`, beside `_decorGuids` (`:243`):

```cpp
        void SpawnCritters(BlockPlan const& plan);
        std::vector<ObjectGuid> _critterGuids;
```

`src/PDv2InstanceScript.cpp`, modelled on `SpawnDecor` (`:948-1005`):

```cpp
    void PDv2InstanceScript::SpawnCritters(BlockPlan const& plan)
    {
        if (!sPDv2Mgr->GetConfig().decorEnable)
        {
            return;
        }

        std::vector<CritterRule> const& rules = sPDv2Mgr->CritterRules();
        if (rules.empty())
        {
            return;
        }

        std::vector<CritterSpot> const spots = BuildCritterPlan(
            plan,
            [](int chunkId) { return sPDv2Mgr->WalkMaskFor(chunkId); },
            rules, plan.effectiveSeed);

        uint32 placed = 0;
        for (CritterSpot const& spot : spots)
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            sPDv2Mgr->BlockToWorld(spot.bx, spot.by, spot.u, spot.v, x, y, z);

            Creature* c = instance->SummonCreature(
                static_cast<uint32>(spot.creatureEntry),
                Position(x, y, z, static_cast<float>(spot.orientation)));
            if (!c)
            {
                continue;
            }

            // The same two lines every dungeon spawn gets, and for the same
            // reason: the server has no terrain on this map, so Map::GetHeight
            // answers INVALID_HEIGHT and a creature with gravity would fall
            // through the floor the client draws.
            c->SetHomePosition(x, y, z, static_cast<float>(spot.orientation));
            c->SetDisableGravity(true);

            // NO PDv2MobData tag, deliberately. The tag is the module's own
            // definition of "this is a dungeon mob": without it, OnMobDied
            // refuses this creature, no room counter moves, no affix touches
            // it, and the damage hooks leave it alone.
            _critterGuids.push_back(c->GetGUID());
            ++placed;
        }

        // `PD_LOG_DEBUG` does not exist - corrected 2026-09-01 during
        // implementation. The file's own convention for a per-layout summary is
        // LOG_INFO with the PD_LOG category, and the substring "placed N
        // critter(s)" is what Task 18's boot-log check greps for.
        LOG_INFO(PD_LOG, "PDv2: placed {} critter(s) of {} planned",
                 placed, uint32(spots.size()));
    }
```

**Keep critters out of the props — added 2026-09-01 after the Task 8 review.** A `scatter` decor
rule and a critter rule draw from the *same* candidate universe: both call `CollectScatter` on the
same block, on two RNG streams with no awareness of each other, and the critter planner has no
spacing or anchor gate at all. So a rubble pile and a rat can land on the exact same cell centre —
and with up to 82 props and 28 critters in one layout, they eventually will.

`SpawnDecor` runs first in the same guard, so `SpawnCritters` can simply skip a spot that collides.
Collect the decor spots' world positions as they are summoned and pass them in; drop any critter
within about 2 yd of one. Deterministic either way, because both plans are.

Call it from the `if (!_spawned)` guard, after `SpawnKitProps`, and tear it down in `DespawnAll`
beside `_decorGuids`:

```cpp
        for (ObjectGuid const& guid : _critterGuids)
        {
            if (Creature* c = instance->GetCreature(guid))
            {
                c->DespawnOrUnsummon();
            }
        }
        _critterGuids.clear();
```

Critters use `DespawnOrUnsummon`, not the `Delete()` the GameObjects get — they are creatures, and
`Delete()` is the GameObject teardown path.

- [ ] **Step 4: Build, deploy and verify in game**

```bash
cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m
```

Then install-copy, restart, `.pdungeon v2 gen`, enter, and check all five properties. A critter must:
be visible; **not** attack when you stand next to it; be **killable**; move **no** counter in the HUD
when killed; and give **no** XP. The last two are the ones that would break a run, and the boot log's
`placed N critter(s)` line is the offline half of the evidence.

- [ ] **Step 5: Commit**

```bash
git add src/PDv2CreatureAI.cpp src/PDv2Scaling.cpp src/PDv2InstanceScript.h src/PDv2InstanceScript.cpp
git commit -m "feat(critters): spawn ambient life, excluded from AI, scaling and every run counter"
```

---

## Task 11: Two new packs — undead and demons

**Files:**
- Create: `data/sql/db-world/mod_pdungeon_packs_undead_demon.sql`
- Create: `data/sql/db-world/mod_pdungeon_member_spells_undead_demon.sql`

**Interfaces:**
- Consumes: nothing.
- Produces: `pdungeon_packs` ids 4 and 5, 24 `pdungeon_pack_members` rows, 72 `pdungeon_member_spells` rows.

**Read `data/sql/db-world/mod_pdungeon_packs.sql` and `mod_pdungeon_member_spells.sql` in full
before writing a line.** Both carry the house rules this task must obey, and the second one's header
is the authority on the spell cadence.

- [ ] **Step 1: Write the packs and their members**

Pack 4 "Barrow Dead" (`type = 6` undead) and pack 5 "Legion Rift" (`type = 3` **demon** — `type = 5`
is GIANT, so a "type 5 demon pack" would be Northrend ice giants). Every entry is a stock template
below 41000, none in 84263–84290, none referenced by another module's SQL.

```sql
-- mod_pdungeon_packs_undead_demon.sql
--
-- Two themed packs. Pack ids start at 4 so the shipped mod_pdungeon_packs.sql's
-- `DELETE ... WHERE id BETWEEN 1 AND 3` can never touch them.
--
-- theme 0 = any look. level_min/level_max 80/80 is the BAND SELECTOR, not a
-- description: the default account band is [76,80] and every spawn is force-
-- levelled to 80 by OnBeforeCreatureSelectLevel regardless.
--
-- Not one creature_template row is edited or added. Every entry is stock, which
-- is the founding constraint of the whole three-table design.
--
-- role: 0 melee, 1 range, 2 boss.

DELETE FROM `pdungeon_pack_members` WHERE `packId` IN (4, 5);
DELETE FROM `pdungeon_packs` WHERE `id` IN (4, 5);

INSERT INTO `pdungeon_packs` (`id`,`name`,`theme`,`level_min`,`level_max`,`unlock_dlvl`,`enabled`) VALUES
(4, 'Barrow Dead', 0, 80, 80, 0, 1),
(5, 'Legion Rift', 0, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members` (`packId`,`entry`,`role`,`casterSpellId`,`weight`) VALUES
-- pack 4, undead (creature_template.type = 6)
(4, 31438, 0,     0, 100),  -- Shadow Vault Abomination   uc1  25200 hp
(4, 31721, 0,     0, 100),  -- Frostbrood Sentry          uc1  25200 hp
(4, 29974, 0,     0, 100),  -- Niffelem Forefather        uc1  15121 hp
(4, 28349, 0,     0, 100),  -- Risen Vrykul Berserker     uc1  12600 hp
(4, 30921, 0,     0, 100),  -- Skeletal Runesmith         uc1  12600 hp
(4, 31278, 0,     0, 100),  -- Ravenous Ghoul             uc1  12600 hp, 4 skins
(4, 31847, 0,     0, 100),  -- Scavenging Geist           uc1  12600 hp
(4, 30203, 1, 60015, 100),  -- Forgotten Depths High Priest uc2
(4, 32284, 1, 69211, 100),  -- Scourge Soulbinder         uc2, 3 skins
(4, 30482, 1, 22088, 100),  -- Wrathstrike Gargoyle       uc2
(4, 28350, 1, 60015, 100),  -- Risen Vrykul Magus         uc1 caster, free kit only
(4, 25352, 2,     0, 100),  -- Scourge Overlord           BOSS, rank 1, 252000 hp
-- pack 5, demons (creature_template.type = 3)
(5, 20427, 0,     0, 100),  -- Veneratus the Many         uc1  36860 hp
(5, 20403, 0,     0, 100),  -- Legion Destroyer           uc1  18430 hp
(5, 31528, 0,     0, 100),  -- Plagued Felbeast           uc1  12600 hp
(5, 23075, 0,     0, 100),  -- Legion Ring Infernal       uc1  11980 hp
(5, 19746, 0,     0, 100),  -- Pit Breaker                uc1   9215 hp
(5, 18862, 0,     0, 100),  -- Dread Overlord             uc1   9215 hp
(5, 18871, 0,     0, 100),  -- Voidlord                   uc1   9215 hp
(5, 31529, 1, 60015, 100),  -- Ravishing Betrayer         uc2
(5, 18859, 1, 22088, 100),  -- Wrath Priestess            uc2
(5, 18870, 1, 69211, 100),  -- Voidshrieker               uc2, mana 11982
(5, 24919, 1, 60015, 100),  -- Wrath Herald               uc2
(5, 29620, 2,     0, 100);  -- Dreadlord Mal'Ganis        BOSS, rank 1, 327600 hp
```

- [ ] **Step 2: Verify every member before shipping**

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT entry, name, type, unit_class, faction, rank, exp, HealthModifier, lootid FROM creature_template WHERE entry IN (31438,31721,29974,28349,30921,31278,31847,30203,32284,30482,28350,25352,20427,20403,31528,23075,19746,18862,18871,31529,18859,18870,24919,29620) ORDER BY entry;"
```

Check four things per row, each of which fails **silently in game** if wrong:

1. **`type`** is 6 for pack 4 and 3 for pack 5.
2. **`unit_class`** — 1 **and 4** both mean `basemana 0` at level 80, so every spell in that
   creature's kit must have a flat `ManaCost` of 0. A nonzero `ManaModifier` does **not** create
   mana; it multiplies a basemana of 0.
3. **`faction`** is hostile to players. `35, 190, 2050, 2070, 2081, 2084, 7` are **not** — a pack
   built from them spawns neutral mobs, `_roomAlive` never reaches zero and the room can never be
   cleared. Every faction here (14, 16, 21, 90, 974, 2068) was proven hostile against
   `FactionTemplate.dbc` field 5.
4. **`exp` + `unit_class`** — `creature_classlevelstats.basehp1` is literally **1** for classes 4
   and 8 at level 80, so an expansion-1 template with `unit_class 8` spawns with 1 HP. Neither pack
   contains one; keep it that way.

Also confirm no name ends in ` (1)`, ` (2)` or ` (3)` — the world DB is full of sniff duplicates that
carry the suffix into the displayed name.

- [ ] **Step 3: Write the combat kits**

72 rows, three per creature. Melee cadence: `cd 6000-8000 @ minDiff 1`, `8000-10000 @ 50`,
`8000-12000 @ 75`. Range: one slot-0 filler at `cd 0 @ 1` plus the two gated rows. Crowd control:
`cd 60000` and **never in position 1**.

Every filler chosen here has `ManaCost 0` **and** `ManaCostPercentage 0`. That is deliberate and it
fixes a defect in the *shipped* data: `Creature::Regenerate` gives an in-combat creature only
`Spirit/5 + 17` mana per interval, so the three shipped percentage-cost fillers (47809, 47857,
42842) empty their caster in six to nine casts and then fail `CheckPower` silently for the rest of
the fight. Do not copy that pattern.

The delete must be an **explicit entry list**. The entries run from 18859 to 32284, so a `BETWEEN`
would wipe an operator's rows for thousands of unrelated templates:

```sql
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (18859,18862,18870,18871,19746,20403,20427,23075,24919,25352,28349,28350,
  29620,29974,30203,30482,30921,31278,31438,31528,31529,31721,31847,32284);
```

Write the 72 rows from the verified table in the research record, in the same
`(entry, spellId, slot, cooldownMs, minDiff)` column order the shipped file uses.

- [ ] **Step 4: Verify every spell id against the DBC the server actually loads**

**`acore_world.spell_dbc` is NOT `Spell.dbc`.** It holds 5542 custom rows and contains none of these
ids; verifying against it returns "does not exist" for every correct answer. The authority is
`C:\wowstuff\dcore\Data\dbc\Spell.dbc` (55100 records). Note this server's `Spell.dbc` is patched —
ids in the `800xxx` range belong to other FL work and must never be picked up by a name search.

Write a guard script that checks all 72 proposed rows for: identity (the spell is what the comment
says), range ≥ 25.0 yd for every slot-0 filler, `ManaCost 0` on every 0-mana creature, CC at
`cd 60000` and never in position 1, no summon / knockback / jump effect, and no `(entry, spellId)`
primary-key duplicate. Run it against the 74 **live** rows as well — a guard that only passes on new
data is a rubber stamp.

Expected: `checked 72 proposed rows, 0 problems` and `checked 74 live rows, 0 problems`.

- [ ] **Step 5: Apply, restart and verify**

```bash
"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore -D acore_world -N -B -e "SELECT 'packs',COUNT(*) FROM pdungeon_packs UNION ALL SELECT 'members',COUNT(*) FROM pdungeon_pack_members UNION ALL SELECT 'spells',COUNT(*) FROM pdungeon_member_spells;"
```

Expected: packs `5`, members `28 + 24 = 52` (confirm the current 28 first), spells `74 + 72 = 146`.
Boot log: `5 pack(s)` and `146` member spells, and **no** `LOG_ERROR` about a role-1 member without
a filler or a duplicate slot-0 row.

- [ ] **Step 6: Tell the operator about the economy change**

Ten of the 22 new trash members carry small **native loot tables** (Frostweave Cloth, Fur Scraps,
Scourge Curio). The shipped 84263–84290 all have `lootid 0`, so PDv2 has never dropped anything but
its own mats and bonus roll. Both new bosses are deliberately `lootid 0`. This is a real economy
change and belongs in the Task 18 operator document **before** the run, not after.

- [ ] **Step 7: Commit**

```bash
git add data/sql/db-world/mod_pdungeon_packs_undead_demon.sql data/sql/db-world/mod_pdungeon_member_spells_undead_demon.sql
git commit -m "feat(packs): Barrow Dead and Legion Rift, 24 stock members with verified combat kits"
```

---

## Task 12: Give the spawn draw a testable seam

`SelectSpawns` has **no offline test of any kind** — neither harness links `PDv2PackMgr.cpp`, and it
cannot be linked as-is because it includes `DatabaseEnv.h`. Task 13 changes the draw order, which is
a documented determinism contract, with no net under it. This task builds the net first, as a
**pure refactor** whose output must be byte-identical.

**Files:**
- Create: `src/generator/PDv2PackDraw.h`, `src/generator/PDv2PackDraw.cpp`
- Modify: `src/PDv2PackMgr.h`, `src/PDv2PackMgr.cpp` (delegate), `tests/blockplan_harness.cpp` (new checks)

**Interfaces:**
- Consumes: `SpawnPick`, `SpawnSelectInputs` (already engine-free — `PDv2PackMgr.h` includes only `<cstdint>`, `<string>`, `<unordered_map>`, `<vector>`).
- Produces: `bool PDv2SelectSpawns(uint32_t seed, SpawnSelectInputs const& in, PackPools const& pools, std::vector<SpawnPick>& out);` — the whole draw, engine-free and linkable into the harness.

- [ ] **Step 1: Write the failing test**

A characterisation test: the draw for a fixed seed and a fixed pool must not move. Capture the
current output **before** the refactor and pin it.

```cpp
        // The draw order is a determinism CONTRACT: changing it re-rolls which
        // creatures every stored seed spawns. This pins it. If this check fails
        // after a change to PDv2PackDraw, that is the change being noticed, not
        // the test being wrong - update the pin deliberately, in the same commit
        // as the draw-order comment.
        bool CheckSpawnDrawPinned(std::string& why)
        {
            SpawnSelectInputs in = FixedSpawnInputs();
            std::vector<SpawnPick> picks;
            if (!PDv2SelectSpawns(12345u, in, FixedPackPools(), picks))
            {
                why = "the pinned spawn draw refused to select";
                return false;
            }

            std::string got;
            for (SpawnPick const& p : picks)
            {
                got += std::to_string(p.entry);
                got += ',';
            }
            if (got != PD_SPAWN_DRAW_PIN)
            {
                why = "the spawn draw moved: " + got;
                return false;
            }
            return true;
        }
```

The two fixtures it uses, beside `DecorFixture`. They are deliberately hand-written constants and
not read from the DB — the harness has no database, and a pin that moved when the world DB changed
would pin nothing:

```cpp
        // A fixed stand-in for the live pack tables: two packs, one with a
        // boss, enough members that a 5-trash room and a boss room both have
        // something to draw. The entries are the shipped stock ones so the pin
        // is readable by anyone who knows the dungeon.
        PackPools FixedPackPools()
        {
            PackPools pools;
            pools.melee  = { {1, 84264}, {1, 84265}, {1, 84266},
                             {2, 84267}, {2, 84268}, {2, 84269} };
            pools.caster = { {1, 84263}, {2, 84276} };
            pools.boss   = { {1, 84288}, {2, 84289} };
            return pools;
        }

        SpawnSelectInputs FixedSpawnInputs()
        {
            SpawnSelectInputs in;
            in.rooms = { { /*roomIndex*/ 0, /*isBossRoom*/ false },
                         { 1, false },
                         { 2, true } };
            in.spawnsPerRoom = 5;
            in.bossRoomAdds = 2;
            in.casterPct = 40;
            in.bandMin = 76;
            in.affixPct = 40;
            return in;
        }
```

Match the real field names of `SpawnSelectInputs` (`src/PDv2PackMgr.h:125`) and of the pool element
type when you write this — the shape above is the contract, the spelling comes from the header.

- [ ] **Step 2: Run it to verify it fails**

It will not compile — `PDv2SelectSpawns` does not exist. That is the red.

- [ ] **Step 3: Do the extraction**

`src/generator/PDv2PackDraw.h` — engine-free, `<cstdint>` / `<string>` / `<vector>` only, like the
rest of `src/generator/`:

```cpp
namespace PDungeon
{
    // The pools the draw reads, as a view over whatever holds them. Lifting
    // this out of PDv2PackMgr is what lets the harness link the draw: the
    // manager's own file includes DatabaseEnv.h and never will.
    struct PackPools
    {
        // (packId, creature entry) in the loader's own order.
        std::vector<PackMember> melee;
        std::vector<PackMember> caster;
        std::vector<PackMember> boss;

        // Packs holding at least one NON-BOSS member, ascending. The per-room
        // pack draw is uniform over this list; a pack that can fill nothing
        // must not be drawable, or a fifth of all rooms would come out empty.
        std::vector<int> trashPackIds;

        // The members of ONE pack, or an empty vector if it has none of that
        // role. Empty is the signal to fall back to the merged pool.
        std::vector<PackMember> const& meleeOf(int packId) const;
        std::vector<PackMember> const& casterOf(int packId) const;
    };

    // The whole spawn draw. Deterministic for a given (seed, inputs, pools):
    // same three on any compiler yields the same picks, which is why it lives
    // here and not beside the database.
    bool PDv2SelectSpawns(uint32_t seed, SpawnSelectInputs const& in,
                          PackPools const& pools, std::vector<SpawnPick>& out);
}
```

`SpawnPick`, `SpawnSelectInputs` and `PackMember` move out of `src/PDv2PackMgr.h` into this header
so both sides share one definition — `PDv2PackMgr.h` includes only `<cstdint>`, `<string>`,
`<unordered_map>` and `<vector>` today, so nothing engine-bound comes with them.

Move the body of `PDv2PackMgr::SelectSpawns` (`src/PDv2PackMgr.cpp:477-620`) verbatim into
`src/generator/PDv2PackDraw.cpp` as `PDv2SelectSpawns`, reading `pools` where it read members.
**Move the code; do not retype it.** Carry the draw-order comment block (`:477-496`) with it — that
comment is the contract's text, and Task 13 rewrites it in place.

`PDv2PackMgr::SelectSpawns` becomes a forwarder that builds a `PackPools` view of its members and
calls through. `trashPackIds` is computed once at load time, in `LoadFromDB`, not per draw.

`PDv2PackMgr::SelectSpawns` becomes a two-line forwarder that builds a `PackPools` view of its
members and calls through.

Then run the harness once, read the actual pin string out of the failure message, and paste it into
`PD_SPAWN_DRAW_PIN`. The pin is only meaningful because it is captured from the pre-refactor code
path: capture it by building the harness against the extracted function **before** Task 13 touches
the order.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp src\generator\PDv2PackDraw.cpp && pdblock.exe --batch 500 && pdblock.exe --decor-batch 3000
```

Expected: `ALL CHECKS PASS`. **Note the sixth source file** — every later harness build in this plan
and in Rounds B and C needs it.

A new `.cpp` was added to `src/generator/`, so the worldserver needs a **cmake re-configure**:

```bash
cmake -S C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk -B C:/wowstuff/dcore_bin && cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m
```

Expected: `-- Configuring done` / `-- Generating done`, then the build, then
`PDv2PackDraw.cpp` appearing in the compiled file list. If it does not appear, the configure did not
take and the change silently does not ship.

- [ ] **Step 5: Commit**

```bash
git add src/generator/PDv2PackDraw.h src/generator/PDv2PackDraw.cpp src/PDv2PackMgr.h src/PDv2PackMgr.cpp tests/blockplan_harness.cpp
git commit -m "refactor(packs): lift the spawn draw into the engine-free layer and pin it"
```

---

## Task 13: One theme per room

**Files:**
- Modify: `src/generator/PDv2PackDraw.cpp` (the new draw + the rewritten draw-order comment), `src/PDv2PackMgr.h:186-193` (the contradicting paragraph in the header)
- Test: `tests/blockplan_harness.cpp` (a coherence check + the updated pin)

**Interfaces:**
- Consumes: `PDv2SelectSpawns` from Task 12.
- Produces: the guarantee that all trash picks of one room share a pack id.

- [ ] **Step 1: Write the failing test**

```cpp
        // Every trash pick of a room comes from ONE pack. The boss slot is
        // exempt: it draws from the role-2 pool across all packs, so a room's
        // theme never constrains which boss can appear.
        bool CheckRoomThemeCoherent(std::vector<SpawnPick> const& picks,
                                    std::string& why)
        {
            std::map<int, int> packOfRoom;
            for (SpawnPick const& p : picks)
            {
                if (p.isRunBoss) continue;
                auto it = packOfRoom.find(p.roomIndex);
                if (it == packOfRoom.end())
                {
                    packOfRoom[p.roomIndex] = p.packId;
                    continue;
                }
                if (it->second != p.packId)
                {
                    why = "a room mixes two packs";
                    return false;
                }
            }
            return true;
        }
```

`SpawnPick` needs a `packId` field for this; add it in the same step and set it wherever a pick is
built.

- [ ] **Step 2: Run it to verify it fails**

Expected: `FAIL ...: a room mixes two packs` on almost every seed — today all unlocked packs are
merged into one melee pool and one caster pool and every slot rolls independently.

- [ ] **Step 3: Write the implementation**

Insert **one** draw at the top of the per-room loop body (`PDv2PackMgr.cpp:561-567` before the
extraction; the corresponding place in `PDv2PackDraw.cpp` after it), **before** the boss pick and
before the affix `Chance`.

```cpp
            // ---- draw 1 of the room: its pack -------------------------------
            // One theme per room. Drawn FIRST, before the boss pick and before
            // the affix roll, so a room's faction is decided before anything
            // that depends on it.
            //
            // Only packs with at least one NON-BOSS member are eligible, and
            // that filter is part of the contract, not an optimisation: with
            // the live tables a uniform draw would send a fifth of all rooms
            // to a pack that has no trash at all and could fill nothing.
            //
            // The draw is consumed UNCONDITIONALLY, hit or miss, exactly like
            // the dead affix chance below it - a room that falls back to the
            // merged pool must move the stream by as much as one that does
            // not, or the two desynchronise.
            int roomPackId = 0;
            {
                int const eligible = static_cast<int>(pools.trashPackIds.size());
                int const k = rng.UniformInt(0, eligible > 0 ? eligible - 1 : 0);
                if (eligible > 0)
                {
                    roomPackId = pools.trashPackIds[static_cast<size_t>(k)];
                }
            }
```

and, at each trash slot, prefer that pack and fall back **per slot**, never per room:

```cpp
                // Themed if the room's pack can fill this role, merged if it
                // cannot. Falling back per SLOT keeps the rest of the room
                // themed when a pack happens to have no caster.
                std::vector<PackMember> const& themed =
                    wantCaster ? pools.casterOf(roomPackId) : pools.meleeOf(roomPackId);
                std::vector<PackMember> const& pool =
                    themed.empty() ? (wantCaster ? pools.caster : pools.melee) : themed;
```

The boss slot is **not** changed: it keeps drawing from the role-2 pool across all packs, so a
room's theme never constrains which boss can appear.

- [ ] **Step 4: Rewrite the draw-order comment — in this commit**

Two texts contradict each other after this change and **both** must move:
`PDv2PackDraw.cpp`'s verbatim draw-order block (carried over from `PDv2PackMgr.cpp:477-496`) and the
paragraph in `PDv2PackMgr.h:186-193`. Add the new draw to the numbered order and say what it costs:
every stored seed spawns different creatures from here on.

Then re-capture `PD_SPAWN_DRAW_PIN` from the new output and update it **in the same commit** — the
pin moving is the point of this change, not a failure.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp src\generator\PDv2PackDraw.cpp && pdblock.exe --batch 500 && pdblock.exe --decor-batch 3000
```

Expected: `ALL CHECKS PASS` from both, exit 0, and no `a room mixes two packs` failures.

- [ ] **Step 6: Commit**

```bash
git add src/generator/PDv2PackDraw.cpp src/PDv2PackMgr.h tests/blockplan_harness.cpp
git commit -m "feat(packs): one pack per room, drawn before the boss and affix rolls"
```

---

## Task 14: Feather the floor's class border

The hard line between floor and wall band is not a defect that slipped through — it is **enforced**:
`51_texture_blockkit.py:753-759` fails the build if a wall or void pixel carries anything but rock
15, or a walkable pixel anything outside 2..10. That gate must be **rewritten, never deleted**; it
is the only check that stops the wall inheriting the floor texture, the defect that took rounds 3,
6, 7 and 11 to close.

**Files:**
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\51_texture_blockkit.py:392-407` (constants), `:409-450` (`paint_block_alpha`), `:731-766` (the verify gate)
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\52_punch_kit_holes.py:263-299` (MCSH)

**Interfaces:**
- Consumes: nothing.
- Produces: a kit whose walkable→wall alpha ramps over ~2 yd instead of stepping.

- [ ] **Step 1: Add the feather to the painter**

One alpha texel is 8.3333/16 = **0.5208 yd**, so a 4-texel ramp is 2.08 yd. Add beside the alpha
constants at `51:392-407`:

```python
FEATHER_TEXELS = 4                           # ~2.08 yd ramp


def _is_wall_class(cls):
    return cls == CLASS_WALL_L or cls == CLASS_VOID_L


def _class_at(classes, gu, gv):
    """Surface class of the block-global alpha pixel (gu, gv).

    Clamped at the block edge, which is deliberate: a block is authored with no
    knowledge of its neighbours, so 'outside' has to read as more of the same
    class or two blocks would disagree at their join.
    """
    cell_r = min(max(gu // PIXELS_PER_CELL, 0), WALK_CELLS_PER_BLOCK - 1)
    cell_c = min(max(gv // PIXELS_PER_CELL, 0), WALK_CELLS_PER_BLOCK - 1)
    return classes[cell_r * WALK_CELLS_PER_BLOCK + cell_c]


def _wall_distance(classes, gu, gv):
    """Chebyshev distance in texels from a WALKABLE pixel to the nearest
    wall/void pixel, capped at FEATHER_TEXELS (which means 'far enough').
    """
    for d in range(1, FEATHER_TEXELS + 1):
        for du in range(-d, d + 1):
            for dv in range(-d, d + 1):
                if max(abs(du), abs(dv)) != d:
                    continue
                if _is_wall_class(_class_at(classes, gu + du, gv + dv)):
                    return d
    return FEATHER_TEXELS
```

Then, in `paint_block_alpha`, keep the wall branch exactly as it is and ramp only the walkable one.
`value` is still "how much rock", so the ramp runs from the floor value up to `ALPHA_WALL`:

```python
            if _is_wall_class(cls):
                value = ALPHA_WALL
            else:
                # (the existing track / mottle branch, unchanged, producing `base`)
                base = ...
                # Feather. ONLY walkable pixels move: the wall's constant region
                # is never made thinner, because a thin filtered alpha band was
                # measured to lose the rock on the inner wall face (48:1410).
                #
                # The last row and column of each MCNK are pinned: flag 0x8000
                # is not set, so the client DUPLICATES them, and a gradient
                # there shows as a 0.52 yd discontinuity every 33.33 yd.
                on_mcnk_edge = (pr == PIXELS_PER_MCNK - 1 or
                                pc == PIXELS_PER_MCNK - 1)
                d = _wall_distance(classes, gu, gv)
                if d < FEATHER_TEXELS and not on_mcnk_edge:
                    t = d / float(FEATHER_TEXELS)       # 0 at the wall, 1 far
                    value = int(round(ALPHA_WALL + (base - ALPHA_WALL) * t))
                else:
                    value = base
```

The alternative to pinning the edge is to **set MCNK flag 0x8000** (`do_not_fix_alpha_map`), which
tells the client not to duplicate at all. That is the cleaner fix and it is size-preserving, but it
changes a header field the composer and the DLL have never seen set. **Pin the ring for this round**
and record the flag as the better long-term option.

Three constraints, each of which has already cost this project a round:

- **Feather inward from the wall only.** The wall texture sits on the un-alpha'd base layer
  precisely because a thin filtered alpha band was measured to lose the rock on the inner wall face
  (`48:1410-1430`). A ramp that eats into the wall makes its constant region thinner — the same
  failure mode.
- **Block-global coordinates only.** The two MCNKs of a block must agree at their shared border
  (`51:387-390`).
- **Keep the outermost pixel row and column of each block at their current class value.** MCNK flag
  `0x8000` is not set, so the client duplicates the last alpha row and column; a gradient there
  becomes a visible 0.52 yd discontinuity every 33.33 yd. This is also what makes two independently
  authored blocks agree at a join.

- [ ] **Step 2: Rewrite the verify gate — do not delete it**

`51:753-759` asserts `rock == ALPHA_WALL` on wall/void and `2 <= rock <= 10` on walkable. A feather
puts walkable pixels in `(10, 15]` and fails on thousands of them. The new form:

> a pixel **further than `FEATHER_TEXELS` from a class boundary** obeys the old rule; a pixel inside
> the band must lie between the two class values and must be monotone in its distance to the
> boundary.

State the invariant in the gate's own comment, as the old one does. A gate that only says "some
values are allowed" is not the gate that was there.

- [ ] **Step 3: Feather or drop MCSH**

`52_punch_kit_holes.py:263-299` writes a 1-bit wall shadow that steps on the **identical** class
line and samples the same 16-samples-per-walk-cell grid. If the alpha ramps and the shadow does not,
the shadow re-draws the hard border the feather just removed. Either feather it the same way or drop
it — the two must not disagree. Record which was chosen and why.

- [ ] **Step 4: Regenerate and verify**

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/51_texture_blockkit.py --dry-run
```

Expected: no `painted alpha pixel(s)/cells violate the class invariants` abort. `--dry-run` writes
nothing, so this is safe to iterate on.

Then the real pass, and the parity chain **in this order** — the oracle must run **after** the kit
and **before** the DLL suite:

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/51_texture_blockkit.py && python scripts/52_punch_kit_holes.py
```

- [ ] **Step 5: Commit**

The scripts live in `C:\wowstuff\ForgottenLand2.0\scripts`, which is **not a git repo**. Commit the
module-side record instead — the round document — and note the script versions there. This is a
standing weakness of the pipeline and is recorded in the baseline from Task 1.

---

## Task 15: Break the 8.33 yd ladder

The corridor floor carries a literal ruled grid at cell pitch. Measured on the live kit, it comes
from the corridor branch alone: the per-cell plus-sign gives two continuous 2.60 yd longitudinal
bands plus **eight cross-ties** at `gu = r*16 + 8`.

**Files:**
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\51_texture_blockkit.py:416` (`is_corridor`), `:428-429` (`edge_pass`), `:437-440` (the cross)

- [ ] **Step 1: Make the track continuous along the corridor axis**

Replace the per-cell cross

```python
                    cu = cell_r * PIXELS_PER_CELL + PIXELS_PER_CELL // 2
                    cv = cell_c * PIXELS_PER_CELL + PIXELS_PER_CELL // 2
                    d = min(abs(gu - cu), abs(gv - cv))
                    value = ALPHA_STRIPE_CORE if d <= 2 else ALPHA_STRIPE_RIM
```

with a distance measured to the **block's own centre line** instead of the cell's:

```python
                    # ONE continuous band per axis, measured to the BLOCK
                    # centre. The old form measured to the CELL centre, which
                    # is what drew a plus sign in every cell and therefore a
                    # ladder of cross-ties at cell pitch.
                    #
                    # This is continuous by construction: a north/south corridor
                    # is walkable on columns 3 and 4, i.e. gv in 48..79, so
                    # |gv - 64| is the band across the lane and |gu - 64| never
                    # wins - the result is one longitudinal stripe. A cross
                    # corridor gets both arms and they meet in the middle,
                    # which is a real crossing rather than an artifact.
                    centre = PIXELS_PER_MCNK      # 64 = the block centre
                    d = min(abs(gu - centre), abs(gv - centre))
                    base = ALPHA_STRIPE_CORE if d <= 2 else ALPHA_STRIPE_RIM
```

The stripe core stays 5 texels (2.60 yd) wide; widen it only after seeing it in game.

- [ ] **Step 2: Drop `edge_pass`**

Measured: `edge_pass` is **not** a block-boundary ring. The only walkable cells on a block boundary
are the socket throats — cells (0,3)+(0,4) for a room, plus (7,3)+(7,4) for a north/south corridor —
so dropping it removes a 2-cell stripe patch at each doorway and nothing else. That patch is one of
the things that reads as a seam.

```python
                # `is_corridor` alone decides the track now. edge_pass painted a
                # stripe patch into the two socket-throat cells of every ROOM,
                # which is a doorway, not a worn path - measured on the live kit,
                # those are the only walkable cells a block boundary has.
                if is_corridor:
                    <the block-centre band from step 1>
                else:
                    base = _mottle(gu, gv)
```

- [ ] **Step 3: Verify**

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/51_texture_blockkit.py --dry-run
```

Expected: the class-invariant gate (as rewritten in Task 14) still passes. The track values stay
inside the walkable band, so this change must **not** need another gate edit — if it does, the band
is being widened by accident.

- [ ] **Step 4: Record in the round document**

Same as Task 14 step 5: the scripts are not versioned, so the round document is the record.

---

## Task 16: A floor texture per room role

**Files:**
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\48_gen_t1_blockkit.py:1379` (`TEXTURE_SUPERSET`), `:1431` (`THEME_TEXTURE_LAYERS`)
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\51_texture_blockkit.py:886-890` (`tex_ids_for`)

- [ ] **Step 1: Pick the tiles, and measure them first**

This is cheap because a kit file **is** exactly one `(theme, role, mask, alt)` variant:
`retexture_adt` builds ONE MCLY per file and applies it to all 256 MCNKs, so a per-role
`tex_ids_for` is enough. Only the 4-byte texId per MCLY moves.

Candidates, all verified to exist **with** an `_s.blp` sibling, so **no new BLP and no patch-9**:

| Role | Theme 2 (city) | Theme 1 (mine) |
|---|---|---|
| Room | `TILESET\DUSKWOOD\DuskwoodCobblestone2.blp` (**keep** — the live look) | `TILESET\Aerie Peaks\AeriePeaksRockyMudBase.blp` (keep) |
| Corridor | `TILESET\Generic\ICB_CobbleStone_B.blp` | `TILESET\Aerie Peaks\AeriePeaksRockRoadBase.blp` |
| Boss room | `TILESET\DUSKWOOD\DuskwoodCobblestone.blp` | `TILESET\Aerie Peaks\AeriePeaksGravelNeedlesBase.blp` |

**Run the axis-imbalance measurement from `100_stage_city_wall_texture.py:44-50` on each new tile
before committing it.** Terrain UV is axis-aligned to the tile and never to the street, so a
directional texture reads rotated on half the surfaces — the live street tile was chosen at 29.7 %
axis-aligned for exactly that reason. Existence and `_s` are verified; **axis imbalance is not**.

- [ ] **Step 2: Keep layer 0 alone**

The wall texture must remain layer 0 of every role in both themes. Moving the floor onto the
un-alpha'd base returns the round-11 "floor tiles on the wall" defect.

- [ ] **Step 3: Verify**

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/48_gen_t1_blockkit.py && python scripts/51_texture_blockkit.py --dry-run
```

Expected: MTEX grows from 167 bytes uniformly in all 215 ADTs, and the gate passes. Uniform growth
is what keeps the composer's fixed-size assumption valid — `composer.cpp:271` requires only that
every kit chunk equal the void base's size.

**`KIT_VERSION` does not move**: `pdungeon_chunk_meta`'s columns are chunkId / kitVersion / theme /
role / socketMask / walkMask / anchors, and a texture pass touches none of them.

---

## Task 17: Detail doodads on the floor

**Files:**
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\51_texture_blockkit.py:119` + `build_mcly` (the effect id)
- Modify: `C:\wowstuff\ForgottenLand2.0\scripts\52_punch_kit_holes.py` (the MCNK header 0x40 write)

- [ ] **Step 1: Set the effect id per role**

`build_mcly` packs `struct.pack("<3Ii", texId, flags, ofsInMCAL, MCLY_EFFECT_NONE)` — the effect id
is a signed int32 at byte 12 of each 16-byte entry. Point layer 1's at a stock
`GroundEffectTexture.dbc` row. **No DBC edit and no patch-9**: three suitable rows already ship, and
their doodad models resolve to real files in `common-2.MPQ`.

| Role | Row | What it is |
|---|---|---|
| Corridor | 598 | Deadwind gravel, density 2, TerrainType 2 (Stone) — sparse, grey, same tileset family as the mine wall |
| Room | 22796 | Rubble + Desolace bone, density 8 — three parts rubble to one part bone |
| Boss room | 1741 | Plaguelands bones, density 2 — Blizzard's own "dead ground with corpses" pairing |

- [ ] **Step 2: Write the MCNK header's low-quality texture map — the gate that decides whether any of it shows**

Without this step **nothing renders and the task silently does nothing.** The client picks the
ground-effect row from the 16 bytes at MCNK payload offset **0x40**, two bits per 8×8 sub-cell,
naming the dominant layer. Every kit MCNK has it **all-zero** — measured on all 860 block MCNKs of
t1b-v28, inherited from the donor tile. Layer 0 is the **wall** texture, so:

- an effect id on layer 1 alone renders **nothing**;
- one on layer 0 scatters doodads over floor **and** wall band alike.

Getting doodads on the floor only means writing 0x40 to name layer 1 in walkable sub-cells. That
belongs in `52_punch_kit_holes.py`, which already writes header 0x3C and owns the same class grid
and the same measured `transposed` axis flag — **not** in 51, whose step-[6] scope check at `:164`
would reject it.

The 0x40 semantics were verified against a stock tile: on `PVPZone02_30_32` MCNK[0], every sub-cell
whose predicted texture names layer 1 has a layer-1 mean alpha ≥ 7.1/15, and every cell naming
layer 0 has ≤ 7.0.

- [ ] **Step 3: Verify**

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/52_punch_kit_holes.py --dry-run
```

Expected: no abort, and the hole/skirt output unchanged from the Task 1 baseline — 0x40 is a
different header field and must not disturb the holes.

This task is **kit-only**: a new `t1b-vNN`, a `KitDir` flip and a cold client restart. No worldserver
restart, no MPQ work.

---

## Task 18: Ship it

**Files:**
- Create: `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde21.md`
- Modify: `C:\wowstuff\FL2-Client\FLStream.ini` (the v29 paragraph and the `KitDir` flip)
- Modify: `share-public/docs/World of Warcraft/12-server-todo.md`, `forgotten-land/15-host-migration-log.md`, `claude_log.md`

- [ ] **Step 1: Regenerate the kit, in the load-bearing order**

48 rewrites `kit_meta.json` from scratch and 51/52 refresh the per-file hashes the composers check,
so the order is not a preference:

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/48_gen_t1_blockkit.py && python scripts/51_texture_blockkit.py && python scripts/52_punch_kit_holes.py && python scripts/47_pd_map760_rows.py --check
```

Expected from the last one: `client vs server AGREE, field for field`. Anything else means the DBCs
and `mod_pdungeon_map760.sql` have drifted and the round stops here.

**No `30_build_hot_dbc_patch.py` run and no new patch-9** — Round A introduces neither a new BLP nor
a new DBC row. If a tuning pass added one after all, run it and take a fresh `pre_*` backup first.

- [ ] **Step 2: Prove the kit is deterministic**

Hash the 217 files, run the chain again, hash again, and diff. Expected: `FC: keine Unterschiede
gefunden` across all 217. Any difference means a non-deterministic draw entered the kit generator.

- [ ] **Step 3: Run every gate, in the order that makes parity meaningful**

The oracle's composed tile currently **predates** the staging kit it is the oracle for by ~90
seconds. It happens to still be byte-identical, but that is luck. Run script 49 **after** the kit
chain and **before** the DLL suite, every round:

```bash
cd /c/Users/Anwender/Documents/GitHub/azerothcore-wotlk/modules/mod-procedural-dungeon && pdblock.exe --batch 500 && pdblock.exe --decor-batch 3000 && pdblock.exe --roomcap 3000 && pdblock.exe --manifest 884213 C:\wowstuff\ForgottenLand2.0\output\pd_live_manifest.txt
```

```bash
cd /c/wowstuff/ForgottenLand2.0 && python scripts/49_pd_compose_blocks.py --manifest C:\wowstuff\ForgottenLand2.0\output\pd_live_manifest.txt
```

```bash
"/c/Users/Anwender/Documents/GitHub/fl-stream-client/build/Release/flstream_tests.exe"
```

**The DLL suite's exit code is not the acceptance criterion.** `TestComposerAgainstOracle` returns
*before* printing its section header if `output\pd_compose_blocks\manifest.txt` is absent — the
parity leg vanishes with no output and the suite still prints `ALL TESTS PASSED`. Confirm the
literal line `composer vs the Python oracle` **and both** `byte-identical to the oracle` lines.

The same shape applies to `pdblock`: a batch that prints no `214 walk mask(s)` line is a weaker gate
wearing a passing result.

- [ ] **Step 4: Build and install the worldserver**

Tasks 9, 10, 12 and 13 changed `src/`, and Task 12 **added** a file, so configure then build:

```bash
cmake -S C:/Users/Anwender/Documents/GitHub/azerothcore-wotlk -B C:/wowstuff/dcore_bin && cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m
```

Back up, install, hash:

```bash
cp /c/wowstuff/dcore/worldserver.exe /c/wowstuff/dcore/worldserver.exe.pre_roundA_20260901
cp /c/wowstuff/dcore_bin/bin/RelWithDebInfo/worldserver.exe /c/wowstuff/dcore/worldserver.exe
certutil -hashfile C:\wowstuff\dcore\worldserver.exe MD5
```

The copy is **not optional**: the server runs as `C:\wowstuff\dcore\worldserver.exe` with `dcore` as
its working directory, and a freshly built exe started in place asserts at `OpenSSLCrypto.cpp:36`.

- [ ] **Step 5: Deploy the kit**

No script does this — `grep FLPD-Kit scripts/` returns nothing. It is a manual copy, and the
**client must be closed**.

```bash
robocopy C:\wowstuff\ForgottenLand2.0\output\pd_block_kit\FLStream\chunks\t1b C:\wowstuff\FL2-Client\Data\FLPD-Kit\t1b-v29 /MIR
```

Expected: 217 files, 0 mismatches. Then set `KitDir=Data\FLPD-Kit\t1b-v29` in `FLStream.ini` **and**
append the v29 paragraph to the ladder in the same edit. The flip without the paragraph is exactly
how v25–v28 lost theirs.

- [ ] **Step 6: Write the operator round document**

`C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde21.md`, in the fixed seven-part shape the
runde14–16 documents share:

1. **State table** — Kit `t1b-v29` · patch-9 md5 (**unchanged**, `97fc5cd5`) · `worldserver.exe`
   **new** · `FLStream.dll` unchanged · SQL `kitVersion 23` (**unchanged**). Note explicitly that
   `KIT_VERSION` (23) and the kit directory number (v29) are five apart and are different counters —
   the state table's two rows must not be filled from the same number.
2. **Start** — restart worldserver → cold-restart client → `.gm on` → `.pdungeon v2 info`, which
   must say **214 walk mask(s)**, **5 pack(s)**, and the new **14 decor rule(s), 6 critter rule(s)**
   → `.pdungeon v2 gen` → wait for READY → `.gm off` → `/pd` → Enter.
3. **One lettered section per ask**, each with **Erwartet** and **Falls doch**:
   A the floor's borders read as transitions, not cuts · B no ruled grid on corridor floors ·
   C room, corridor and boss room are visibly different surfaces · D ground detail is present ·
   E crates and furniture in corners and on open floor · F critters are present, ignorable, killable,
   and move no counter · G each room is one faction of undead or demons.
4. **Mine regression probe** — `.pdungeon v2 gen 0 1`.
5. **What is still open** — the round-16 leftovers this round does not touch: 245 of 694 buildings
   show a second, never-checked side; the 200 outward-pointing wall corners are bare; the honest
   visible-wall figure is 7.9 % by eye-ray. Plus the two economy notes: **native loot now drops**
   from ten of the new trash members, and both new bosses are deliberately loot-free.
6. **Rollback table** — kit back to `t1b-v28`, worldserver back to `worldserver.exe.pre_roundA_20260901`,
   the four pipeline scripts back to their `.pre_roundA_20260901` copies, SQL rows deletable by
   their own id ranges. Carry the **Verboten** row forward from runde16 (a v5+ kit must never run
   under the pre-Phase-3 DLL), and add the honest note that
   `FLStream.dll.pre_phase3_20260830` **does not exist on this box**, so the prohibition is a rule
   without an enforceable rollback target.
7. **Rückmeldung** — what to report and how.

- [ ] **Step 7: Sync the vault, in the same commit as the work**

Three standing duties, none optional:

- `share-public/docs/World of Warcraft/12-server-todo.md` — update the PDv2 row; add the deferred
  `mod_pdungeon_templates.sql` range-delete narrowing as its own open item.
- `share-public/docs/World of Warcraft/forgotten-land/15-host-migration-log.md` — a MIG entry: this
  is a host-relevant change (new SQL files, a new worldserver binary, a new client kit).
- `share-public/claude_log.md` — one entry at the **very end** of the file; it is ascending.

- [ ] **Step 8: Commit**

```bash
git add -A && git commit -m "chore(round-a): kit v29, operator round document, vault sync"
```

---

## Self-review notes

**Spec coverage.** A1.1 → Task 14 · A1.2 → Task 15 · A1.3 → Task 16 · A1.4 → Task 17 · A2 →
Tasks 2–7 · A3 → Tasks 8–10 · A4 → Task 11 · A5 → Tasks 12–13 · the ladder back-fill and the
round document → Tasks 1 and 18. No Round A spec item is without a task.

**Deliberately deferred, and recorded rather than hidden:**

- `mod_pdungeon_templates.sql`'s `DELETE ... BETWEEN 910000 AND 910099` is **defused** by a new file
  (Task 7), not **fixed**. Narrowing it to 910000-910033 is correct but the updater will not
  re-apply an edited base file to a database that already has it, so it needs a migration of its
  own. It goes in the global queue.
- The kit pipeline scripts live in a directory that is **not a git repo**, so Tasks 14–17 have no
  version control. Their record is the round document and the `FLStream.ini` ladder. This is a
  standing weakness, not a Round A decision.
- `t1b-v28`'s contents remain unreconstructed. Task 1 documents the gap honestly rather than
  inventing a paragraph for it.
