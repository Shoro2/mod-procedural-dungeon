/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PDWorldBuilder.h"
#include "PDDefines.h"
#include "PDPaletteMgr.h"
#include "generator/PDWallPlan.h"
#include <algorithm>
#include <cmath>

namespace PDungeon
{
    namespace
    {
        float const HALF_PI = 1.57079632679489662f;

        uint32 TileSalt(int x, int y)
        {
            return static_cast<uint32>(x) * 73856093u ^ static_cast<uint32>(y) * 19349663u;
        }

        int DistSqToEntrance(PDLayout const& layout, float tileX, float tileY)
        {
            Room const* entrance = layout.GetRoom(layout.entranceRoomId);
            if (!entrance)
            {
                return 0;
            }
            float const dx = tileX - static_cast<float>(entrance->CenterX());
            float const dy = tileY - static_cast<float>(entrance->CenterY());
            return static_cast<int>(dx * dx + dy * dy);
        }

        // Emit the palette pieces for one straight wall run, laid FLUSH by
        // physical model length (not tiled), and burying each end that abuts a
        // perpendicular wall one tile INTO it (tile-sharing) plus an optional
        // config micro-push. Flush anchoring closes Long->Short seams and, for
        // all-Long runs at TileSize 9.5 (28.5 = 3*9.5), reproduces the old
        // pixel-perfect tiling exactly. Deterministic: integer tile inputs, the
        // fixed TileSalt, and config constants only. NOT cumulative (the old
        // drift-prone per-joint pull is gone); the extension touches terminal
        // pieces only, so interior Long|Long joints stay untouched.
        void AddWallRun(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out, WallRun const& run)
        {
            bool const horizontal = run.horizontal;
            int const axisStart = horizontal ? run.startX : run.startY;
            int const perpTile = horizontal ? run.startY : run.startX;

            // Fold the shared junction tile(s) into the coverage: the
            // perpendicular through-run already owns that tile; we deliberately
            // overlap it so the two tapered bases meet at player height.
            int const lowTile = axisStart - (run.abutLow ? 1 : 0);
            int const covered = run.length + (run.abutLow ? 1 : 0) + (run.abutHigh ? 1 : 0);

            // World coord of the OUTER (low) edge of lowTile plus the fixed
            // perpendicular tile-center coordinate.
            float edgeX = 0.0f;
            float edgeY = 0.0f;
            PDWorldBuilder::TileToWorld(config, layout,
                horizontal ? static_cast<float>(lowTile) : perpTile + 0.5f,
                horizontal ? perpTile + 0.5f : static_cast<float>(lowTile),
                edgeX, edgeY);
            float const perpWorld = horizontal ? edgeY : edgeX;
            float cursor = horizontal ? edgeX : edgeY;   // advances along the axis

            std::vector<size_t> emitted;
            int remaining = covered;
            while (remaining > 0)
            {
                int const placedFromLow = covered - remaining;
                PalettePiece const* piece = sPDPaletteMgr->GetWallPiece(
                    static_cast<uint8>(std::min(remaining, 255)), TileSalt(lowTile + placedFromLow, perpTile));
                if (!piece)
                {
                    return;
                }

                float const len = piece->lengthYd;         // physical length
                float const centerAxis = cursor + len * 0.5f;  // FLUSH: inner face glued to previous
                float const centerTileX = horizontal
                    ? (centerAxis - config.centerX) / config.tileSize + layout.width * 0.5f
                    : perpTile + 0.5f;
                float const centerTileY = horizontal
                    ? perpTile + 0.5f
                    : (centerAxis - config.centerY) / config.tileSize + layout.height * 0.5f;

                PlannedSpawn spawn;
                spawn.entry = piece->goEntry;
                if (horizontal)
                {
                    spawn.x = centerAxis;
                    spawn.y = perpWorld;
                }
                else
                {
                    spawn.x = perpWorld;
                    spawn.y = centerAxis;
                }
                spawn.o = (horizontal ? 0.0f : HALF_PI) + piece->rotOffset;
                spawn.zOffset = piece->zOffset;
                spawn.requiresCollision = true;
                spawn.sortKey = DistSqToEntrance(layout, centerTileX, centerTileY);

                emitted.push_back(out.size());
                out.push_back(spawn);

                cursor += len;                    // flush: next piece starts here
                remaining -= piece->lenTiles;     // slot accounting stays on tiles
            }

            // Optional micro-push past flush, terminal pieces only, outward along
            // the axis (into the perpendicular wall). Interior joints untouched.
            float const extend = config.wallJunctionExtend;
            if (extend > 0.0f && !emitted.empty())
            {
                if (run.abutLow)
                {
                    PlannedSpawn& lo = out[emitted.front()];
                    (horizontal ? lo.x : lo.y) -= extend;
                }
                if (run.abutHigh)
                {
                    PlannedSpawn& hi = out[emitted.back()];
                    (horizontal ? hi.x : hi.y) += extend;
                }
            }
        }

