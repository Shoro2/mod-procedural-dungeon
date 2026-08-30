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

#ifndef MOD_PDUNGEON_V2_MGR_H
#define MOD_PDUNGEON_V2_MGR_H

#include "generator/PDBlockPlan.h"
#include "generator/PDv2DecorPlan.h"
#include "generator/PDv2GameMath.h"
#include "generator/PDv2WorldMath.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

// PDv2 engine glue, first slice.
//
// v1 rasterises a layout and spawns it as GameObjects. v2 does neither: the
// server decides which kit block sits where, says so in an FLPD2 manifest, and
// the client composes the terrain from that. So this manager's whole job on the
// world side is to hold a plan per account and to answer "where in the world is
// that block".
//
// What this slice deliberately does NOT do yet, so each piece can be built and
// tested before the next is written:
//   * no AIO push - the manifest is written to a file and the DLL's dev-only
//     LOAD verb picks it up. PDClientLink replaces that.
//   * no entry gate - OnPlayerCanEnterMap is not hooked, so a player without
//     the DLL would enter and see void. GM-only commands keep that contained.
//   * no creatures, no walk grid, no kill plane. The server still has no idea
//     where the floor is (`GroundZ -100000`); only the client does.
namespace PDungeon
{
    struct PDv2Config
    {
        bool        enabled = false;
        uint32_t    mapId = 760;
        float       floorZ = 50.0f;      // must match the kit's floor plane
        int         rooms = 5;
        int         bossRooms = 1;
        int         fieldBlocks = 8;
        int         originBX = 256;      // 256/8 = tile 32
        int         originBY = 256;
        int         loopChancePct = 15;
        int         theme = 1;
        std::string manifestPath;        // where `v2 gen` writes the manifest

        // 01 §8 gameplay knobs.
        int         xpPerRoom = 10;
        int         xpPerDlvl = 100;
        int         dlvlCap = 30;
        // Trash in a NORMAL room, and trash beside the boss in a boss room.
        // Two knobs because one cannot say "rooms got fuller, boss rooms did
        // not" - which is exactly what the operator asked for on 2026-08-08.
        int         spawnsPerRoom = 5;
        int         bossRoomAdds = 2;
        int         lootBonusRollPct = 15;
        float       castRangeYd = 25.0f;
        float       aggroRangeYd = 20.0f;
        // No cast-pacing knob lives here. Every cooldown a mob has is a
        // per-spell column in pdungeon_member_spells, including the filler's
        // - one server-wide number could never say "spam the Frostbolt but
        //   not the knockback".

        // The difficulty curve, per point of the 1..100 dial. Percent of the
        // creature's own numbers, added linearly, exactly like
        // mod-dungeon-challenge's HealthMultiplierPerLevel /
        // DamageMultiplierPerLevel - the defaults ARE that module's live values
        // on this box. PDv2 owns its own keys so the two dungeons can diverge.
        int         diffHealthPctPerLevel = 5;
        int         diffDamagePctPerLevel = 2;

        // Share of a run's TRASH that wears the affixes, in percent. Default =
        // mod-dungeon-challenge's live DungeonChallenge.AffixPercentage.
        int         affixPct = 40;

        // Server-side props (torches, braziers). On by default: a dungeon
        // without them is lit by nothing at all, because the kit's terrain
        // carries no light sources. Off is for an operator hunting a GO budget
        // or a display problem - it costs nothing else, since the props are
        // decoration and no mechanic reads them.
        bool        decorEnable = true;
    };

