# mod-procedural-dungeon

Procedurally generated dungeons for AzerothCore (WoW 3.3.5a) — **every run a
unique layout, no client patch required.**

Inspired by [threejs-procedural-dungeon / "Dungeon Forge"](https://github.com/majidmanzarpour/threejs-procedural-dungeon):
seed → room scattering → separation → connectivity graph → MST + loop edges →
BFS room semantics (entrance / combat / elite / treasure / shrine / boss) →
tile grid → decoration. The server runs exactly that pipeline per instance run
and **assembles the result out of existing WMO/M2 GameObjects** (Wintergrasp
walls, gates, braziers, chests) on an unused, client-known base map.

> **PDv2 (map 760, the client-composed kit) plans differently since Round B:** one chain of
> rooms from the entrance through the boss rooms, a few one-room pockets and, per boss segment,
> a possible loop room beside the corridor - see `docs/superpowers/specs/2026-09-02-pdv2-b0-spine-generator-design.md`.
>
> Round B also **fills** that chain. **Death** returns you alive - at full health, with **no
> resurrection sickness** (operator, 2026-09-08), never to a world graveyard (map 760 has
> none) - to the arena of the **furthest boss you have already killed**, or to the entrance
> while no boss has fallen. Since Round C / C5 that checkpoint is computed from the run's own
> state; B1's clickable Altars of Return are gone.
> A **sealed portcullis** (GO 910059) holds each boss doorway - the walk grid is cut on both
> sides, so creatures stop at it too - until that segment's PLANNED non-boss kills reach
> `V2.Barrier.Pct`. Since Round D **every corridor between two chain rooms carries one
> patrol** - a **single file** of 1, 2 or 3 melee creatures (2 from difficulty
> `V2.Patrol.Size2Diff`, 3 from `V2.Patrol.Size3Diff`), the leader walking a **corridor-only**
> beat from doorway to doorway and the others following it at `V2.Patrol.FollowDistYd` per rank.
> The beat is planned by the patrol's own path finder: **axis-aligned legs down the lane centre**
> that pay for a turn, for hugging a wall and for a cell carrying a prop, so a patrol no longer
> cuts through house corners or furniture. It rejoins that existing beat after an evade rather
> than re-planning a shorter one, and a file whose leader dies dissolves and holds. And with
> `V2.Ambush.Chance` one corridor per segment is an **ambush**: step into that corridor
> block and it springs once - a 2 s stun and `V2.Ambush.Mobs` creatures. The trigger is the
> block, not a radius (Round C removed `V2.Ambush.RadiusYd`: a 9 yd disc could be walked
> past on every corner, T and cross junction). Neither patrol nor ambush moves any counter.
> Since Round C / C6 a run also draws **no boss twice** while the pool holds more distinct
> bosses than the run has boss rooms (below that, repeats fill the rooms), and every boss
> fights with **two abilities at difficulty 1** - four rows per boss, two at `minDiff 1`
> plus one each at 50 and 75.
> Round C / C7 puts that state on the **HUD**: a `Gate` line counts the next sealed
> portcullis's kills up and reads `open` when nothing is sealed, the map paints every
> **cleared room green**, and every notice is shouted as a **raid warning** on top of the
> chat line. And with C8 the run now **ends** somewhere: when the last boss dies **Chromie**
> (NPC 910550) appears in that hall, speaks three lines four seconds apart, a **reward
> cache** (GO 910068) stands beside her, and about a second after the last line a **portal
> to Azealia** (GO 910067) opens - a click, not an automatic pull, so the cache is looted
> first. All three go down with the run, so re-entering during the finale rebuilds the
> dungeon and takes an un-looted cache with it.
> Creatures also stand on the kit's **published spawn anchors** now, not on a 12 yd circle
> around the block centre (that circle survives only as the overflow), and the ordinary room
> gained a **third size** - a 33.33 yd platform - while the entrance and boss rooms lost their
> centre pad. Eight conf keys tune the three hazards; every code default equals the shipped
> `.conf.dist` value, so none has to be set.
>
> **Round E (2026-09-10) gives the dungeon its loot.** Every kill pays every player on the map
> personally: a **Faded Remnant** (white) always, a **Gleaming Remnant** (green) at 5 %, a
> **Radiant Remnant** (blue) at 1 % — the five Round-E currencies 920105–920109 that the
> Forgotten Talents tree is bought with — scaled by a **room factor** (a ten-room run pays the
> listed chance, a one-room run a tenth of it, +1 % per room above ten, so one-room farming
> does not pay), plus 1..5 random **materials** from every expansion, the count growing with
> the dungeon level. **Gear** comes from six pools generated out of the world DB by workspace
> script 106 (`pdungeon_loot_pool`): the Shifting Cache in dead ends and loop rooms holds a
> WotLK heroic-dungeon or non-ICC normal-raid epic (ICC normal from dungeon level 10), every
> boss corpse a ToGC / Ruby-Sanctum heroic item, Chromie's Cache an ICC 10/25 item (ICC heroic
> from dungeon level 10) plus the **Sovereign** (purple, difficulty ≥ 50, 50 %) and **Eternal**
> (orange, ≥ 75, 10 %) Remnants and the old Azealia-Underground rares (five mounts at
> 1/10 000, the Expert Emblem, the Exobeast plate, the Mystery Boxes). Finished **tier-set
> pieces** are in the pools; tier tokens, VoA and PvP gear are not. Gear is filtered to what
> the looter's class and race can wear, item counts scale with the loot multiplier, and every
> number is a `ProceduralDungeon.V2.Loot.*` key whose code default equals the shipped
> `.conf.dist`. All loot rolls use the core's `urand` — the layout is seeded, the loot never is.
>
> Round E also **caps the difficulty dial per account**: everyone starts at 1, a completed run
> unlocks up to its difficulty **+5** without a death and **+3** with one (`V2.Cap.CleanUnlock` /
> `.DeathUnlock`), account-wide and never lowered by play; the panel's slider follows the cap.
> The **rooms slider now counts ordinary rooms** — the entrance and the boss rooms come on top —
> so "14" on the panel is "0/14 rooms" on the HUD (bosses have their own counter; stored layouts
> reroll once, layout version 4). **`ProceduralDungeon.V2.Debug`** gates every per-creature and
> per-tick line, so a quiet log is the default. And the gen panel **draws the planned layout**
> after Generate, the same map the HUD shows once you are inside.
>
> Kit **`t1b-v39`** (Round E / WP4) closes the two visual leftovers: the tower room's side walls
> are lined with houses again (the facade reserve was still sized for a pad ring Round B removed),
> and the ground texture no longer breaks at chunk edges — every MCNK carries `0x8000`
> (`do_not_fix_alpha_map`), so the alpha feather is authored edge to edge instead of pinned.
>
> **Event rooms (Round E / WP5).** Each boss segment has a `V2.Event.ChancePct` (25 %) chance of
> an extra dead-end room with its own corridor — purple on the map — where the **Weary Pilgrim**
> waits. Talk to him and **hold the line** for `V2.Event.DurationSec` (60 s): every
> `V2.Event.SpawnEverySec` (5 s) one creature of the run's packs (10 % casters) storms in from
> the room's rim and goes for him; the HUD counts down and shows his health. If he lives, a
> Shifting Cache appears and everyone on the map earns `V2.Event.ParagonXp` (1 000) × the loot
> multiplier in Paragon XP; if he falls, the wave vanishes and there is no reward. The wave
> drops materials but no Remnants (`V2.Loot.Currency.ExtraMobsDropCurrency`), and it never
> counts toward the run, the gates or the room total. The layout decides where an event room
> sits (layout version 5), so a stored dungeon rerolls once.
>
> The v1 pipeline below still describes the GameObject-assembled prototype.

