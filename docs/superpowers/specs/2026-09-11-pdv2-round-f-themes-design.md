# PDv2 Round F — more themes: finish the Mine, then the Forest — Design

**Status 2026-09-11: operator picked "erst Mine fertigstellen, dann Wald" after the ranked feasibility table (recons
`.superpowers/sdd/roundf-recon-a.md` / `roundf-recon-b.md`). F1 (Mine) BUILT + STAGED 2026-09-11 on the workbench (worldserver `e5118ffea825`, branch `claude/pdv2-round-f-63ac9a2a`, T2 = `tools/pd_testlauf_runde32.md` section 1); F2 (Forest) planned after F1's T2.**

## 1. What we measured

- A **theme** is one integer that is simultaneously the kit's chunk-id namespace (`ThemeChunkIdBase`: 1 → 2000, 2 → 12000) and the art
  selector. Kit `t1b-v39` already ships **both** themes (122 ADTs each): theme 1 "mine" (Aerie Peaks rock floors, Deadwind rock wall,
  rock columns / stalagmites / cave-ins / broken carts as kit props, no facades) and theme 2 "city" (the default). `.pdungeon v2 gen
  <seed> 1` renders the mine today; `pdungeon_account.theme` freezes the theme per stored layout. The only gap is a player-facing choice.
- Packs, decor rules and critter rules all carry a `theme` column, and every shipped row is `0` (any). `PDv2PackMgr::LoadFromDB(theme)`
  loads packs ONCE at startup filtered by the CONFIG theme — a per-account theme needs the pool filtered per plan at spawn time.
- One `Light.dbc` row per map (position-based): every theme shares the mood (LightParams 918, ambience 37, music 439). A per-theme
  mood would need the composer/DLL to write a theme-dependent MCNK `areaid` → own `AreaTable` row (light/ambience/music) — noted, not in F.
- Stock material: mine props are the richest set (crystals 219/244/2592, lumber piles 1108/1109, ore crates 36, carts 290/7997, powder keg
  436, wheelbarrow 215, Dark Iron brazier 3411 — all with `GameObjectModels.dtree` collision and stock precedent); mine rosters Deadmines
  36, Uldaman 70, Gnomeregan 90, Blackrock Depths 230. Forest: tilesets Grizzly Hills / Ashenvale / Howling Fjord (specular pairs
  complete), props fallen trees 8025/8028, log 6826, fences 6151/6150, Terokkar trees 7276/7279/7288, campfires; rosters Wailing
  Caverns 43, Maraudon 349, Razorfen 47/129; light donor LP 513. Only 26 free GO ids (910048-49, 910069, 910077-910099).
- No cave tileset family exists in 3.3.5 (caves are WMO interiors) — the mine keeps its rock substitutes. Terrain cannot roof a room;
  a ceiling would be a WMO/M2 overhang (spike) — out of Round F.

## 2. Decisions (assumed so work can start; each a one-line change if refused)

