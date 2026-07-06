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

#include "PDMgr.h"
#include "Chat.h"
#include "Config.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "Player.h"
#include "PDDefines.h"

namespace PDungeon
{
    PDMgr* PDMgr::instance()
    {
        static PDMgr instance;
        return &instance;
    }

    void PDMgr::LoadConfig()
    {
        _config.enabled = sConfigMgr->GetOption<bool>("ProceduralDungeon.Enable", false);
        _config.baseMapId = sConfigMgr->GetOption<uint32>("ProceduralDungeon.BaseMapId", 37);
        _config.centerX = sConfigMgr->GetOption<float>("ProceduralDungeon.Center.X", 0.0f);
        _config.centerY = sConfigMgr->GetOption<float>("ProceduralDungeon.Center.Y", 0.0f);
        _config.centerZ = sConfigMgr->GetOption<float>("ProceduralDungeon.Center.Z", 0.0f);
        _config.orientation = sConfigMgr->GetOption<float>("ProceduralDungeon.Center.O", 0.0f);
        _config.tileSize = sConfigMgr->GetOption<float>("ProceduralDungeon.TileSize", 10.0f);
        _config.gen.gridWidth = sConfigMgr->GetOption<int>("ProceduralDungeon.Grid.Width", 64);
        _config.gen.gridHeight = sConfigMgr->GetOption<int>("ProceduralDungeon.Grid.Height", 64);
        _config.gen.roomsMin = sConfigMgr->GetOption<int>("ProceduralDungeon.Rooms.Min", 10);
        _config.gen.roomsMax = sConfigMgr->GetOption<int>("ProceduralDungeon.Rooms.Max", 16);
        _config.gen.roomSizeMin = sConfigMgr->GetOption<int>("ProceduralDungeon.Rooms.SizeMin", 5);
        _config.gen.roomSizeMax = sConfigMgr->GetOption<int>("ProceduralDungeon.Rooms.SizeMax", 11);
        _config.gen.loopChancePct = sConfigMgr->GetOption<int>("ProceduralDungeon.LoopChance", 15);
        _config.gen.torchEvery = sConfigMgr->GetOption<int>("ProceduralDungeon.TorchEvery", 4);
        _config.gen.packSizeMin = sConfigMgr->GetOption<int>("ProceduralDungeon.Mob.PackSize.Min", 3);
        _config.gen.packSizeMax = sConfigMgr->GetOption<int>("ProceduralDungeon.Mob.PackSize.Max", 5);
        _config.gen.casterChancePct = sConfigMgr->GetOption<int>("ProceduralDungeon.Mob.CasterChance", 30);
        _config.spawnBatchSize = sConfigMgr->GetOption<uint32>("ProceduralDungeon.Spawn.BatchSize", 50);
        _config.maxGameObjects = sConfigMgr->GetOption<uint32>("ProceduralDungeon.Spawn.MaxGameObjects", 1500);
        _config.mobLevel = sConfigMgr->GetOption<int>("ProceduralDungeon.Mob.Level", 0);
        _config.leashTiles = sConfigMgr->GetOption<int>("ProceduralDungeon.Mob.LeashTiles", 18);
        _config.shrineSpellId = sConfigMgr->GetOption<uint32>("ProceduralDungeon.ShrineSpellId", 48161);
        _config.theme = sConfigMgr->GetOption<std::string>("ProceduralDungeon.Theme", "wg");
        _config.announce = sConfigMgr->GetOption<bool>("ProceduralDungeon.Announce", true);
        _config.debug = sConfigMgr->GetOption<bool>("ProceduralDungeon.Debug", false);

        // The theme string ends up in a SQL query; restrict it hard.
        std::string sanitized;
        for (char c : _config.theme)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            {
                sanitized += c;
            }
        }
        if (sanitized.empty())
        {
            sanitized = "wg";
        }
        _config.theme = sanitized;
    }

    void PDMgr::SetPendingSeed(ObjectGuid guid, uint32_t seed)
    {
        std::lock_guard<std::mutex> guard(_pendingLock);
        _pendingSeeds[guid] = seed;
    }

    bool PDMgr::ConsumePendingSeed(ObjectGuid guid, uint32_t& seed)
    {
        std::lock_guard<std::mutex> guard(_pendingLock);
        auto const itr = _pendingSeeds.find(guid);
        if (itr == _pendingSeeds.end())
        {
            return false;
        }
        seed = itr->second;
        _pendingSeeds.erase(itr);
        return true;
    }

    bool PDMgr::StartRun(Player* player, uint32_t seed, bool gmMode)
    {
        if (!player)
        {
            return false;
        }

        ChatHandler handler(player->GetSession());
        if (!_config.enabled)
        {
            handler.SendSysMessage("Procedural dungeons are disabled.");
            return false;
        }
        if (player->GetMapId() == _config.baseMapId)
        {
            handler.SendSysMessage("You are already inside a procedural dungeon.");
            return false;
        }

        // Drop a stale bind so a fresh instance (and layout) is created.
        sInstanceSaveMgr->PlayerUnbindInstance(player->GetGUID(), _config.baseMapId, player->GetDungeonDifficulty(), true, player);

        SetPendingSeed(player->GetGUID(), seed);

        uint32 const options = gmMode ? TELE_TO_GM_MODE : 0;
        if (!player->TeleportTo(_config.baseMapId, _config.centerX, _config.centerY, _config.centerZ + 2.0f, _config.orientation, options))
        {
            ChatHandler(player->GetSession()).SendSysMessage("Teleport into the procedural dungeon failed (check instance_template/map_dbc SQL).");
            LOG_ERROR(PD_LOG, "StartRun: TeleportTo map {} failed for player {}", _config.baseMapId, player->GetName());
            return false;
        }

        LOG_DEBUG(PD_LOG, "StartRun: player {} -> map {} seed {}", player->GetName(), _config.baseMapId, seed);
        return true;
    }
}
