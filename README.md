# mod-procedural-dungeon

Procedurally generated dungeons for AzerothCore (WoW 3.3.5a): every run gets a
freshly generated layout. The module ships two generations of the same idea:

- **v1** assembles the dungeon on the server out of existing WMO/M2 GameObjects
  (Wintergrasp walls, gates, braziers, chests) on an unused, client-known base
  map. **No client patch required.**
- **v2** plans a chain of rooms and corridors on its own map (default 760) and
  lets the client compose the terrain from a pre-built block kit. v2 is the
  full game mode (per-account dungeon, difficulty dial, affixes, bosses, loot,
  themes) but needs client-side components, see Requirements.

The generator is inspired by
[threejs-procedural-dungeon / "Dungeon Forge"](https://github.com/majidmanzarpour/threejs-procedural-dungeon):
seed, room scattering, separation, connectivity graph, MST + loop edges, BFS
room semantics (entrance / combat / elite / treasure / shrine / boss), tile grid,
decoration. The same seed always produces the same layout.

## Features

- Deterministic, engine-free layout generator (`src/generator/`), usable with a
  fixed seed for reproducible runs.
- v1: room/corridor dungeon built from GameObjects, entrance NPC, elite rooms
  that open a boss gate, treasure chest, exit portal, shrine buff.
- v2: spine of rooms from entrance to boss rooms with side pockets and loop
  rooms, sealed boss gates, patrols and ambushes in corridors, event rooms,
  bosses with affixes, a finale (NPC, reward cache, exit portal), three visual
  themes (city, mine, forest), a difficulty dial 1..100 that unlocks per account,
  personal loot pools (currency tokens, materials, gear), optional Paragon XP and
  Endless Storage integration, a generation panel and run HUD delivered by AIO.
- Own creature AI for generated layouts: static mmaps cannot see spawned walls,
  so mobs path on the module's own A* grid; casters only cast with real line of
  sight.
- Dungeon creatures never grant reputation (a code switch, no stock data is
  changed).

## How a v2 run plays

**Layout.** A run is one chain of rooms from the entrance through the boss rooms, with a few one-room pockets
off the ordinary rooms and, per boss segment, a possible loop room beside a straight corridor. Rooms come in
three sizes (standard, blob outline, 33 yd platform); the entrance and boss rooms are open arenas. The rooms
slider counts ordinary rooms; the entrance and boss rooms come on top. The same seed always gives the same
layout. Creatures stand on the kit's published spawn anchors.

**Death.** Dying returns you alive at full health, with no resurrection sickness, to the arena of the furthest
boss you have already killed (or to the entrance while no boss has fallen). The map has no graveyard.

**Gates, patrols, ambushes.** A sealed portcullis holds each boss doorway (creatures stop at it too) until that
segment's planned non-boss kills reach `V2.Barrier.Pct`. Every corridor between two chain rooms carries one
patrol: a single file of 1, 2 or 3 melee creatures (2 from difficulty `V2.Patrol.Size2Diff`, 3 from
`V2.Patrol.Size3Diff`) walking a corridor-only beat from doorway to doorway, planned so that it keeps to the lane
centre and avoids walls and props. With `V2.Ambush.Chance`, one corridor per segment is an ambush: stepping into
that corridor block stuns you for 2 s and spawns `V2.Ambush.Mobs` creatures. Patrols and ambushes do not count
toward any kill counter.

**Bosses and affixes.** A run draws no boss twice while the pool holds more distinct bosses than the run has boss
rooms. Bosses fight with two abilities at difficulty 1, more from difficulty 50 and 75. Trash creatures come from
themed creature packs; with `V2.Affix.Percentage` a room has a chance to include one creature carrying all
affixes unlocked at the current difficulty (the Dungeon Challenge affix set).

**Difficulty.** An integer 1..100 chosen on the panel; mob health and damage grow per level
(`V2.Diff.HealthPctPerLevel`, `V2.Diff.DamagePctPerLevel`). The dial is capped per account: everyone starts at 1,
and a completed run unlocks up to its difficulty +5 without a death and +3 with one
(`V2.Cap.CleanUnlock` / `.DeathUnlock`), account-wide and never lowered by play.

**Event rooms.** Each boss segment has a `V2.Event.ChancePct` chance of an extra dead-end room (purple on the
map) where a Weary Pilgrim waits. Talk to him and hold the line for `V2.Event.DurationSec`: every
`V2.Event.SpawnEverySec` one creature of the run's packs storms in and goes for him, and the HUD shows the
countdown and his health. If he lives, a Shifting Cache appears and everyone on the map earns Paragon XP
(`V2.Event.ParagonXp`, if mod-paragon is installed); if he falls, the wave vanishes. Event waves never count
toward the run.

**Respawn echoes.** A Forgotten Talents node (found through the aura tag, not a spell id) can make ordinary kills
rise again as one or two echoes of the same creature. Echoes are rewards: they count toward no room, gate or
total, drop no currency, and never echo themselves. `V2.Respawn.Enable` and `V2.Respawn.MaxCopies` control it.

**Finale.** When the last boss dies, the NPC Chromie (910550) appears in that hall, speaks three lines, a reward
cache stands beside her and a portal opens to the `game_tele` row named in `V2.Finale.TeleName`. The portal is a
click, not an automatic pull, so the cache can be looted first; all three disappear with the run.

**Loot.** Every kill pays every player on the map personally. Five Remnant currencies (white to orange tier;
shown in the Currency tab, not in the bags) drop with rising rarity, scaled by a room factor so that one-room
farming does not pay, plus 1..5 random crafting materials per kill, with the count growing with the dungeon level.
Gear comes from loot pools stored in `pdungeon_loot_pool` / `pdungeon_loot_bonus`: dead-end caches, boss corpses
and the finale cache each have their own pool, filtered to what the looter's class and race can wear. A talent-
unlocked **stat profile** (Off / Strength / Agility / Caster) further restricts caches and boss gear to that
primary stat. Dungeon creatures drop no item loot of their own (`V2.Loot.NativeItems`, off by default; gold
stays). Materials go straight into the Endless Storage when `mod-endless-storage` is installed
(`V2.Loot.MatsToStorage`). Spent caches despawn. All loot rolls use the core's `urand`, so the layout is seeded
but the loot never is. Every number is a `ProceduralDungeon.V2.Loot.*` key.

**Themes.** The kit carries three looks: the city, the mine (rock floors and walls, crystals, carts) and the
forest (mossy cliffs, tree lines, campfires). Each theme has its own creature packs, light, ambience, music and
zone name. The panel's Theme dropdown offers Random / Mine / City / Forest; Random rolls a theme from the run's
seed, so every Generate can differ while the same seed always returns the same look. A stored dungeon keeps the
theme it was generated with.

**Panel and HUD.** The AIO addons give a generation panel (rooms, difficulty, caster ratio, theme, stat profile,
and a drawn map of the planned layout) and a run HUD with the room and boss counters, the next gate's progress
(`Gate open` once its barrier is up), the event bar, a map that paints cleared rooms green, and raid-warning
notices.

## How it works (v1)

- Each run is a fresh `InstanceMap` with its **own dynamic collision tree**: spawned walls give real server-side
  line of sight and standable height; the client collides players against the models it already has in its MPQs.
- The `InstanceScript` is attached via the `OnBeforeCreateInstanceScript` hook - no core edits, no
  `instance_template.ScriptId`.
- The base map (default: 37 *Azshara Crater*) is re-typed to a dungeon **server-side only** through the `map_dbc`
  DB override.
- Mob movement: static mmaps cannot see spawned walls, so the module runs its own A* on the generated tile grid
  and drives cross-room chases with waypoint chains; rooms are convex, so in-room combat needs no help. Casters
  only cast with real (dynamic-tree) line of sight.
- v1 rules of a run: clear all elite rooms to open the boss gate; the boss spawns the treasure chest and the exit
  portal; treasure rooms hold bonus chests and the shrine buffs the party once per run.

## Requirements

- AzerothCore (WotLK 3.3.5a) with the `OnBeforeCreateInstanceScript` map hook,
  which current upstream master has. The module was developed against the
  [Shoro2/azerothcore-wotlk](https://github.com/Shoro2/azerothcore-wotlk) fork;
  we found no fork-only hook used by the module sources, but it has not been
  built against vanilla upstream.
- v1: extracted `vmaps/` including the `GAMEOBJECT_MODELS` list (standard vmap
  extraction), and `CheckGameObjectLoS = 1` (default) in `worldserver.conf`.
- v2 additionally needs:
  - **AIO** (Rochet2) running on a Lua engine such as `mod-ale`; the two addons in
    `lua_scripts/` (`flprocdungeon.lua`, `flpdui.lua`) are delivered to players
    through it. Put them into the server's `lua_scripts/` folder.
  - Client files that are **not part of this repository**: the block kit (loose
    files under the client's `Data` folder), the `FLStream.dll` client
    extension that composes the map from the server's layout manifest, and the
    matching client DBC rows for map 760 (Map, MapDifficulty, AreaTable, Light,
    plus CurrencyTypes for the Remnant currencies). Without them a client cannot
    load map 760; the entry gate refuses clients that do not report the DLL.
  - Optional: `mod-paragon` (Paragon XP rewards), `mod-endless-storage` (kill
    materials are deposited there when present), `mod-forgotten-talents` (the
    talents that react to dungeon content).

## Installation

```bash
cd azerothcore-wotlk/modules
git clone https://github.com/Shoro2/mod-procedural-dungeon.git
cd ../build && cmake .. -DSCRIPTS=static -DMODULES=static && make -j$(nproc)
```

The SQL under `data/sql/db-world/` and `data/sql/db-characters/` is applied
automatically by the AzerothCore db updater on the next worldserver start. That
includes `mod_pdungeon_map760.sql` (server-side rows for map 760); the client
side of map 760 has to match and is not part of this repo.

Copy `conf/mod_procedural_dungeon.conf.dist` to your `configs/modules/` folder as
`mod_procedural_dungeon.conf` (the build installs the `.dist` automatically), then:

1. v1: set `ProceduralDungeon.Enable = 1`. Scout a large flat spot on the base
   map with a GM character (`.go xyz <x> <y> <z> 37`, then `.gps`), set
   `ProceduralDungeon.Center.X/Y/Z` and update the graveyard row in
   `mod_pdungeon_base.sql` to match. Optionally spawn the entrance NPC in a
   capital: `.npc add 910510`.
2. v2: set `ProceduralDungeon.V2.Enable = 1` once the client components above are
   in place.

## Configuration

`conf/mod_procedural_dungeon.conf.dist` documents every key; the code default of
each key equals the shipped value. The main ones:

| Key | Meaning |
|---|---|
| `ProceduralDungeon.Enable` / `ProceduralDungeon.V2.Enable` | switch v1 / v2 on (both default 0) |
| `ProceduralDungeon.BaseMapId`, `.Center.X/Y/Z` | v1: map and grid centre the dungeon is assembled on |
| `ProceduralDungeon.Rooms.Min/.Max`, `.Mob.PackSize.*`, `.Theme` | v1: room count, packs, palette theme |
| `ProceduralDungeon.V2.MapId`, `.FloorZ` | v2: the dungeon map and the kit's floor height (must match the kit) |
| `ProceduralDungeon.V2.Rooms`, `.BossRooms`, `.DetourChance`, `.Branches` | v2 layout fallbacks (per-account values from the panel win) |
| `ProceduralDungeon.V2.Theme` | fallback theme; the panel can pick Random / Mine / City / Forest |
| `ProceduralDungeon.V2.RequiredDllVersion` | minimum `FLStream.dll` bridge version the entry gate accepts |
| `ProceduralDungeon.V2.Loot.*`, `.Cap.*`, `.Event.*`, `.Barrier.*`, `.Patrol.*`, `.Ambush.*` | loot, difficulty cap, event rooms, boss gates, corridor hazards |
| `ProceduralDungeon.V2.Finale.TeleName` | `game_tele` name the exit portal after the last boss leads to |
| `ProceduralDungeon.V2.Debug` | verbose per-creature logging (off by default) |

## Usage

All commands need GM level.

| Command | Effect |
|---|---|
| `.pdungeon gen [seed]` | v1 dry run: prints the generated layout as ASCII in chat |
| `.pdungeon enter [seed]` | v1: teleport into a fresh run (random seed if omitted) |
| `.pdungeon leave` / `info` / `validate` | v1: leave, show run state, check that every palette piece has a collision model |
| `.pdungeon v2 gen [seed] [theme]` | v2: plan and store a layout for your account, push it to the client |
| `.pdungeon v2 enter [force]` | v2: enter your dungeon (`force` skips the client-ready check, dev only) |
| `.pdungeon v2 info` / `patrol` | v2: run state / the patrol beat of the room you stand in |
| `.pdungeon v2 cap [n]` | v2: set the difficulty cap of your account |

Players use the **entrance NPC** (entry 910510) for v1 (random layout or a chosen
seed). In v1, clear all elite rooms to open the boss gate; killing the boss spawns
the treasure chest and the exit portal. In v2, players use the generation panel
(rooms, difficulty, caster ratio, theme) delivered by the addon, press Generate and
enter; the HUD shows rooms, bosses, the next gate and events.

## Customization

- **Palette** (`pdungeon_palette`): swap wall/gate/decoration pieces or add a
  `theme`, no rebuild needed. Walls and gates should use WMO displays (M2 pieces
  do not block creature vision).
- **Creature packs** (`pdungeon_packs`, `pdungeon_pack_members`,
  `pdungeon_member_spells`): packs reference stock creature entries and add no
  `creature_template` or `spell_dbc` rows, so a new pack is a SQL file that deletes
  and re-inserts its own `packId`. Copy a sibling pack file as the template.
- **Loot pools** (`pdungeon_loot_pool`) and the affix list (`pdungeon_affixes`)
  are plain world tables.
- **Base map** (v1): `ProceduralDungeon.BaseMapId` plus the matching block in
  `mod_pdungeon_base.sql` (prepared variants: 451 Development Land, 169 Emerald
  Dream, 44 Monastery).

## Known limitations

- Mobs that flee or are feared use core movement and can clip through spawned
  walls; evade home-runs walk straight lines (v1) or can cross the void (v2).
- v1 has decorative liquids only (no swimmable pools) and no minimap; the client
  keeps its own view of the base map (weather, zone name, music).
- v2 cannot be entered without the client components; entering map 760 without a
  loaded layout crashes the client, which is why the entry gate checks first.
- Logging out inside a v2 dungeon parks the character at its homebind; the
  dungeon persists, the position inside it does not.

## Part of the Forgotten Land project

Developed for the Forgotten Land server project; it does not depend on the rest
of it apart from the optional modules listed above.
