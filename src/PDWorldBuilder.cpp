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

        void AddWallPiece(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out, int startX, int startY, int length, bool horizontal)
        {
            int position = 0;
            while (position < length)
            {
                int const remaining = length - position;
                PalettePiece const* piece = sPDPaletteMgr->GetWallPiece(static_cast<uint8>(std::min(remaining, 255)), TileSalt(startX + position, startY + position));
                if (!piece)
                {
                    return;
                }

                int const tileX = horizontal ? startX + position : startX;
                int const tileY = horizontal ? startY : startY + position;
                float const centerTileX = horizontal ? tileX + piece->lenTiles * 0.5f : tileX + 0.5f;
                float const centerTileY = horizontal ? tileY + 0.5f : tileY + piece->lenTiles * 0.5f;

                PlannedSpawn spawn;
                spawn.entry = piece->goEntry;
                PDWorldBuilder::TileToWorld(config, layout, centerTileX, centerTileY, spawn.x, spawn.y);
                spawn.o = (horizontal ? 0.0f : HALF_PI) + piece->rotOffset;
                spawn.zOffset = piece->zOffset;
                spawn.requiresCollision = true;
                spawn.sortKey = DistSqToEntrance(layout, centerTileX, centerTileY);
                out.push_back(spawn);

                position += piece->lenTiles;
            }
        }

        void BuildWalls(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out)
        {
            std::vector<bool> consumed(static_cast<size_t>(layout.width) * layout.height, false);
            auto isFreeWall = [&](int x, int y)
            {
                return layout.At(x, y) == TileType::Wall && !consumed[static_cast<size_t>(y) * layout.width + x];
            };
            auto consume = [&](int x, int y)
            {
                consumed[static_cast<size_t>(y) * layout.width + x] = true;
            };

            // Horizontal runs of at least 2 tiles.
            for (int y = 0; y < layout.height; ++y)
            {
                for (int x = 0; x < layout.width; ++x)
                {
                    if (!isFreeWall(x, y) || !isFreeWall(x + 1, y))
                    {
                        continue;
                    }
                    int length = 0;
                    while (isFreeWall(x + length, y))
                    {
                        consume(x + length, y);
                        ++length;
                    }
                    AddWallPiece(layout, config, out, x, y, length, true);
                    x += length;
                }
            }

            // Vertical runs of at least 2 tiles among the leftovers.
            for (int x = 0; x < layout.width; ++x)
            {
                for (int y = 0; y < layout.height; ++y)
                {
                    if (!isFreeWall(x, y) || !isFreeWall(x, y + 1))
                    {
                        continue;
                    }
                    int length = 0;
                    while (isFreeWall(x, y + length))
                    {
                        consume(x, y + length);
                        ++length;
                    }
                    AddWallPiece(layout, config, out, x, y, length, false);
                    y += length;
                }
            }

            // Isolated single tiles.
            for (int y = 0; y < layout.height; ++y)
            {
                for (int x = 0; x < layout.width; ++x)
                {
                    if (isFreeWall(x, y))
                    {
                        consume(x, y);
                        AddWallPiece(layout, config, out, x, y, 1, true);
                    }
                }
            }
        }

        void BuildGates(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& out)
        {
            for (size_t group = 0; group < layout.doorways.size(); ++group)
            {
                Doorway const& doorway = layout.doorways[group];
                for (TilePos const& tile : doorway.tiles)
                {
                    PalettePiece const* piece = sPDPaletteMgr->GetPiece(PaletteRole::Gate, TileSalt(tile.x, tile.y));
                    if (!piece)
                    {
                        continue;
                    }
                    PlannedSpawn spawn;
                    spawn.entry = piece->goEntry;
                    PDWorldBuilder::TileToWorld(config, layout, tile.x + 0.5f, tile.y + 0.5f, spawn.x, spawn.y);
                    spawn.o = (doorway.spanAlongX ? 0.0f : HALF_PI) + piece->rotOffset;
                    spawn.zOffset = piece->zOffset;
                    spawn.requiresCollision = true;
                    spawn.doorGroupId = static_cast<uint32>(group) + 1;
                    spawn.roomId = doorway.roomId;
                    spawn.sortKey = DistSqToEntrance(layout, static_cast<float>(tile.x), static_cast<float>(tile.y));
                    out.push_back(spawn);
                }
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
