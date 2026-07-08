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

#ifndef MOD_PDUNGEON_WORLD_BUILDER_H
#define MOD_PDUNGEON_WORLD_BUILDER_H

#include "PDMgr.h"
#include "generator/PDLayout.h"
#include <vector>

namespace PDungeon
{
    struct PlannedSpawn
    {
        bool isCreature = false;
        uint32 entry = 0;
        float x = 0.0f;
        float y = 0.0f;
        float o = 0.0f;
        float zOffset = 0.0f;       // applied on top of the sampled ground height
        float scale = 0.0f;         // >0: SetObjectScale on the GO (gates span their opening); 0 = keep template size
        uint32 doorGroupId = 0;     // 1-based doorway group; 0 = not a gate
        bool requiresCollision = false; // walls/gates: warn if the model is missing
        int roomId = -1;
        MobKind mobKind = MobKind::Melee;
        int sortKey = 0;
    };

    // Translates a generated layout into world-space spawn plans: axis-aware
    // wall runs from the palette (laid flush by model length, terminal pieces
    // buried into perpendicular walls at junctions), one gate per genuine
    // doorway opening (scaled to span its width), decorations and mob packs.
    // Spawns that appear only on completion (boss chest, exit portal) go to a
    // separate list.
    class PDWorldBuilder
    {
    public:
        static void Build(PDLayout const& layout, PDConfig const& config, std::vector<PlannedSpawn>& initialSpawns, std::vector<PlannedSpawn>& completionSpawns);

        // Fractional tile coordinate -> world position. Tile centers sit at
        // tx + 0.5 / ty + 0.5.
        static void TileToWorld(PDConfig const& config, PDLayout const& layout, float tileX, float tileY, float& worldX, float& worldY);
        static bool WorldToTile(PDConfig const& config, PDLayout const& layout, float worldX, float worldY, int& tileX, int& tileY);
    };
}

#endif
