# PDv2 Round F / F1 — finish the Mine: player theme choice, packs per theme, mine decor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Tasks 1–3 are independent (different files) and run in parallel, one Opus agent each; the planner reviews (Sonnet, short), does the docs, the install and the deploy.

**Goal:** the mine look that already ships in kit `t1b-v39` (theme 1) becomes a player choice on the gen panel, draws its own creature packs (Defias, Stonevault, Dark Iron) and gets mine decor (glowing crystals, braziers, mining clutter). No client patch: the kit is untouched, the panel row arrives over AIO.

**Spec:** `docs/superpowers/specs/2026-09-11-pdv2-round-f-themes-design.md` (D1–D6). **Recon:** `.superpowers/sdd/roundf-recon-a.md` (theme plumbing, `PDv2PackMgr::LoadFromDB(theme)` loads once by the CONFIG theme, decor rule filter `PDv2DecorPlan.cpp:564,744`, free GO ids), `roundf-recon-b.md` (display ids with dtree + stock precedent, rosters). Precedents in this repo: WP9 (`cfg_stat_profile` knob, `SET statprofile`, `C` fields 25/26, the panel's word-label slider), WP3 (`diffCap` as a wire hint), `mod_pdungeon_packs.sql` (pack shape), `mod_pdungeon_decor.sql` + `mod_pdungeon_decor_clutter.sql` (rule shape), `mod_pdungeon_templates_fix.sql` (GO rows + the three-way check comments).

---

## Global Constraints

- Branch: `mod-procedural-dungeon` — cut **`claude/pdv2-round-f-63ac9a2a`** from `main` (`9d89fcc`+); Conventional Commits; comments say WHY in the house voice.
- **Theme knob semantics (D1):** `cfg_theme` 0 = follow `V2.Theme`, else the theme id; valid ids = the themes present in the loaded chunk meta (`themeMax` = the highest); the knob changes the NEXT Generate only; `pdungeon_account.theme` (the stored layout's theme) is untouched.
- **Wire (append-only):** `C` gains field 27 `cfgTheme`, 28 `themeMax`, BEFORE the free-text tail; `CFG_FIELDS` 26 → **28**; verb `SET theme n` (refused with a `PDv2Debug()` line when `n > themeMax` or unknown; 0 always allowed).
- **Pack rule (D2):** the manager holds every enabled pack (any theme) with its `theme`; `SelectSpawns` takes the plan's theme; with `V2.Packs.ThemeExclusive = 1` (default) and ≥ 1 themed pack that has a usable non-boss member in the band → draw from themed packs only; else the theme-0 packs. The harness fixtures are theme 0, so every `PD_SPAWN_DRAW_PIN`/`PD_BOSS_*` pin must hold — if one moves, STOP and report.
- **Data (D3/D4):** new SQL files with entry-exact DELETEs (`mod_pdungeon_packs_mine.sql` packs 9-11 + members; `mod_pdungeon_decor_mine.sql` rules 4+); GO rows 910077-910086 in `mod_pdungeon_templates_fix.sql` (its documented home; **never** `mod_pdungeon_templates.sql`/`phase2.sql`); every display gets the three-way check written down (DBC model path, `GameObjectModels.dtree` bbox, stock `gameobject_template` precedent count).
- Gates: PD C++ → `codestyle-cpp.py` clean, `pdblock --batch 500` / `--decor-batch 3000` = 0 failures with pins held, staged build `cmake --build C:\wowstuff\dcore_bin --config RelWithDebInfo --target worldserver --parallel 4` (never unbounded; only Task 1 builds); Lua → `C:\wowstuff\dcore\lua52_compiler.exe -p`; SQL → read-only checks via `docs/superpowers/notes/round-e-loot-sql/q.sh`. No install/deploy (planner).

---

### Task 1: engine — the theme knob, the wire, packs per theme

**Files:** `src/PDv2Mgr.h/.cpp` (`PDv2AccountState`, `LoadAccountState` SELECT tail, `SetAccountCfg` clamp, `SaveAccountCfg`, `SavePlanToDB` INSERT half, `LoadConfig` for `V2.Packs.ThemeExclusive`, `ThemeMax()` from the loaded chunk meta), `src/PDv2UILink.cpp` (`SET theme`, `SendCfg` fields 27/28, the `GEN` path passes `account.cfgTheme` as `themeOverride`), `src/PDv2PackMgr.h/.cpp` (`LoadFromDB()` without the theme filter, `Pack::theme`, `SpawnSelectInputs::theme`, the exclusivity rule in `SelectSpawns`, boot log per theme), `src/PDv2InstanceScript.cpp` (the `SelectSpawns` call sites pass `plan.config.theme`), `tests/blockplan_harness.cpp` (fixtures gain `theme 0`; a new check `RunThemedPackChecks`: a fixture with one theme-1 pack + two theme-0 packs draws only from the theme-1 pack for theme 1 and only from theme-0 for theme 2; with `ThemeExclusive = 0` all three), **new** `data/sql/db-characters/mod_pdungeon_account_theme.sql` (`cfg_theme TINYINT UNSIGNED NOT NULL DEFAULT 0`, the `…_diffcap.sql` shape: information_schema guard, PREPARE/EXECUTE, no AFTER), `conf/mod_procedural_dungeon.conf.dist` (`V2.Packs.ThemeExclusive = 1` block; the `V2.Theme` block gains a sentence that the panel knob overrides it per account).

```cpp
// PDv2AccountState
uint8_t cfgTheme = 0;                       // cfg_theme: 0 = follow V2.Theme, else the theme id for the next Generate
// PDv2Mgr
int ThemeMax() const;                       // highest theme id present in the loaded chunk meta (2 today)
bool HasTheme(int theme) const;             // chunk meta rows exist for it
// PDv2PackMgr
struct Pack { …; int theme = 0; };
struct SpawnSelectInputs { …; int theme = 0; };
// SelectSpawns: candidates = ThemeExclusive && any usable themed pack ? themed only : theme-0 packs (today's set)
```
- [ ] Step 1 SQL + state + load/clamp/save + `ThemeMax/HasTheme`. · [ ] Step 2 `SET theme`, `SendCfg` 27/28, `GEN` override. · [ ] Step 3 pack pool per theme + rule + conf + boot log (`loaded N pack(s) … theme 0: a, theme 1: b`). · [ ] Step 4 harness fixtures + `RunThemedPackChecks`, `--batch 500` + `--decor-batch 3000` pins held. · [ ] Step 5 codestyle, build, commit `feat(theme): player theme choice (cfg_theme), packs drawn per theme, ThemeExclusive`.

### Task 2: panel — the theme row

**File:** `lua_scripts/flpdui.lua` only. `CFG_FIELDS = 28`; `ParseCfg` `cfgTheme = f[27]`, `themeMax = f[28]`; a slider row **above the caster slider** (first knob after rooms/difficulty — the look is chosen before the numbers), labels from a static table `THEME_NAMES = {[0]="Standard", [1]="Mine", [2]="Stadt", [3]="Wald"}` (unknown id → the number), `min 0, max themeMax, step 1`, `setKey "theme"`, always shown (no gating), `ApplySlider` inside the `setLoop` guard; the hint line under it: "Gilt für das nächste Generieren" in the addon's language (check what language the panel uses today and match it). Height arithmetic documented like WP9. Syntax gate `lua52_compiler.exe -p`. Commit `feat(ui): theme row on the gen panel`.

### Task 3: data — mine packs, mine decor, the ten GO rows

**Files:** **new** `data/sql/db-world/mod_pdungeon_packs_mine.sql` (packs 9 Defias Miners, 10 Stonevault, 11 Dark Iron Forge — `theme 1`, `level_min/max 80`, `unlock_dlvl 0`, `enabled 1`; members per spec D3 with `role` 0/1/2, `casterSpellId` for the casters chosen the way `mod_pdungeon_packs.sql` chose them (read its comments; a caster needs a spell the PD AI casts — take the existing packs' precedent per class, e.g. the Shadowfang/Scholomance caster rows), `weight 100`; entry-exact DELETEs on both tables), **new** `data/sql/db-world/mod_pdungeon_decor_mine.sql` (rules with `theme 1`: crystals 910077/910078/910079 `wall_foot` room 1..3 and `corner` room 0..1, brazier 910080 `wall_foot` room_boss 1..2, clutter 910081-910086 `scatter` room 0..2 and corridor 0..1; weights so crystals dominate; entry-exact DELETE of the new rule ids), `data/sql/db-world/mod_pdungeon_templates_fix.sql` (GO rows 910077 Cave Crystal 219, 910078 Cave Crystal 244, 910079 Cave Crystal 2592, 910080 Dark Iron Brazier 3411, 910081 Lumber Pile Small 1108, 910082 Lumber Pile Large 1109, 910083 Ore Crates 36, 910084 Powder Keg 436, 910085 Wheelbarrow 215, 910086 Ore Cart 7997 — type 5 GENERIC, size 1.0 unless the dtree bbox says the model is tiny/huge (write the bbox), the DELETE list extended, the free-id comment updated to 910048-49, 910069, 910087-910099 = 16).
- Verify before writing: every display resolves in `C:\wowstuff\dcore\Data\dbc\GameObjectDisplayInfo.dbc` (16-byte header WDBC; 19 fields; the model path string), has a `GameObjectModels.dtree` record (`C:\wowstuff\dcore\Data\vmaps\GameObjectModels.dtree`, 8-byte `VMAP_4.8` magic then `uint32 displayId, uint8 isWmo, uint32 nameLen, name, 6 floats`), and its stock precedent count (read-only SQL). Put the three numbers per display into the SQL comment (the fix file's existing rows show the format).
- Also verify each pack member exists in `creature_template` with `lootid`/`unit_class`; note `CreatureImmunitiesId` for the bosses.
- Commit `feat(data): mine packs (Defias, Stonevault, Dark Iron) and mine decor - crystals, brazier, mining clutter (GO 910077-910086)`.

### Task 4 (planner): reviews, docs (`CLAUDE.md`, `README.md`, spec status, 06-custom-ids GO rows, 09 `cfg_theme`), install (backup `worldserver.exe.pre_roundF_f1_<date>`), conf key appended, restart, boot log (packs per theme, decor rules, SQL applied), deploy `flpdui.lua` to `dcore\lua_scripts\ProcDungeon\`, runde-checklist `tools/pd_testlauf_runde32.md` §1 (F1), vault, memory. Host later with F2 or on its own MIG.