        void BuildWalls(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out)
        {
            // Axis-partitioned run detection (engine-free, deterministic): every
            // Wall tile is placed with a piece along its OWN wall line, so the
            // horizontal-first greedy can no longer drop a piece rotated 90 deg
            // at a junction (the "verdrehte kleine Waende" bug).
            std::vector<WallRun> const runs = BuildWallRuns(layout);
            for (WallRun const& run : runs)
            {
                AddWallRun(layout, config, out, run);
            }
        }

        void BuildGates(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out)
        {
            // One gate per doorway group, but ONLY where the doorway is a genuine
            // opening: a Doorway tile with RoomFloor on one side and a corridor
            // on the exact opposite side, whose run is capped by wall at both
            // span ends. Blobs that merely hug a room edge or turn a corner yield
            // no such run and get no gate (fixes stray gates). The gate axis and
            // width come from that local opening, not the blob centroid/bbox, so
            // a junction gate is neither mis-placed nor rotated 90 deg wrong.
            // doorGroupId/roomId are preserved so the room-clear gating is
            // untouched: a doorway that now gets no gate simply has no GUID under
            // its group, and opening a gate-less group is a harmless no-op.
            for (size_t group = 0; group < layout.doorways.size(); ++group)
            {
                Doorway const& doorway = layout.doorways[group];
                if (doorway.tiles.empty())
                {
                    continue;
                }

                GateOpening const opening = FindGateOpening(layout, doorway);
                if (!opening.valid)
                {
                    continue;
                }

                PalettePiece const* piece = sPDPaletteMgr->GetPiece(PaletteRole::Gate, TileSalt(opening.anchorX, opening.anchorY));
                if (!piece)
                {
                    continue;
                }

                PlannedSpawn spawn;
                spawn.entry = piece->goEntry;
                PDWorldBuilder::TileToWorld(config, layout, opening.centerTileX, opening.centerTileY, spawn.x, spawn.y);
                // Orient across the REAL opening: spanAlongX is the local through-
                // axis (perpendicular to the corridor direction). rot_offset
                // (operator-calibrated, gate = 1.5708) composes on top.
                spawn.o = (opening.spanAlongX ? 0.0f : HALF_PI) + piece->rotOffset;
                spawn.zOffset = piece->zOffset;
                // Gate is an M2 -> its size scales (walls, being WMO, cannot).
                // Cover the full opening: operator calibration is template size=2
                // spanning a 2-tile doorway at TileSize, i.e. 1 tile of span per
                // scale unit => scale == span tiles.
                spawn.scale = static_cast<float>(opening.spanTiles);
                spawn.requiresCollision = true;
                spawn.doorGroupId = static_cast<uint32>(group) + 1;
                spawn.roomId = doorway.roomId;
                spawn.sortKey = DistSqToEntrance(layout, opening.centerTileX, opening.centerTileY);
                out.push_back(spawn);
            }
        }

        PaletteRole DecorRole(DecorKind kind)
        {
            switch (kind)
            {
                case DecorKind::Torch:
                    return PaletteRole::Torch;
                case DecorKind::Brazier:
                    return PaletteRole::Brazier;
                case DecorKind::Chest:
                    return PaletteRole::Chest;
                case DecorKind::Shrine:
                    return PaletteRole::Shrine;
                case DecorKind::ExitPortal:
                    return PaletteRole::ExitPortal;
                case DecorKind::EntrancePad:
                    return PaletteRole::EntranceDeco;
            }
            return PaletteRole::Torch;
        }

