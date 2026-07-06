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

#ifndef MOD_PDUNGEON_DEFINES_H
#define MOD_PDUNGEON_DEFINES_H

#include "Define.h"

namespace PDungeon
{
    // Reserved project id blocks (registered in share-public 06-custom-ids.md):
    // gameobject_template 910000-910099, creature_template 910500-910549.
    enum PDGameObjectEntries : uint32
    {
        GO_WALL_LONG     = 910000,  // 3 tiles
        GO_WALL_ALT      = 910001,  // 3 tiles, visual variant
        GO_WALL_SHORT    = 910002,  // 2 tiles
        GO_WALL_END      = 910003,  // 1 tile
        GO_GATE          = 910010,
        GO_TORCH         = 910020,
        GO_BRAZIER       = 910021,
        GO_CHEST         = 910030,
        GO_SHRINE        = 910031,
        GO_EXIT_PORTAL   = 910032,
        GO_ENTRANCE_DECO = 910033
    };

    enum PDCreatureEntries : uint32
    {
        NPC_TRASH_MELEE  = 910500,
        NPC_TRASH_CASTER = 910501,
        NPC_ELITE        = 910502,
        NPC_BOSS         = 910503,
        NPC_ENTRANCE     = 910510
    };

    enum PDSpells : uint32
    {
        SPELL_SHADOW_BOLT = 20791,  // generic mob shadow bolt
        SPELL_WAR_STOMP   = 24375   // boss aoe stomp
    };

    enum PDMisc : uint32
    {
        WAYPOINT_MOVE_ID_BASE = 910000,  // MovePoint ids used by the module AI
        VALIDATE_DESPAWN_SECS = 15
    };

    char const* const PD_LOG = "module.pdungeon";
}

#endif