    // The 01 §7 gameplay half of a pdungeon_account row: progression, and the
    // cfg_* knobs the player owns. Cached beside the plans and under the same
    // lock, because both are per account and both are read from map threads.
    //
    // `loaded` distinguishes "the account has a row" from "these are defaults",
    // which is what decides whether GeneratePlan follows the account or the
    // server config.
    struct PDv2AccountState
    {
        uint32_t    dlvl = 0;
        uint32_t    dxp = 0;
        int         cfgRooms = 5;
        // The 1..100 dial (2026-08-08). cfg_diff_x100 is not read or written
        // anywhere any more - see mod_pdungeon_account_difficulty.sql for why
        // the column survives its own retirement.
        int         cfgDifficulty = PD_GAME_DIFF_DEFAULT;
        int         cfgCasterPct = PD_GAME_CASTER_PCT_DEFAULT;
        // 76 rather than the column's default of 1: 76..80 is the only band v1's
        // imported pack stock actually covers, so a fresh account that never
        // touched the setting still gets real creatures instead of an empty
        // pool. A stored row is always taken at face value.
        int         cfgBandMin = PD_GAME_BAND_MAX;
        std::string cfgPacks;
        bool        loaded = false;
    };

    struct PDv2RunReward
    {
        uint32_t dxpGained = 0;
        int      newDlvl = 0;
        bool     leveledUp = false;
    };

    // The geometry constants (PD_TILE_SIZE_YD and friends) moved to
    // generator/PDv2WorldMath.h so the world math that uses them is
    // harness-checkable; the include above keeps them visible here.

    // Stored with every persisted layout; bump on any change that would make
    // an old seed regenerate a DIFFERENT dungeon (generator logic, kit block
    // ids, field semantics). A mismatch at load means "reroll needed", never
    // "regenerate wrong".
    //
    // v2 (2026-08-30, Phase 2): dead-end stubs and visual alternates draw
    // from the stream, so a v1 seed no longer reproduces its stored layout.
    // Every stored dungeon rerolls once on first entry; dlvl/dxp are
    // untouched by design (layout columns update via ON DUPLICATE KEY only).
    constexpr uint32_t PD_LAYOUT_VERSION = 2;

    class PDv2Mgr
    {
    public:
        static PDv2Mgr* instance();

        void LoadConfig();
        PDv2Config const& GetConfig() const { return _config; }
        bool IsEnabled() const { return _config.enabled; }

        // Builds a plan for `accountId`, replaces any previous one and saves
        // its generation inputs to the characters DB. Returns false when the
        // generator could not produce a valid layout.
        // themeOverride 0 follows the server config; a nonzero value is the
        // GM test path (`.pdungeon v2 gen [seed] [theme]`) and is persisted
        // like any other gen input - the theme is frozen into the layout.
        bool GeneratePlan(uint32_t accountId, uint32_t seed, BlockPlan& out,
                          int themeOverride = 0);

        // The stored plan, or an empty pointer when the account has none.
        //
        // shared_ptr rather than a raw pointer into the map, and the pointee is
        // immutable: StorePlan REPLACES the shared object instead of mutating
        // it, so a reader on a map thread keeps a complete plan even while a
        // re-roll swaps the account's current one. The raw-pointer form this
        // replaced was only safe because MapUpdate.Threads = 1 - a constraint
        // nobody should have to remember when F7 raises it (12-server-todo §5,
        // closed 2026-08-07).
        std::shared_ptr<BlockPlan const> GetPlan(uint32_t accountId) const;

        // Restores the account's persisted layout by REGENERATING it from the
        // stored seed + generation inputs (a plan is deterministic, so no
        // layout blob exists to load). Called at login; a missing row, seed 0
        // or a foreign layout_version simply means "no dungeon yet". No-op
        // when a plan is already cached for this account.
        void LoadPlanFromDB(uint32_t accountId);

        // Restores the account's dlvl/dxp and cfg_* knobs. Called at login
        // beside LoadPlanFromDB; a missing row simply means "the defaults".
        // No-op when the account is already cached.
        void LoadAccountState(uint32_t accountId);

        // A copy, because callers run on map threads and the cache is shared.
        PDv2AccountState GetAccountState(uint32_t accountId) const;

        // Replaces ONLY the cfg_* knobs in the cache, each clamped through the
        // 01 §8 math on the way in - so an out-of-band value can never reach a
        // dungeon regardless of which command or DB edit produced it. Does not
        // persist; call SaveAccountCfg for that.
        void SetAccountCfg(uint32_t accountId, PDv2AccountState const& cfg);