        void BuildDecorations(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& initialSpawns, std::vector<PlannedSpawn>& completionSpawns)
        {
            for (Decoration const& decor : layout.decorations)
            {
                PalettePiece const* piece = sPDPaletteMgr->GetPiece(DecorRole(decor.kind), TileSalt(decor.x, decor.y));
                if (!piece)
                {
                    continue;
                }

                PlannedSpawn spawn;
                spawn.entry = piece->goEntry;
                PDWorldBuilder::TileToWorld(config, layout, decor.x + 0.5f, decor.y + 0.5f, spawn.x, spawn.y);
                spawn.o = decor.facing * HALF_PI + piece->rotOffset;
                spawn.zOffset = piece->zOffset;
                spawn.roomId = decor.roomId;
                spawn.sortKey = DistSqToEntrance(layout, static_cast<float>(decor.x), static_cast<float>(decor.y));

                // The exit portal and the boss room chest only appear once the
                // boss is dead.
                bool const onCompletion = decor.kind == DecorKind::ExitPortal ||
                    (decor.kind == DecorKind::Chest && decor.roomId == layout.bossRoomId);
                if (onCompletion)
                {
                    completionSpawns.push_back(spawn);
                }
                else
                {
                    initialSpawns.push_back(spawn);
                }
            }
        }

        uint32 MobEntry(MobKind kind)
        {
            switch (kind)
            {
                case MobKind::Melee:
                    return NPC_TRASH_MELEE;
                case MobKind::Caster:
                    return NPC_TRASH_CASTER;
                case MobKind::Elite:
                    return NPC_ELITE;
                case MobKind::Boss:
                    return NPC_BOSS;
            }
            return NPC_TRASH_MELEE;
        }

        void BuildCreatures(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out)
        {
            for (SpawnPoint const& point : layout.spawnPoints)
            {
                PlannedSpawn spawn;
                spawn.isCreature = true;
                spawn.entry = MobEntry(point.kind);
                spawn.mobKind = point.kind;
                spawn.roomId = point.roomId;
                PDWorldBuilder::TileToWorld(config, layout, point.x + 0.5f, point.y + 0.5f, spawn.x, spawn.y);

                // Face the room center for a natural look.
                if (Room const* room = layout.GetRoom(point.roomId))
                {
                    float centerX = 0.0f;
                    float centerY = 0.0f;
                    PDWorldBuilder::TileToWorld(config, layout, room->CenterX() + 0.5f, room->CenterY() + 0.5f, centerX, centerY);
                    spawn.o = std::atan2(centerY - spawn.y, centerX - spawn.x);
                }

                // Creatures spawn after all geometry.
                spawn.sortKey = 1000000 + DistSqToEntrance(layout, static_cast<float>(point.x), static_cast<float>(point.y));
                out.push_back(spawn);
            }
        }
    }

    void PDWorldBuilder::TileToWorld(PDConfig const& config, PDLayout const& layout, float tileX, float tileY, float& worldX, float& worldY)
    {
        worldX = config.centerX + (tileX - layout.width * 0.5f) * config.tileSize;
        worldY = config.centerY + (tileY - layout.height * 0.5f) * config.tileSize;
    }

    bool PDWorldBuilder::WorldToTile(PDConfig const& config, PDLayout const& layout, float worldX, float worldY, int& tileX, int& tileY)
    {
        tileX = static_cast<int>(std::floor((worldX - config.centerX) / config.tileSize + layout.width * 0.5f));
        tileY = static_cast<int>(std::floor((worldY - config.centerY) / config.tileSize + layout.height * 0.5f));
        return layout.InBounds(tileX, tileY);
    }

    void PDWorldBuilder::Build(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& initialSpawns, std::vector<PlannedSpawn>& completionSpawns)
    {
        initialSpawns.clear();
        completionSpawns.clear();

        BuildWalls(layout, config, initialSpawns);
        BuildGates(layout, config, initialSpawns);
        BuildDecorations(layout, config, initialSpawns, completionSpawns);
        BuildCreatures(layout, config, initialSpawns);

        std::stable_sort(initialSpawns.begin(), initialSpawns.end(), [](PlannedSpawn const& lhs, PlannedSpawn const& rhs)
        {
            return lhs.sortKey < rhs.sortKey;
        });
    }
}
