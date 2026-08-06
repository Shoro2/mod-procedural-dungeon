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
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "PDDefines.h"
#include "QueryResult.h"
#include "generator/PDv2WalkGrid.h"

#include <algorithm>
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

        StorePlan(accountId, out);
        SavePlanToDB(accountId, out);
        return true;
    }

    void PDv2Mgr::StorePlan(uint32_t accountId, BlockPlan const& plan)
    {
        std::lock_guard<std::mutex> guard(_lock);
        _plans[accountId] = plan;
    }

    void PDv2Mgr::SavePlanToDB(uint32_t accountId, BlockPlan const& plan)
    {
        BlockCfg const& cfg = plan.config;
        // Layout columns only - dlvl/dxp and the cfg_* gameplay knobs belong
        // to the gameplay slice, and a reroll must never clobber them.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, theme, layout_seed, layout_version, "
            "gen_rooms, gen_boss_rooms, gen_field_blocks, gen_origin_bx, gen_origin_by, "
            "gen_loop_pct) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE theme = VALUES(theme), "
            "layout_seed = VALUES(layout_seed), layout_version = VALUES(layout_version), "
            "gen_rooms = VALUES(gen_rooms), gen_boss_rooms = VALUES(gen_boss_rooms), "
            "gen_field_blocks = VALUES(gen_field_blocks), gen_origin_bx = VALUES(gen_origin_bx), "
            "gen_origin_by = VALUES(gen_origin_by), gen_loop_pct = VALUES(gen_loop_pct)",
            accountId, cfg.theme, cfg.seed, PD_LAYOUT_VERSION, cfg.rooms, cfg.bossRooms,
            cfg.fieldBlocks, cfg.originBX, cfg.originBY, cfg.loopChancePct);
    }

    void PDv2Mgr::LoadPlanFromDB(uint32_t accountId)
    {
        if (GetPlan(accountId))
        {
            return;
        }

        QueryResult result = CharacterDatabase.Query(
            "SELECT layout_seed, layout_version, theme, gen_rooms, gen_boss_rooms, "
            "gen_field_blocks, gen_origin_bx, gen_origin_by, gen_loop_pct "
            "FROM pdungeon_account WHERE accountId = {}", accountId);
        if (!result)
        {
            return;
        }

        Field* fields = result->Fetch();
        uint32_t const seed = fields[0].Get<uint32>();
        uint32_t const version = fields[1].Get<uint32>();
        if (!seed)
        {
            return;
        }
        if (version != PD_LAYOUT_VERSION)
        {
            LOG_INFO(PD_LOG, "PDv2: account {} has a layout stamped v{} (current v{}) - "
                             "kept in the DB, but a reroll is needed",
                     accountId, version, PD_LAYOUT_VERSION);
            return;
        }

        BlockCfg cfg;
        cfg.seed = seed;
        cfg.theme = fields[2].Get<uint8>();
        cfg.rooms = fields[3].Get<uint8>();
        cfg.bossRooms = fields[4].Get<uint8>();
        cfg.fieldBlocks = fields[5].Get<uint8>();
        cfg.originBX = fields[6].Get<uint16>();
        cfg.originBY = fields[7].Get<uint16>();
        cfg.loopChancePct = fields[8].Get<uint8>();

        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
        {
            // Determinism makes this near-impossible for a layout that once
            // generated; if it happens the generator changed without a
            // PD_LAYOUT_VERSION bump, and that is worth shouting about.
            LOG_ERROR(PD_LOG, "PDv2: account {}'s stored layout (seed {}) no longer "
                              "regenerates - PD_LAYOUT_VERSION should have been bumped",
                      accountId, seed);
            return;
        }

        StorePlan(accountId, plan);
        LOG_INFO(PD_LOG, "PDv2: restored account {}'s dungeon from seed {} ({} blocks)",
                 accountId, seed, uint32(plan.blocks.size()));
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
        // The math lives in generator/PDv2WorldMath.h beside its inverse, so
        // the harness can prove the two agree - see the header's rationale.
        double wx = 0.0, wy = 0.0;
        BlockLocalToWorld(bx, by, u, v, wx, wy);
        x = static_cast<float>(wx);
        y = static_cast<float>(wy);
        z = _config.floorZ;
    }

    void PDv2Mgr::LoadChunkMeta()
    {
        _walkMasks.clear();

        // Highest kit version wins per chunk id: rows are read in ascending
        // kitVersion order and later ones overwrite. Today there is exactly
        // one kit, so this is bookkeeping for the day there are two.
        QueryResult result = WorldDatabase.Query(
            "SELECT chunkId, kitVersion, walkMask FROM pdungeon_chunk_meta "
            "WHERE theme = '{}' ORDER BY kitVersion", _config.theme);
        if (!result)
        {
            LOG_ERROR(PD_LOG, "PDv2: pdungeon_chunk_meta has no rows for theme {} - "
                              "mod_pdungeon_chunk_meta.sql was not applied, and no "
                              "walk grid can be built (creatures will not chase)",
                      _config.theme);
            return;
        }

        uint32 bad = 0;
        do
        {
            Field* fields = result->Fetch();
            int const chunkId = static_cast<int>(fields[0].Get<uint32>());
            std::string const rle = fields[2].Get<std::string>();

            std::vector<uint8_t> mask;
            if (!DecodeWalkMaskRle(rle, mask) ||
                mask.size() != PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK)
            {
                LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed walkMask ('{}')",
                          chunkId, rle);
                ++bad;
                continue;
            }

            auto& slot = _walkMasks[chunkId];
            std::copy(mask.begin(), mask.end(), slot.begin());
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} walk mask(s) from pdungeon_chunk_meta "
                         "(theme {}, {} malformed)",
                 uint32(_walkMasks.size()), _config.theme, bad);
    }

    uint8_t const* PDv2Mgr::WalkMaskFor(int chunkId) const
    {
        auto it = _walkMasks.find(chunkId);
        return it == _walkMasks.end() ? nullptr : it->second.data();
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