## How it works

- Each run is a fresh `InstanceMap` with its **own dynamic collision tree** —
  spawned walls give real server-side line of sight and standable height;
  the client collides players against the models it already has in its MPQs.
- The `InstanceScript` is attached via the `OnBeforeCreateInstanceScript`
  hook — no core edits, no `instance_template.ScriptId`.
- The base map (default: 37 *Azshara Crater*) is re-typed to a dungeon
  **server-side only** through the `map_dbc` DB override.
- Same seed = same dungeon (deterministic generator, portable across
  compilers) — usable for daily seeds or leaderboards later.
- Mob movement: static mmaps cannot see spawned walls, so the module runs its
  own A* on the generated tile grid and drives cross-room chases with
  waypoint chains; rooms are convex, so in-room combat needs no help. Casters
  only cast with real (dynamic-tree) line of sight.

## Requirements

- AzerothCore rev with the `OnBeforeCreateInstanceScript` map hook (present
  in current 15.x master and this project's fork).
- Extracted `vmaps/` including the `GAMEOBJECT_MODELS` list (standard vmap
  extraction) — without it the spawned pieces have no **server-side**
  collision. Verify in game with `.pdungeon validate`.
- `worldserver.conf`: `CheckGameObjectLoS = 1` (default).

## Install

```bash
cd azerothcore-wotlk/modules
git clone https://github.com/Shoro2/mod-procedural-dungeon.git
cd ../build && cmake .. -DSCRIPTS=static -DMODULES=static && make -j$(nproc)
```

SQL under `data/sql/db-world/` and `data/sql/db-characters/` is applied
automatically by the db updater on worldserver start. Copy
`conf/mod_procedural_dungeon.conf.dist` next to your `worldserver.conf` (the
build installs it automatically) and set:

1. `ProceduralDungeon.Enable = 1`
2. Scout a large flat spot on the base map with a GM character
   (`.go xyz <x> <y> <z> 37`, then `.gps`) and set
   `ProceduralDungeon.Center.X/Y/Z`. Update the graveyard row in
   `mod_pdungeon_base.sql` to match.
3. Optional: spawn the entrance NPC in a capital: `.npc add 910510`.

## Usage

| Command | Effect |
|---|---|
| `.pdungeon gen [seed]` | Dry run: prints the generated layout as ASCII in chat |
| `.pdungeon enter [seed]` | GM: teleport into a fresh run (random seed if omitted) |
| `.pdungeon leave` | Teleport back to your hearth location |
| `.pdungeon info` | Seed, spawn queue, elite rooms remaining, timings |
| `.pdungeon validate` | Spawns every palette piece and reports collision models |

Players use the **entrance NPC** (entry 910510): random layout or a chosen
seed. Rules of a run: clear all **elite rooms** to open the **boss gate**;
kill the boss to spawn the **treasure chest** and the **exit portal**.
Treasure rooms hold bonus chests, the shrine buffs the party once per run.

## Customization

- **Palette** (`pdungeon_palette` table): swap wall/gate/decoration pieces or
  add a new `theme` — no rebuild needed. Walls and gates should use **WMO**
  displays (M2 pieces do not block creature vision).
- **Layout knobs** (`mod_procedural_dungeon.conf`): grid size, room count and
  sizes, loop chance, pack sizes, torch density, spawn batching.
- **Creature packs** (`pdungeon_packs` / `pdungeon_pack_members` /
  `pdungeon_member_spells`): **eight** packs ship today — 1 Crypt Horrors,
  2 Abyssal Broodpit, 3 Lords of the Deep, 4 Barrow Dead, 5 Legion Rift,
  6 Shadowfang Pack, 7 Cult of the Damned, 8 Ahn'kahet Deep — one theme per
  room, with the boss drawn from the role-2 pool across all packs and no boss
  appearing twice in a run while the pool allows it. Every pack **references**
  stock creature entries and adds no `creature_template` or `spell_dbc` row of
  its own, so adding a pack is pure data: a `.sql` pair that deletes and
  re-inserts its own `packId` only. Add one by copying a sibling file rather
  than by hand — the header of each records what has to be measured (health,
  swing, silhouette, loot, immunities, and the `SpellDifficulty` substitution
  that decides whether the spell id you wrote is the spell the core casts).
- **No dungeon kill grants reputation.** The module calls
  `Creature::SetReputationRewardDisabled(true)` on every creature it spawns —
  room mobs, patrols, ambushes, split children and critters alike — so stock
  entries that carry a `creature_onkill_reputation` row cannot turn a generated
  dungeon into a reputation faucet (pack 7's Scholomance trash would otherwise
  pay +10 Argent Dawn per kill, to Exalted, with no turn-in). It is a code
  switch, not a data patch, so no stock table is retuned and every future pack
  inherits it; there is deliberately no per-pack opt-out.
- **Base map**: `ProceduralDungeon.BaseMapId` + the matching SQL block in
  `mod_pdungeon_base.sql` (prepared variants: 451 Development Land,
  169 Emerald Dream, 44 Monastery).

## Known limitations (MVP)

- Mobs that flee or are feared use core movement and can clip through spawned
  walls; evade home-runs walk straight lines.
- No minimap for the generated layout (planned: AIO map frame fed from the
  tile grid).
- Decorative liquids only — no swimmable pools.
- The client keeps its own view of the base map (weather/zone name/music from
  the original map).

## Roadmap

- AIO minimap UI (tile grid → addon frame)
- More palette themes (Icecrown, Ulduar, …) and room templates
- Difficulty scaling, affixes, timed runs, leaderboards on `pdungeon_runs`
- Optional one-time MPQ patch with a purpose-built modular tile kit

## License

GPL v2, like AzerothCore.
