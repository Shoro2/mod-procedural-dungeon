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

#include "PDv2InstanceScript.h"

#include "Chat.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "PDDefines.h"
#include "PDv2Mgr.h"
#include "Player.h"
#include "Position.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

#include <cmath>

namespace PDungeon
{
    namespace
    {
        // How far below the floor plane counts as "fallen off the world".
        // Generous, because a client-authoritative jump can dip briefly.
        float const FALL_MARGIN_YD = 60.0f;

        // Checking every tick would be waste; a fall is not urgent to the yard.
        uint32 const FALL_CHECK_INTERVAL_MS = 1000;

        // Placeholder trash until creature packs exist. Chosen because it is a
        // stock, level-appropriate humanoid every client already has art for.
        uint32 const PLACEHOLDER_CREATURE = 29402;   // Anub'ar Skirmisher

        int const SPAWNS_PER_ROOM = 3;
        float const SPAWN_SPREAD_YD = 12.0f;
    }

    PDv2InstanceScript::PDv2InstanceScript(InstanceMap* map) : InstanceScript(map)
    {
    }

    void PDv2InstanceScript::OnPlayerEnter(Player* player)
    {
        if (!player || !player->GetSession())
        {
            return;
        }

        // The plan is per account, and an instance belongs to whoever first
        // walked into it. Binding here rather than at teleport time keeps this
        // script usable no matter how the player got onto the map.
        if (!_accountId)
        {
            _accountId = player->GetSession()->GetAccountId();
        }

        BlockPlan const* plan = sPDv2Mgr->GetPlan(_accountId);
        if (!plan)
        {
            LOG_WARN(PD_LOG, "PDv2: player {} entered map {} with no stored plan - "
                             "nothing to spawn", player->GetName(), instance->GetId());
            return;
        }

        if (sPDv2Mgr->EntranceWorldPos(*plan, _entranceX, _entranceY, _entranceZ))
        {
            _haveEntrance = true;
        }

        // A plan can be re-rolled while this instance is alive - the account
        // keeps the instance, so the old creatures and the old walk grid would
        // otherwise survive under new terrain. Rebuilding on a seed change is
        // what makes `.pdungeon v2 gen` mean the same thing inside as outside.
        if (_spawned && _spawnedSeed != plan->effectiveSeed)
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} was built for seed {}, plan is now {} - "
                             "rebuilding", instance->GetInstanceId(), _spawnedSeed,
                     plan->effectiveSeed);
            DespawnAll();
            _spawned = false;
            _gridReady = false;
            _gridTried = false;
        }

        EnsureWalkGrid(*plan);

        if (!_spawned)
        {
            SpawnFromPlan(*plan);
            _spawned = true;
            _spawnedSeed = plan->effectiveSeed;
        }
    }

    void PDv2InstanceScript::DespawnAll()
    {
        for (ObjectGuid const& guid : _spawnedGuids)
        {
            if (Creature* c = instance->GetCreature(guid))
            {
                c->DespawnOrUnsummon();
            }
        }
        _spawnedGuids.clear();
    }

    void PDv2InstanceScript::EnsureWalkGrid(BlockPlan const& plan)
    {
        if (_gridTried)
        {
            return;
        }
        _gridTried = true;

        std::string error;
        if (!BuildWalkGrid(plan, [](int chunkId) { return sPDv2Mgr->WalkMaskFor(chunkId); },
                           &_grid, &error))
        {
            // Without the grid the mobs stand where they spawned and never
            // chase - the dungeon degrades, it does not crash. Loud log line
            // because the only known cause is kit metadata that was not
            // applied or does not match the plan's chunk ids.
            LOG_ERROR(PD_LOG, "PDv2: instance {} could not build its walk grid ({}) - "
                              "creatures will not chase", instance->GetInstanceId(), error);
            return;
        }
        _gridReady = true;
        LOG_DEBUG(PD_LOG, "PDv2: instance {} walk grid {}x{} cells, {} walkable",
                  instance->GetInstanceId(), _grid.width, _grid.height,
                  uint32(_grid.WalkableCount()));
    }

    void PDv2InstanceScript::SpawnFromPlan(BlockPlan const& plan)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        uint32 spawned = 0;

        for (PlacedBlock const& b : plan.blocks)
        {
            // Rooms only. A corridor is 8.3 yd wide, so anything standing in one
            // would be shoulder to shoulder with the walls.
            if (b.roomId < 0 || b.role == BlockRole::RoomEntrance)
            {
                continue;
            }

            double const mid = PD_BLOCK_SIZE_YD / 2.0;
            for (int i = 0; i < SPAWNS_PER_ROOM; ++i)
            {
                // A small fixed pattern around the block centre. Deliberately
                // NOT random: the same plan must produce the same dungeon, and
                // an unseeded draw here would break that quietly.
                double const angle = 2.0 * 3.14159265358979 * i / SPAWNS_PER_ROOM;
                double const du = std::cos(angle) * SPAWN_SPREAD_YD;
                double const dv = std::sin(angle) * SPAWN_SPREAD_YD;

                float x = 0.0f, y = 0.0f, z = 0.0f;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, mid + du, mid + dv, x, y, z);

                // +0.5 so a creature is not spawned inside the floor plane. The
                // map cannot be asked for a height here - it has none.
                if (Creature* c = instance->SummonCreature(PLACEHOLDER_CREATURE,
                                                           Position(x, y, z + 0.5f, 0.0f)))
                {
                    c->SetHomePosition(x, y, z + 0.5f, 0.0f);

                    // Gravity OFF, and this is not cosmetic: the server has no
                    // terrain here, so Map::GetHeight answers INVALID_HEIGHT
                    // and every creature is permanently "above nothing" - they
                    // fall through the platforms the client draws (operator
                    // report 2026-08-06). Disabling gravity pins them to the
                    // floor plane the kit was generated at, which is the only
                    // floor the server knows. Movement is unaffected: the walk
                    // grid drives it and every waypoint carries the same Z.
                    c->SetDisableGravity(true);
                    _spawnedGuids.push_back(c->GetGUID());
                    ++spawned;
                }
            }
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} on map {} spawned {} creature(s) from a "
                         "{}-block plan", instance->GetInstanceId(), instance->GetId(),
                 spawned, uint32(plan.blocks.size()));
        (void)cfg;
    }

    void PDv2InstanceScript::CatchFallers()
    {
        if (!_haveEntrance)
        {
            return;
        }

        float const floor = sPDv2Mgr->GetConfig().floorZ;
        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsInWorld())
            {
                continue;
            }
            if (player->GetPositionZ() > floor - FALL_MARGIN_YD)
            {
                continue;
            }

            // Put them back at the entrance rather than killing them. There is
            // no terrain to land on anywhere else, so a corpse run would strand
            // them, and dying to the architecture is not a mechanic.
            player->TeleportTo(instance->GetId(), _entranceX, _entranceY, _entranceZ + 2.0f, 0.0f);
            ChatHandler(player->GetSession()).SendSysMessage(
                "You fell off the dungeon and were returned to the entrance.");
            LOG_DEBUG(PD_LOG, "PDv2: returned {} to the entrance from Z {}",
                      player->GetName(), player->GetPositionZ());
        }
    }

    void PDv2InstanceScript::EvictDisconnected()
    {
        // A client that crashes or is killed leaves its player standing here,
        // alive in memory, for the session's grace period - and a reconnect
        // during that window attaches to THAT player, so the server sends the
        // returning client straight back to map 760 while its fresh DLL has
        // composed nothing yet. The client dies on CMap::LoadWdt, relogs into
        // the same trap, and the DB says nothing about any of it because the
        // live player was never saved (measured 2026-08-06: crash 6 s after
        // injection, no intercept in the DLL log, characters.map already 0).
        //
        // So the eviction has to happen on the LIVE player, and this is the
        // only place that sees it: send anyone whose socket is gone home at
        // once, before a reconnect can find them in here.
        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsInWorld() || !player->GetSession())
            {
                continue;
            }
            if (!player->GetSession()->IsSocketClosed() && !player->GetSession()->IsKicked())
            {
                continue;
            }

            player->TeleportTo(player->m_homebindMapId, player->m_homebindX,
                               player->m_homebindY, player->m_homebindZ,
                               player->GetOrientation());
            LOG_INFO(PD_LOG, "PDv2: evicted {} from the dungeon - the client is gone "
                             "and a reconnect must not land back on this map",
                     player->GetName());
        }
    }

    void PDv2InstanceScript::Update(uint32 diff)
    {
        if (_fallCheckTimer <= diff)
        {
            _fallCheckTimer = FALL_CHECK_INTERVAL_MS;
            CatchFallers();
            EvictDisconnected();
        }
        else
        {
            _fallCheckTimer -= diff;
        }
    }
}
