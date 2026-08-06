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

#ifndef MOD_PDUNGEON_V2_INSTANCE_SCRIPT_H
#define MOD_PDUNGEON_V2_INSTANCE_SCRIPT_H

#include "InstanceScript.h"
#include "generator/PDBlockPlan.h"
#include "generator/PDv2WalkGrid.h"

#include <cstdint>
#include <vector>

class InstanceMap;
class Player;

namespace PDungeon
{
    // One PDv2 run.
    //
    // The map has no terrain server-side, which changes what an instance script
    // has to do here. Two consequences drive this class:
    //
    //   * the server does not know where the floor is, so a player who steps off
    //     a platform falls for ever. Nothing in the core will stop them, because
    //     there is no ground to hit. The fall catcher below IS the floor, as far
    //     as the server is concerned.
    //   * creatures cannot be placed by asking the map for a height. They are
    //     placed at the plan's own block coordinates instead, at the kit's floor
    //     plane - the same number the client's terrain was generated at.
    //
    // Attached by PDv2MapScript through OnBeforeCreateInstanceScript, so no
    // instance_template.ScriptId binding is needed.
    class PDv2InstanceScript : public InstanceScript
    {
    public:
        explicit PDv2InstanceScript(InstanceMap* map);

        void Initialize() override { }
        void Update(uint32 diff) override;
        void OnPlayerEnter(Player* player) override;

        // The walkable surface of this instance's plan, or nullptr while no
        // plan is bound yet (or its masks are missing). The creature AI paths
        // over this; it is built once on first entry and read-only afterwards,
        // and AI updates run on this map's own update thread, so no lock.
        WalkGrid const* GetWalkGrid() const { return _gridReady ? &_grid : nullptr; }

    private:
        void SpawnFromPlan(BlockPlan const& plan);
        void EnsureWalkGrid(BlockPlan const& plan);
        void CatchFallers();

        uint32_t _accountId = 0;
        bool     _spawned = false;
        uint32   _fallCheckTimer = 0;
        float    _entranceX = 0.0f;
        float    _entranceY = 0.0f;
        float    _entranceZ = 0.0f;
        bool     _haveEntrance = false;
        WalkGrid _grid;
        bool     _gridReady = false;
        bool     _gridTried = false;
    };
}

#endif
