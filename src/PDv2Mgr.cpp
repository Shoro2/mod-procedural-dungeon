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

        _config.xpPerRoom = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.XP.PerRoom", 10);
        _config.xpPerDlvl = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.XP.PerDlvl", 100);
        _config.dlvlCap = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.DlvlCap", 30);
        _config.spawnsPerRoom = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.SpawnsPerRoom", 5);
        _config.bossRoomAdds = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.BossRoomAdds", 2);
        _config.lootBonusRollPct = sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.BonusRollPct", 15);
        _config.castRangeYd = sConfigMgr->GetOption<float>("ProceduralDungeon.V2.CastRangeYd", 25.0f);
        _config.casterNukeCooldownMs = sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.CasterNukeCooldownMs", 8000);
        _config.aggroRangeYd = sConfigMgr->GetOption<float>("ProceduralDungeon.V2.AggroRangeYd", 20.0f);

        // Clamped at 0 on the way in: a negative percentage would make a HARDER
        // dungeon hit softer, and at difficulty 100 it would drive the x100
        // multiplier below zero and underflow the unsigned arithmetic the
        // scaling hooks do. There is no upper clamp - an operator who wants a
        // brutal curve is entitled to one.
        _config.diffHealthPctPerLevel = std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Diff.HealthPctPerLevel", 5));
        _config.diffDamagePctPerLevel = std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Diff.DamagePctPerLevel", 2));

        // Clamped into [0, 100] because it is handed straight to a percent
        // roll: 0 means "no affixed mobs", 100 means "all of them", and there
        // is nothing sensible outside that.
        _config.affixPct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Affix.Percentage", 40)));

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
        PDv2AccountState const state = GetAccountState(accountId);
        int const dlvl = static_cast<int>(state.dlvl);

        BlockCfg cfg;
        cfg.seed = seed;
        // 01 §8: the shape of the dungeon follows the account's progression and
        // its chosen room count, clamped into the band that dlvl unlocked. The
        // server config stays as the fallback for an account with no row, so a
        // fresh install still generates what the operator configured.
        cfg.rooms = state.loaded ? GameClampRooms(state.cfgRooms, dlvl)
                                 : GameClampRooms(_config.rooms, dlvl);
        cfg.bossRooms = state.loaded ? GameBossRooms(dlvl) : _config.bossRooms;
        // fieldBlocks is NOT derived from the room count: 8 blocks is exactly
        // one ADT tile, and a multi-tile plan is untested on the client side.
        // The room cap (PD_GAME_ROOMS_CAP_MEASURED) is what keeps the layout
        // inside this field instead - see the measurement in PDv2GameMath.h.
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
        // A fresh immutable object every time, replaced under the lock: readers
        // that fetched the previous shared_ptr keep a complete, never-mutated
        // plan for as long as they hold it. That is the whole fix for the
        // pointer-into-the-map pattern this replaced - a re-roll can no longer
        // pull the plan out from under a map thread mid-read.
        auto fresh = std::make_shared<BlockPlan const>(plan);
        std::lock_guard<std::mutex> guard(_lock);
        _plans[accountId] = std::move(fresh);
    }

    void PDv2Mgr::SavePlanToDB(uint32_t accountId, BlockPlan const& plan)
    {
        BlockCfg const& cfg = plan.config;
        PDv2AccountState const state = GetAccountState(accountId);
        std::string packs = state.cfgPacks;
        CharacterDatabase.EscapeString(packs);

        // Layout columns only on UPDATE - dlvl/dxp and the cfg_* gameplay knobs
        // belong to the gameplay slice, and a reroll must never clobber them.
        //
        // The cfg_* values appear in the INSERT half all the same, and only
        // there: when this statement CREATES the row it would otherwise write
        // the column defaults, and cfg_mob_level_min's default of 1 selects no
        // pack at all against v1's level-80 stock. Seeding the row from the
        // cached state keeps a first `v2 gen` from silently disagreeing with
        // the state the server has been using since login.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, theme, layout_seed, layout_version, "
            "gen_rooms, gen_boss_rooms, gen_field_blocks, gen_origin_bx, gen_origin_by, "
            "gen_loop_pct, cfg_rooms, cfg_difficulty, cfg_caster_pct, cfg_mob_level_min, "
            "cfg_packs) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}') "
            "ON DUPLICATE KEY UPDATE theme = VALUES(theme), "
            "layout_seed = VALUES(layout_seed), layout_version = VALUES(layout_version), "
            "gen_rooms = VALUES(gen_rooms), gen_boss_rooms = VALUES(gen_boss_rooms), "
            "gen_field_blocks = VALUES(gen_field_blocks), gen_origin_bx = VALUES(gen_origin_bx), "
            "gen_origin_by = VALUES(gen_origin_by), gen_loop_pct = VALUES(gen_loop_pct)",
            accountId, cfg.theme, cfg.seed, PD_LAYOUT_VERSION, cfg.rooms, cfg.bossRooms,
            cfg.fieldBlocks, cfg.originBX, cfg.originBY, cfg.loopChancePct,
            state.cfgRooms, state.cfgDifficulty, state.cfgCasterPct, state.cfgBandMin, packs);
    }

    void PDv2Mgr::LoadAccountState(uint32_t accountId)
    {
        {
            std::lock_guard<std::mutex> guard(_lock);
            if (_accounts.find(accountId) != _accounts.end())
            {
                return;
            }
        }

        PDv2AccountState state;
        QueryResult result = CharacterDatabase.Query(
            "SELECT dlvl, dxp, cfg_rooms, cfg_difficulty, cfg_caster_pct, cfg_mob_level_min, "
            "cfg_packs FROM pdungeon_account WHERE accountId = {}", accountId);
        if (result)
        {
            Field* fields = result->Fetch();
            state.dlvl = fields[0].Get<uint32>();
            state.dxp = fields[1].Get<uint32>();
            state.cfgRooms = fields[2].Get<uint8>();
            state.cfgDifficulty = fields[3].Get<uint8>();
            state.cfgCasterPct = fields[4].Get<uint8>();
            state.cfgBandMin = fields[5].Get<uint8>();
            state.cfgPacks = fields[6].Get<std::string>();
            state.loaded = true;
        }

        // Clamp on the way IN, not only on the way out. A row edited by hand,
        // or written before a band or grid rule changed, must not be able to
        // put an illegal value into a live dungeon - and dlvl in particular
        // decides what the other clamps even allow.
        if (state.dlvl > static_cast<uint32_t>(_config.dlvlCap > 0 ? _config.dlvlCap : 0))
        {
            state.dlvl = static_cast<uint32_t>(_config.dlvlCap > 0 ? _config.dlvlCap : 0);
        }
        int const dlvl = static_cast<int>(state.dlvl);
        state.cfgRooms = GameClampRooms(state.cfgRooms, dlvl);
        // No dlvl argument any more: the dial is open from the first run, so
        // the only illegal difficulty is one outside [1, 100].
        state.cfgDifficulty = GameClampDiff(state.cfgDifficulty);
        state.cfgCasterPct = GameClampCasterPct(state.cfgCasterPct);
        state.cfgBandMin = GameClampBandMin(state.cfgBandMin);

        std::lock_guard<std::mutex> guard(_lock);
        _accounts[accountId] = state;
    }

    PDv2AccountState PDv2Mgr::GetAccountState(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _accounts.find(accountId);
        return it == _accounts.end() ? PDv2AccountState() : it->second;
    }

    void PDv2Mgr::SetAccountCfg(uint32_t accountId, PDv2AccountState const& cfg)
    {
        std::lock_guard<std::mutex> guard(_lock);
        PDv2AccountState& state = _accounts[accountId];
        int const dlvl = static_cast<int>(state.dlvl);
        state.cfgRooms = GameClampRooms(cfg.cfgRooms, dlvl);
        state.cfgDifficulty = GameClampDiff(cfg.cfgDifficulty);
        state.cfgCasterPct = GameClampCasterPct(cfg.cfgCasterPct);
        state.cfgBandMin = GameClampBandMin(cfg.cfgBandMin);
        state.cfgPacks = cfg.cfgPacks;
        state.loaded = true;
    }

    void PDv2Mgr::SaveAccountCfg(uint32_t accountId)
    {
        PDv2AccountState const state = GetAccountState(accountId);
        std::string packs = state.cfgPacks;
        // cfg_packs is the one free-text column here and it reaches this
        // statement from a player-facing command, so it is escaped rather than
        // trusted - the module's format-style Execute does not quote for us.
        CharacterDatabase.EscapeString(packs);

        // cfg_* columns only, the mirror image of SavePlanToDB: a settings
        // change must never touch progression or the stored layout.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, cfg_rooms, cfg_difficulty, "
            "cfg_caster_pct, cfg_mob_level_min, cfg_packs) VALUES ({}, {}, {}, {}, {}, '{}') "
            "ON DUPLICATE KEY UPDATE cfg_rooms = VALUES(cfg_rooms), "
            "cfg_difficulty = VALUES(cfg_difficulty), cfg_caster_pct = VALUES(cfg_caster_pct), "
            "cfg_mob_level_min = VALUES(cfg_mob_level_min), cfg_packs = VALUES(cfg_packs)",
            accountId, state.cfgRooms, state.cfgDifficulty, state.cfgCasterPct,
            state.cfgBandMin, packs);
    }

    PDv2RunReward PDv2Mgr::GrantRunReward(uint32_t accountId, int roomsUsed)
    {
        PDv2RunReward reward;
        uint32_t dxp = 0;

        {
            std::lock_guard<std::mutex> guard(_lock);
            PDv2AccountState& state = _accounts[accountId];
            uint32_t const oldDlvl = state.dlvl;

            reward.dxpGained = GameRunDxp(roomsUsed, _config.xpPerRoom);
            // Saturating add. 4 billion dxp is not reachable in play, but a
            // wrap would hand the account a dlvl reset it did not earn, and
            // that is not a bug anyone would think to look for.
            state.dxp = (state.dxp > UINT32_MAX - reward.dxpGained)
                            ? UINT32_MAX
                            : state.dxp + reward.dxpGained;
            dxp = state.dxp;
            state.dlvl = static_cast<uint32_t>(
                GameDlvlFromDxp(dxp, _config.xpPerDlvl, _config.dlvlCap));
            state.loaded = true;

            reward.newDlvl = static_cast<int>(state.dlvl);
            reward.leveledUp = state.dlvl > oldDlvl;
        }

        // dlvl and dxp only - the cfg_* knobs and the whole layout half of the
        // row have their own writers, and a reward must not speak for them.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, dlvl, dxp) VALUES ({}, {}, {}) "
            "ON DUPLICATE KEY UPDATE dlvl = VALUES(dlvl), dxp = VALUES(dxp)",
            accountId, reward.newDlvl, dxp);

        return reward;
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

    std::shared_ptr<BlockPlan const> PDv2Mgr::GetPlan(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _plans.find(accountId);
        return it == _plans.end() ? nullptr : it->second;
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
