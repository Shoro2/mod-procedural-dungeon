# PDv2 Round E / WP9 — the stat-profile loot filter, unlocked by a Forgotten Talents legendary node, chosen in the /pd panel — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Tasks 1–3 are independent (different repos/files) and run in parallel, one Opus agent each; the planner reviews (Sonnet, short), does the docs, the install and the deploys.

**Goal (operator, runde31 §3 "Bekannte Kanten"):** today gear rolls filter class, faction, armour type and weapon type only — a spell-power ring can drop for a rogue. A player who owns the new legendary node *Discerning Eye* picks a **stat profile** in the `/pd` panel (Off / Strength / Agility / Caster), stored per account; every gear roll in the depths (caches, boss corpses, the final cache) then also filters by that profile. Without the node the panel row is hidden and the server refuses the setting.

**Architecture:** one new pure predicate `FitsProfileRaw(statMask, profile)` in the engine-free `PDv2GameMath.h` (harness-pinned) fed by a per-row `statMask` that `PDv2LootMgr::Load()` precomputes from `ItemTemplate::ItemStat[]`; `Collect` applies it after the class/race test with a **two-stage fallback** (drop the profile first, keep the class; only then the old unfiltered fallback). The unlock is the FT aura tag **76004** read once per injection (`TaggedAuraAmount`, promoted to a within-module header); the chosen profile is a new account knob `cfg_stat_profile` that travels like `cfg_caster_pct` (`SET statprofile n`, refused while locked — the `band` idiom); `SendCfg` appends `cfgStatProfile` and `statFilterUnlocked` before the text tail; the panel shows a fifth slider row whose labels are words. Recon with `path:line`: `.superpowers/sdd/wp9-recon.md` — read it first.

**Tech Stack:** C++17 module (`-Werror`, `codestyle-cpp.py`, `pdblock` harness), module characters SQL, AIO Lua, FT content JSON + Python tests.

---

## Global Constraints

