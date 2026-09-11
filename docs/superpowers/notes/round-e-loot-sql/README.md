# Round E — loot-pool research SQL (read-only, 2026-09-10)

The queries the loot-pool reconnaissance ran against the local `acore_world` on
2026-09-10 (MySQL 8.4.5, 105 733 `item_template` rows). They are the seed for
`scripts/106_pd_loot_pools.py` (workspace, WP1 Task 1) and the record of how the
pool sizes in the Round E spec (§1.3) were measured. Nothing here was applied.

| File | Role |
|---|---|
| `q.sh` | runs one SQL file against the DSN in `C:\wowstuff\dcore\configs\worldserver.conf` (password never on argv) |
| `tmpl4.sql` | the pool resolver: `__CSEED__` / `__GSEED__` / `__LM__` / `__LABEL__` placeholders; creature + gameobject seeds, `reference_loot_template` recursion, `LootMode & 1`, PvE filter (no resilience), tokens reported separately |
| `_run4.sql` | `tmpl4.sql` instantiated for P2 (non-ICC normal raids) — the shape script 106 reproduces per pool |
| `gen4.sh` | substitutes the seeds into the template and runs it |
| `percase.sh` | builds a boss seed from `<maps> <supplement> <difficulty slot>` and a GO seed from a CSV id list |
| `cs_p1b.sql` / `gs_p1.sql` | P1 seeds (heroic 5-man bosses incl. the nine script-summoned ones + 6 cache GOs) |
| `_cs2c.sql` / `_gs2c.sql` | P2 seeds (Naxx/OS/EoE/Ulduar/Ony/ToC, slots 0+1, 28 cache GOs) |
| `_cs3.sql` / `_gs3.sql` | P3 seeds (ToGC slots 2+3 + tribute chests) |
| `_cs4.sql` / `_gs4.sql` | P4 seeds (ICC slots 0+1 + 10N/25N caches) |
| `_cs5.sql` / `_gs5.sql` | P5 seeds (ICC slots 2+3 + 10H/25H caches) |
| `p6b.sql` | P6 materials census with the entry-band expansion heuristic and the junk filter |
| `bossmap.sql` | the `boss_*` ScriptName → map attribution scan used to find summoned bosses |

The generator these seeds became is `scripts/106_pd_loot_pools.py` in the workspace
(`C:\wowstuff\ForgottenLand2.0\scripts\`, backed up in the vault at
`share-public/python_scripts/pdv2-loot/106_pd_loot_pools.py`); it parameterises `tmpl4.sql`
once per pool and writes `data/sql/db-world/mod_pdungeon_loot_pools.sql` — regenerate it,
never hand-edit it.

Six schema facts the resolver depends on (all measured, see the spec §1.3):
`rank = 3` is *world boss*, not dungeon boss; `Reference <> 0` means the `Item`
column is not an item; difficulty clones keep `rank` but lose `ScriptName`;
several bosses are never in `creature`; Eye of Eternity has creature loot 0 (chests
only); `dungeonencounter_dbc` is empty on this realm.
