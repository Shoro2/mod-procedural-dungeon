# mod-procedural-dungeon

Procedurally generated dungeons: per-run unique layouts assembled at runtime
from existing WMO/M2 GameObjects on a client-known base map (default 37).
No client patch. See [README.md](./README.md) for install/usage and
`share-public/docs/World of Warcraft/procedural-dungeon/` for the full design
doc.

## Layout

| Path | Contents |
|------|----------|
| `src/generator/` | **Engine-free** deterministic generator (types, RNG, pipeline, grid A*). Compiles standalone — keep it free of any engine include. |
| `src/` | Engine glue: `PDMgr` (config/pending seeds), `PDPaletteMgr` (SQL tile set), `PDWorldBuilder` (layout → spawn plans), `PDInstanceScript` (run lifecycle), `PDCreatureAI` (leash/A*-waypoints/LoS-gated casters), entrance NPC, exit/shrine GOs, `.pdungeon` commands |
| `tests/ascii_harness.cpp` | Standalone verification: `g++ -std=c++17 -Wall -Wextra -Werror -O2 -Isrc tests/ascii_harness.cpp src/generator/*.cpp -o pdgen`; `pdgen <seed>` / `pdgen --batch 500` |
| `data/sql/db-world/` | `instance_template` + `map_dbc` override (base map), GO/creature templates (910000+/910500+), `pdungeon_palette` |
| `data/sql/db-characters/` | `pdungeon_runs` history |

## Core touchpoints (no core edits)

- `AllMapScript::OnBeforeCreateInstanceScript` → attaches `PDInstanceScript`
- `Map::SummonGameObject/SummonCreature` → batched build-out in `Update()`
- Per-instance dynamic collision tree → LoS (`CheckGameObjectLoS = 1`) + height
- `GameObject::SetGoState` → gate open/close (boss gate logic)

## Conventions

- Reserved IDs: GO 910000–910099, NPC 910500–910549 (registered in
  share-public `06-custom-ids.md`)
- Determinism: never use std `<random>` distributions in `src/generator/`
  (implementation-defined) — only `PDRandom` helpers; keep iteration orders
  fixed. Same seed must yield the same dungeon on gcc/clang/MSVC.
- The ASCII harness must stay green: `pdgen --batch 500` = 0 failures.
- AC code style, `-Werror`-clean; run `apps/codestyle/codestyle-cpp.py` from
  the module root before committing.
