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

#ifndef MOD_PDUNGEON_MGR_H
#define MOD_PDUNGEON_MGR_H

#include "ObjectGuid.h"
#include "generator/PDGenTypes.h"
#include <mutex>
#include <string>
#include <unordered_map>

class Player;

namespace PDungeon
{
    struct PDConfig
    {
        bool enabled = false;
        uint32_t baseMapId = 37;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float centerZ = 0.0f;
        float orientation = 0.0f;
        float tileSize = 10.0f;
        float wallOverlap = 1.75f;     // yards each straight wall piece is pulled into its neighbour
        float wallJunctionExtend = 0.0f; // extra yards a wall run's END piece buries into a
                                         // perpendicular wall/corner, on top of the built-in
                                         // one-tile tile-share; terminal pieces only, never cumulative
        float gateWidthExtra = 0.2f;     // extra scale added to a gate's opening-span width so it
                                         // overlaps the flanking (tapered) walls and can't be skipped
        GenConfig gen;                 // seed field is per-run
        uint32_t spawnBatchSize = 50;
        uint32_t maxGameObjects = 1500;
        int mobLevel = 80;
        int leashTiles = 18;
        uint32_t shrineSpellId = 48161; // Power Word: Fortitude as harmless default
        std::string theme = "wg";
        bool announce = true;
        bool debug = false;
    };

    // Module singleton: cached config plus the pending-seed handshake between
    // the teleport initiator (command/gossip) and the instance script that is
    // created afterwards on the target map's thread.
    class PDMgr
    {
    public:
        static PDMgr* instance();

        void LoadConfig();

        PDConfig const& GetConfig() const { return _config; }
        bool IsEnabled() const { return _config.enabled; }

        void SetPendingSeed(ObjectGuid guid, uint32_t seed);
        bool ConsumePendingSeed(ObjectGuid guid, uint32_t& seed);

        // Unbinds a stale run and teleports the player to the base map. The
        // fresh InstanceMap created there picks the pending seed up again.
        bool StartRun(Player* player, uint32_t seed, bool gmMode);

    private:
        PDConfig _config;
        std::mutex _pendingLock;
        std::unordered_map<ObjectGuid, uint32_t> _pendingSeeds;
    };
}

#define sPDMgr PDungeon::PDMgr::instance()

#endif