- Branches: `mod-procedural-dungeon` `claude/pdv2-round-e-63ac9a2a` (HEAD `1f9a765` + this plan), `mod-forgotten-talents` same name (HEAD `d80dbcd`). Conventional Commits, English, comments say WHY in the house voice.
- **Profiles:** `0 Off, 1 Strength, 2 Agility, 3 Caster` (`PD_STAT_PROFILE_*` in `PDDefines.h`, `PD_STAT_PROFILE_MAX = 3`).
- **The stat mask** (one `uint8` per pool row, built from `ItemStat[0..StatsCount-1]`, `ITEM_MOD_*` values from `ItemTemplate.h:27-70`): `STR` (4), `AGI` (3), `INT` (5), `CASTER_EVIDENCE` (SPIRIT 6, MANA_REGENERATION 43, SPELL_POWER 45, SPELL_PENETRATION 47, deprecated 41/42), `PHYS_EVIDENCE` (ATTACK_POWER 38, RANGED_ATTACK_POWER 39, ARMOR_PENETRATION_RATING 44, EXPERTISE_RATING 37, DEFENSE_SKILL_RATING 12, DODGE 13, PARRY 14, BLOCK_RATING 15, BLOCK_VALUE 48). Stamina, hit, crit, haste, resilience are neutral. `ScalingStatDistribution != 0` → mask `0`.
- **The rule ("primary stat wins", zero evidence passes):** `FitsProfileRaw(mask, profile)`: profile 0 → true; mask 0 → true. Strength: `STR` → true; else `AGI || INT` → false; else `!CASTER_EVIDENCE`. Agility: `AGI` → true; else `STR || INT` → false; else `!CASTER_EVIDENCE`. Caster: `INT` → true; else `STR || AGI` → false; else `!PHYS_EVIDENCE`. (Hybrids with two primaries pass both of their profiles; trinkets/relics/replicas with no stats pass everything; a pure-rating tank ring passes Strength/Agility and fails Caster.)
- **Fallback in `Collect`/`RollGear`/`RollGearUnion`:** stage 1 class+race+profile; if empty → stage 2 class+race only (profile dropped, one `PDv2Debug()` line naming pool/class/profile); if still empty → the existing unfiltered stage. Never "no gear".
- **Unlock:** `PD_TALENT_TAG_STATFILTER = 76004`; `unlocked = TaggedAuraAmount(player, 76004) > 0`, read **once** per injection/`SendCfg`/`SET`, never per candidate. The effective profile of a roll = `unlocked ? account.cfgStatProfile : 0`. Aura is per character, the knob per account — deliberate: the choice is shared across the account, each character unlocks it on its own.
- **Wire:** `C` payload gains field 25 `cfgStatProfile` and field 26 `statFilterUnlocked` (0/1), appended BEFORE the free-text tail (`PDv2UILink.cpp:494-495` keeps `Sanitize(...)` last); `CFG_FIELDS` 24 → **26** in `flpdui.lua`; the `SET` key is `statprofile` (refused with a `PDv2Debug()` line while not unlocked, exactly like `band :917-928`; clamp 0..3 in `SetAccountCfg`, which takes NO unlock parameter).
- **Persistence:** new file `data/sql/db-characters/mod_pdungeon_account_statprofile.sql` (the `…_diffcap.sql` shape: information_schema guard, `PREPARE/EXECUTE`, `cfg_stat_profile TINYINT UNSIGNED NOT NULL DEFAULT 0`, **no AFTER**); `LoadAccountState` SELECT tail, clamp, `SaveAccountCfg` columns + `ON DUPLICATE KEY`, `SavePlanToDB` INSERT half.
- Gates: PD → `codestyle-cpp.py` clean, `pdblock` harness (`--batch 500`) green with the new `FitsProfileRaw` pins, `cmake --build C:\wowstuff\dcore_bin --config RelWithDebInfo --target worldserver --parallel 4` (never unbounded; **Task 1 is the only task that builds**); FT → suite green + `--from-canonical` regenerated + idempotent; Lua → `C:\wowstuff\dcore\lua52_compiler.exe -p` (a 5.2 parser: balance only). No install/deploy (planner).
- The three places that state "stat profile is NOT filtered" are rewritten in Task 1's commit: `generator/PDv2GameMath.h:534-537`, `conf/mod_procedural_dungeon.conf.dist:749-752`; the spec line `2026-09-10-…design.md:266` (D5) by the planner.

---

### Task 1: PD engine — stat mask, `FitsProfileRaw`, two-stage fallback, account knob, UI link, tagged-aura header

**Repo:** `mod-procedural-dungeon`. **Files:** `src/generator/PDv2GameMath.h` (D5 block `:521-738`), `tests/blockplan_harness.cpp` (`FitsClassRaw` pins `:3262-3305` as the template), `src/PDv2LootMgr.h/.cpp` (`LootPoolEntry :66-72`, `Load :151-175`, `Collect :299-323`, `RollGear :365-409`, `RollGearUnion :411-453`, `ItemFitsPlayer :525-534`), `src/PDv2ChestLoot.cpp` (`:172-174`, `:198-201`, `:241-243`, `:333`, `:395`), `src/PDv2InstanceScript.h/.cpp` (`TaggedAuraAmount :327-345` → moved, `InjectBossGear :943-986`, `SpawnRespawnCopies` caller `:2124-2142`), **new** `src/PDv2TaggedAura.h`, `src/PDDefines.h` (`:128-142`), `src/PDv2Mgr.h/.cpp` (`PDv2AccountState :334-363`, `SavePlanToDB :384-418`, `LoadAccountState :421-484`, `SetAccountCfg :490-507`, `SaveAccountCfg :509-530`), `src/PDv2UILink.cpp` (`SendCfg :385-497`, `SET :865-953`), **new** `data/sql/db-characters/mod_pdungeon_account_statprofile.sql`, `conf/mod_procedural_dungeon.conf.dist` (`:749-752`).