        // Writes ONLY the cfg_* columns, mirroring the rule SavePlanToDB
        // documents for the layout columns: settings must never clobber
        // progression, and a reroll must never clobber settings.
        void SaveAccountCfg(uint32_t accountId);

        // Pays out a finished run: 01 §8 dxp (difficulty-independent by
        // design), recomputes dlvl, and persists dlvl/dxp only.
        PDv2RunReward GrantRunReward(uint32_t accountId, int roomsUsed);

        // Writes the manifest for `plan` to the configured path. Until
        // PDClientLink exists this file IS the transport: the operator feeds it
        // to the DLL by hand.
        bool WriteManifest(BlockPlan const& plan, uint32_t seq, std::string& pathOut,
                           std::string& error) const;

        // World position of a point inside a block. `u` runs north to south and
        // `v` west to east, both in yards from the block's north-west corner -
        // the same FLPD-BLOCK-1 frame the kit's anchors use.
        void BlockToWorld(int bx, int by, double u, double v,
                          float& x, float& y, float& z) const;

        // Convenience: the middle of a plan's entrance block.
        bool EntranceWorldPos(BlockPlan const& plan, float& x, float& y, float& z) const;

        // Loads the kit's walk masks from `pdungeon_chunk_meta` (the SQL that
        // ships with the module, generated by 48_gen_t1_blockkit.py). Called
        // once at world startup; deliberately NOT on `.reload config`, because
        // a kit change also means a new client patch and DLL kit, and that is
        // a restart in any case. After startup the table is read-only, so map
        // threads may query it without a lock.
        void LoadChunkMeta();

        // The 8x8 walk mask for a kit chunk, or nullptr for an unknown id -
        // the shape BuildWalkGrid's WalkMaskProvider wants.
        uint8_t const* WalkMaskFor(int chunkId) const;

        size_t WalkMaskCount() const { return _walkMasks.size(); }

        // The kit's anchor points for a chunk (entry, boss, chest, spawns), in
        // the block-local FLPD-BLOCK-1 frame, or nullptr for a chunk with none
        // - which every corridor is. Loaded beside the walk masks out of the
        // same `pdungeon_chunk_meta` row, so the two can never describe
        // different kits. The decor planner keeps its props clear of these.
        std::vector<DecorAnchor> const* AnchorsFor(int chunkId) const;

        // The chunk's structural GameObject props (fountain, cave-in, ...),
        // or nullptr - most corridors have none. Same lifetime and source as
        // the anchors: one chunk-meta row, decoded once at load.
        std::vector<KitProp> const* PropsFor(int chunkId) const;

        // Loads `pdungeon_decor_rules`, ascending id. Called once at world
        // startup beside LoadChunkMeta and read-only afterwards, for the same
        // reason: map threads read it without a lock.
        void LoadDecorRules();

        // The decor rules, in the order they were loaded (ascending id).
        // BuildDecorPlan sorts defensively all the same - see its header.
        std::vector<DecorRule> const& DecorRules() const { return _decorRules; }

    private:
        void StorePlan(uint32_t accountId, BlockPlan const& plan);
        void SavePlanToDB(uint32_t accountId, BlockPlan const& plan);

        PDv2Config _config;
        mutable std::mutex _lock;
        std::unordered_map<uint32_t, std::shared_ptr<BlockPlan const>> _plans;
        std::unordered_map<uint32_t, PDv2AccountState> _accounts;
        std::unordered_map<int, std::array<uint8_t, PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK>> _walkMasks;
        std::unordered_map<int, std::vector<DecorAnchor>> _chunkAnchors;
        std::unordered_map<int, std::vector<KitProp>> _chunkProps;
        std::vector<DecorRule> _decorRules;
    };
}

#define sPDv2Mgr PDungeon::PDv2Mgr::instance()

#endif