| # | Decision | Choice |
|---|---|---|
| D1 | Theme choice | A per-account knob `cfg_theme` (0 = follow the server conf `V2.Theme`, 1 = mine, 2 = city, later 3 = forest) on the gen panel as a fifth-style slider with word labels ("Default / Mine / City"); `SET theme n`; `C` fields 27 `cfgTheme`, 28 `themeMax` (the highest theme the loaded chunk meta carries). It affects the NEXT Generate only; a stored layout keeps the theme it was generated with. |
| D2 | Packs per theme | The pack manager loads every enabled pack of every theme and filters at draw time by the PLAN's theme. Rule: when the plan's theme has at least one themed pack with a usable non-boss member in the run's band, the draw uses ONLY that theme's packs; otherwise the theme-0 packs (today's behaviour). Conf `V2.Packs.ThemeExclusive = 1` (0 = themed packs merely join the pool). |
| D3 | Mine packs | Three packs `theme = 1`: **Defias Miners** (Deadmines: 657, 634, 636, 4417, 641, 1731, 3947; casters 1732, 1729, 4418; bosses 646 Mr. Smite, 3586 Miner Johnson), **Stonevault** (Uldaman: 4855, 4850, 4860, 7320, 4861, 4863, 4847; casters 4852, 4853, 7321; boss 4857 Stone Keeper), **Dark Iron Forge** (BRD: 8891, 8892, 8893, 8890; casters 8894, 8912; bosses 8923 Panzor, 9033 General Angerforge). `level_min/max = 80`, `unlock_dlvl 0`, weights 100 (the PD scaling and AI replace the stock levels/scripts). Build-time deviation: 8911 Fireguard Destroyer (planned) is NOT shipped - its `creature_template` fire immunity survives the PD scaling, and a trash mob one spec cannot damage is a bug, not flavour; pack 11 has eight members. |
| D4 | Mine decor | Decor rules `theme = 1`: crystal formations (219 / 244 / 2592) `wall_foot` 1..3 per room and `corner` 0..1 (they glow — the mine's light source), Dark Iron brazier 3411 `wall_foot` 1..2 in boss rooms, clutter `scatter` 0..2 per room from lumber piles 1108/1109, ore crates 36, powder keg 436, wheelbarrow 215, ore cart 7997 (290 measured too, not shipped). New GO rows **910077-910086** (type 5 GENERIC, the three-way check per display written into `mod_pdungeon_templates_fix.sql`), 16 ids kept for the forest. The theme-0 torch/brazier rules stay (torches in a mine are right). |
| D5 | Mood | Shared with the city (one light per map). Ambience/music per theme = the AreaId lever, a separate later item. |
| D6 | Client | F1 ships NO client patch: kit v39 already carries the mine; the panel row arrives over AIO. `ClientCacheVersion` stays 19 (only new GO ids). |
| D7 | Forest (F2) | Tier b on the city geometry: theme **3**, chunk namespace **22000** (`THEME_BASES {3: 22000}`, `ThemeChunkIdBase`), +122 ADTs (+140 MB kit, `KIT_VERSION` 28, `t1b-v40`), textures Grizzly Hills pine-needle/moss floors + a mossy-rock wall (specular pairs verified), Terokkar/Arathi trees + fallen trees as kit MDDF/MODF (decorative) and 6826/8025 logs + 6151 fences as type-5 GO blockers along wall feet, packs Wailing Caverns / Maraudon / Razorfen `theme 3`, campfire/lantern decor; layout version unchanged (same masks). Planned in its own plan after F1's T2. |
| D8 | Naming in the UI | Themes are shown by name from a small server-sent list? No — the client keeps a static label table (Default / Mine / City / Forest) indexed by id; `themeMax` bounds the slider. |

## 3. Packages

| Package | Content | Repos | Plan | Evidence |
|---|---|---|---|---|
| **F1 — Mine** | D1 knob + verb + `C` fields + panel row; D2 pack pool per theme + exclusivity conf; D3 three mine packs; D4 mine decor + 10 GO rows | mod-procedural-dungeon | `docs/superpowers/plans/2026-09-11-pdv2-round-f-f1-mine.md` | T1 build/harness/boot; T2 operator: `/pd` → Theme Mine → Generate → run (Defias/Stonevault/Dark Iron packs only, crystals light the rooms, mine clutter) |
| **F2 — Forest** | D7 | mod-procedural-dungeon + workspace kit chain + client kit v40 | written after F1's T2 | T1 chain determinism, oracle, harness roomcap for theme 3; T2 operator |

## 4. Open questions for the operator (answer any time)

- Mine mood: keep the shared dark light, or invest in the AreaId lever (composer + DLL) so mine/forest get their own light and music?
- Forest wall: a mossy rock cliff (cheap, terrain) vs. a tree line as MODF facades (heavy: per-model inset/window measurements like the houses)?