**Interfaces:**
```cpp
// PDDefines.h
uint8_t const PD_STAT_PROFILE_OFF = 0, PD_STAT_PROFILE_STRENGTH = 1, PD_STAT_PROFILE_AGILITY = 2, PD_STAT_PROFILE_CASTER = 3, PD_STAT_PROFILE_MAX = 3;
int32 const PD_TALENT_TAG_STATFILTER = 76004;   // Discerning Eye: EffectMiscValue of the FT node's dummy aura; amount 1 = unlocked
// PDv2GameMath.h (engine-free, constexpr, no ItemTemplate)
enum PDStatMaskBits : uint8_t { PD_STAT_STR = 1, PD_STAT_AGI = 2, PD_STAT_INT = 4, PD_STAT_CASTER_EVIDENCE = 8, PD_STAT_PHYS_EVIDENCE = 16 };
constexpr bool FitsProfileRaw(uint8_t statMask, uint8_t profile);          // the rule from Global Constraints, verbatim
constexpr uint8_t GameClampStatProfile(int v);                               // 0..PD_STAT_PROFILE_MAX
// PDv2LootMgr
struct LootPoolEntry { …; uint8_t statMask = 0; };                           // built in Load() from ItemStat[0..StatsCount-1]; 0 when ScalingStatDistribution != 0
static uint8_t StatMaskFor(ItemTemplate const* proto);                       // the only place that reads ItemStat
uint32_t RollGear(std::string_view pool, Player const* looter, uint8_t profile = PD_STAT_PROFILE_OFF) const;
uint32_t RollGearUnion(std::string_view a, std::string_view b, Player const* looter, uint8_t profile = PD_STAT_PROFILE_OFF) const;
// Collect(pool, filterFor, expansionMask, profile, out): class/race as today, then FitsProfileRaw(entry.statMask, profile)
// PDv2TaggedAura.h
namespace PDungeon { inline uint32 TaggedAuraAmount(Unit const* unit, int32 miscValue); }   // the WP6 body, moved (delete the PDv2InstanceScript.cpp copy)
// PDv2AccountState
uint8_t cfgStatProfile = 0;                                                  // cfg_stat_profile; the knob; the unlock is never stored
// PDv2ChestLoot.cpp / InjectBossGear: uint8_t profile = looter && TaggedAuraAmount(looter, PD_TALENT_TAG_STATFILTER) ? account(looter).cfgStatProfile : 0; then RollGear(..., looter, profile)
```
- `Describe()`/the boot loot line may add nothing; the per-roll debug line `:388-398` gains the profile and the fallback stage.
- `SendCfg`: `cfgStatProfile` (25), `statFilterUnlocked` (26, from the aura of the `Player*` at `:394`) before the tail; comment the append rule again. `SET statprofile n`: refuse + debug line when not unlocked, else clamp via `SetAccountCfg`, save, echo.
- Harness: `RunFitsProfileChecks` — a table of ≥ 12 cases (mask 0 passes all; STR-only per profile; AGI-only; INT-only; STR+INT hybrid passes 1 and 3; AGI+INT passes 2 and 3; CASTER_EVIDENCE-only fails 1 and 2, passes 3; PHYS_EVIDENCE-only passes 1 and 2, fails 3; profile 0 passes everything; out-of-range profile treated as 0 — say so in the code).
- Comments: rewrite the D5 paragraph in `PDv2GameMath.h:534-537` (the profile IS filtered now, by the player's choice, with the two-stage fallback) and the conf.dist text `:749-752`.

- [ ] Step 1 math header + harness pins (`pdblock --batch 500`) · [ ] Step 2 LootMgr mask/filter/fallback · [ ] Step 3 tagged-aura header + account knob + SQL + UI link + SendCfg · [ ] Step 4 call sites (chests, final cache, boss) · [ ] Step 5 codestyle, build, commit `feat(loot): stat-profile filter - Discerning Eye unlocks a per-account profile chosen in the panel; primary stat wins, two-stage fallback`.

---

### Task 2: Panel — the profile row

**Repo:** `mod-procedural-dungeon`. **File:** `lua_scripts/flpdui.lua` only (`CFG_FIELDS :121`, `ParseCfg :123-151`, `MakeSlider :331-360`, `RenderBand :326-329`, sliders `:365-369`, `LayoutPanel :452-478`, `ApplyCfg :490-557`, `PANEL_H :254`, `BAND_ROW_H :255`).

- `CFG_FIELDS = 26`; `ParseCfg` names `statProfile = f[25]`, `statUnlocked = f[26]`; the tail stays `f.tail`.
- A fifth slider `profileSlider` via `MakeSlider("…Profile", "Stat profile", RenderProfile, "statprofile", …)` with `RenderProfile(v)` → `Off / Strength / Agility / Caster`, min 0 max 3 step 1, placed under the caster slider (before the band row); `PROFILE_ROW_H` added to the panel height only while shown; **shown only when `statUnlocked == 1`** (the `bandRow` show/hide idiom in `LayoutPanel`, re-anchoring the rows below it); `ApplySlider(profileSlider, c.statProfile, 0, 3, 1)`. One line under the slider (or the loot line) reads "Discerning Eye: caches and bosses roll only gear that fits" while unlocked — optional, keep it short.
- The `SET` goes through the existing `pending[setKey]` debounce → `UI SET statprofile n`. No other behaviour change. Syntax gate with `lua52_compiler.exe -p`. Commit `feat(ui): stat-profile row in the panel, shown once Discerning Eye is owned`.

- [ ] Step 1 parse · [ ] Step 2 slider + render + layout · [ ] Step 3 commit.

---

### Task 3: FT — node 2004 *Discerning Eye*

**Repo:** `mod-forgotten-talents`. **Files:** `content/extra_nodes.json`, regenerated artefacts (`--from-canonical`), `tools/import_ebonhold.py:34` + `client/.../Core.lua:6` (content version **5**), `content/exclusions.json` (`filtered_nodes` 704, `filtered_links` 731, `direct_spells` 837, `spell_closure` 877, `custom_spells` 873, `maximum_power` + 750 000), `tests/test_generated_content.py` (version literals 4 → 5 ×4, rename the test; 947's `progress_pct` 100 → 94 is recomputed), `tests/test_extra_nodes.py` (2004 present, one rank, tag 76004), `tests/test_economy_report.py` if it pins the extension rows, `README.md` extension section (one sentence).

- Contract: `{"id": 2004, "parents": [947], "x": -150, "y": -1650, "cost_mult": 300, "permanent": false, "tag": 76004, "icon": <stock id>, "name": "Discerning Eye", "description": "Unlocks the stat profile in the Forgotten Depths panel: caches and bosses only yield gear that fits it ($s1 profile).", "ranks": [{"value": 1, "cost": 750000}]}`. Verify (−150, −1650) is free (it was at recon; fallback (−120, −1650)). Icon: a **stock** `SpellIcon.dbc` id (≤ 4375, so the 83-icon test holds) whose path names an eye or spyglass — read `C:\Users\Anwender\Documents\GitHub\share-public\dbc\SpellIcon.dbc` the way `C:\Users\Anwender\AppData\Local\Temp\claude\C--wowstuff-ForgottenLand2-0\63ac9a2a-f034-4f08-a65b-df0c4ee8ca97\scratchpad\icon_lookup.py` does (WDBC header, id + string offset) and pick e.g. `INV_Misc_Spyglass_03` / `Spell_Shadow_EvilEye`; state the id and path in the report.
- Regenerate, tests green (54 today; count in the report), second run a no-op, `client/generated` untouched, no deploys. Commit `feat(content): Discerning Eye (2004, tag 76004) - the stat-profile unlock at the end of the treasure branch; content version 5`.

- [ ] Step 1 contract + version · [ ] Step 2 regenerate + pins · [ ] Step 3 commit.

---

### Task 4 (planner): reviews, docs, install, deploys, vault

- [ ] Sonnet reviews of Tasks 1 and 2 (Task 3 is a content add with tests); fixes by the same implementers.
- [ ] Docs: PD `CLAUDE.md` row + `README.md` paragraph, spec D5 line + status; FT `log.md`; `runde31` §11; vault (06: tag 76004, spell 120872, node 2004; queue; log END); memory.
- [ ] Install (backup `worldserver.exe.pre_roundE_wp9_20260910`), boot log (`mod_pdungeon_account_statprofile.sql` applied), deploy `flpdui.lua` + FT addon (loose; patch-9 when the client is closed), `.forgotten grant` note for the test.
