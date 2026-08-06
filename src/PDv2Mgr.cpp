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

#include "PDv2Mgr.h"

#include "Config.h"
#include "Log.h"
#include "PDDefines.h"

#include <cstdio>

namespace PDungeon
{
    PDv2Mgr* PDv2Mgr::instance()
    {
        static PDv2Mgr mgr;
        return &mgr;
    }

    void PDv2Mgr::LoadConfig()
    {
        _config.enabled = sConfigMgr->GetOption<bool>("ProceduralDungeon.V2.Enable", false);
        _config.mapId = sConfigMgr->GetOption<uint32>("ProceduralDungeon.V2.MapId", 760);
        _config.floorZ = sConfigMgr->GetOption<float>("ProceduralDungeon.V2.FloorZ", 50.0f);
        _config.rooms = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.Rooms", 5);
        _config.bossRooms = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.BossRooms", 1);
        _config.fieldBlocks = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.FieldBlocks", 8);
        _config.originBX = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.OriginBX", 256);
        _config.originBY = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.OriginBY", 256);
        _config.loopChancePct = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.LoopChance", 15);
        _config.theme = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.Theme", 1);
        _config.manifestPath = sConfigMgr->GetOption<std::string>(
            "ProceduralDungeon.V2.ManifestPath", "");

        LOG_INFO(PD_LOG, "PDv2: {} map {} floorZ {} rooms {}+{} field {} origin ({},{})",
                 _config.enabled ? "enabled" : "disabled", _config.mapId, _config.floorZ,
                 _config.rooms, _config.bossRooms, _config.fieldBlocks,
                 _config.originBX, _config.originBY);
        if (_config.enabled && _config.manifestPath.empty())
        {
            LOG_WARN(PD_LOG, "PDv2: ProceduralDungeon.V2.ManifestPath is empty - `.pdungeon v2 gen` "
                             "can plan but cannot hand the manifest to the client");
        }
    }

    bool PDv2Mgr::GeneratePlan(uint32_t accountId, uint32_t seed, BlockPlan& out)
    {
        BlockCfg cfg;
        cfg.seed = seed;
        cfg.rooms = _config.rooms;
        cfg.bossRooms = _config.bossRooms;
        cfg.fieldBlocks = _config.fieldBlocks;
        cfg.loopChancePct = _config.loopChancePct;
        cfg.originBX = _config.originBX;
        cfg.originBY = _config.originBY;
        cfg.theme = _config.theme;

        if (!GenerateBlockPlan(cfg, &out))
        {
            LOG_ERROR(PD_LOG, "PDv2: no valid layout for seed {} after {} tries", seed, cfg.maxTries);
            return false;
        }

        std::lock_guard<std::mutex> guard(_lock);
        _plans[accountId] = out;
        return true;
    }

    BlockPlan const* PDv2Mgr::GetPlan(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _plans.find(accountId);
        return it == _plans.end() ? nullptr : &it->second;
    }

    bool PDv2Mgr::WriteManifest(BlockPlan const& plan, uint32_t seq, std::string& pathOut,
                                std::string& error) const
    {
        if (_config.manifestPath.empty())
        {
            error = "ProceduralDungeon.V2.ManifestPath is not set";
            return false;
        }

        std::string const text = EmitManifest(plan, static_cast<int>(seq));

        // Binary mode on purpose: the manifest is LF separated and both parsers
        // reject CR, but a text-mode stream on Windows rewrites every \n.
        FILE* fh = std::fopen(_config.manifestPath.c_str(), "wb");
        if (!fh)
        {
            error = "cannot open " + _config.manifestPath + " for writing";
            return false;
        }
        size_t const written = std::fwrite(text.data(), 1, text.size(), fh);
        std::fclose(fh);
        if (written != text.size())
        {
            error = "short write to " + _config.manifestPath;
            return false;
        }

        pathOut = _config.manifestPath;
        return true;
    }

    void PDv2Mgr::BlockToWorld(int bx, int by, double u, double v,
                               float& x, float& y, float& z) const
    {
        // Mirrors pd_adt_lib: tile (tx, ty) has its world corner at
        // ((32 - ty) * TILE, (32 - tx) * TILE); u runs south (-X) and v east (-Y)
        // from that corner, block by block.
        int const tx = bx / PD_BLOCKS_PER_TILE;
        int const ty = by / PD_BLOCKS_PER_TILE;
        int const blockCol = bx % PD_BLOCKS_PER_TILE;
        int const blockRow = by % PD_BLOCKS_PER_TILE;

        double const xmax = (32.0 - static_cast<double>(ty)) * PD_TILE_SIZE_YD;
        double const ymax = (32.0 - static_cast<double>(tx)) * PD_TILE_SIZE_YD;

        x = static_cast<float>(xmax - (static_cast<double>(blockRow) * PD_BLOCK_SIZE_YD + u));
        y = static_cast<float>(ymax - (static_cast<double>(blockCol) * PD_BLOCK_SIZE_YD + v));
        z = _config.floorZ;
    }

    bool PDv2Mgr::EntranceWorldPos(BlockPlan const& plan, float& x, float& y, float& z) const
    {
        if (plan.entranceIndex < 0 ||
            plan.entranceIndex >= static_cast<int>(plan.blocks.size()))
        {
            return false;
        }
        PlacedBlock const& b = plan.blocks[static_cast<size_t>(plan.entranceIndex)];
        double const mid = PD_BLOCK_SIZE_YD / 2.0;
        BlockToWorld(b.bx, b.by, mid, mid, x, y, z);
        return true;
    }
}
