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

#ifndef MOD_PDUNGEON_V2_PACK_DRAW_H
#define MOD_PDUNGEON_V2_PACK_DRAW_H

#include <cstdint>
#include <vector>

// PDv2 spawn draw: which pack member fills which room slot.
//
// PDv2PackMgr.cpp includes DatabaseEnv.h and never will be linkable into
// tests/blockplan_harness.cpp, but the draw itself never touched the
// database - only the pack TABLES it reads did. Pulling the pure arithmetic
// out to here, engine-free by the same rule as the rest of src/generator/
// (PDBlockPlan.h:38-40), is what lets the harness pin it.
//
// SpawnPick, SpawnSelectInputs and PackMember (plus the small PackRole and
// RoomRequest types they are built from) moved out of PDv2PackMgr.h to make
// this possible; PDv2PackMgr.h includes this header to get them back.
namespace PDungeon
{
    enum PackRole : uint8_t
    {
        PACK_ROLE_MELEE = 0,
        PACK_ROLE_CASTER = 1,
        PACK_ROLE_BOSS = 2
    };

    struct PackMember
    {
        // The pack this member was loaded from. Not read by this task's own
        // draw (which only ever sees the merged role pools below), but
        // needed by PackPools::meleeOf/casterOf (Task 13's per-room pack
        // draw) and by a hand-written fixture, which has no other way to
        // say which pack an entry belongs to once the pools are flattened.
        int      packId = 0;
        uint32_t entry = 0;
        uint8_t  role = PACK_ROLE_MELEE;
        uint32_t casterSpellId = 0;     // 0 for anything that is not a caster
        uint16_t weight = 100;
    };

    struct SpawnPick
    {
        uint32_t entry = 0;
        uint8_t  role = PACK_ROLE_MELEE;
        uint32_t casterSpellId = 0;
        // This spawn wears the run's affixes. Rolled inside the draw so it
        // rides the seeded stream with every other spawn decision - the same
        // seed brings back the same affixed mobs after a restart.
        bool     affixed = false;
    };

    // One room the caller wants filled. `roomIndex` is opaque here and simply
    // handed back, so the instance script can key it however it likes.
    struct RoomRequest
    {
        int  roomIndex = 0;
        bool isBoss = false;
    };

    struct SpawnSelectInputs
    {
        std::vector<RoomRequest> rooms;
        int spawnsPerRoom = 5;          // trash in a NORMAL room
        int bossRoomAdds = 2;           // trash BESIDE the boss, boss rooms
        int casterPct = 60;             // 01 §8 caster ratio, already clamped
        int bandMin = 76;               // band is [bandMin, bandMin + 4]
        int unlockedDlvl = 0;
        int affixPct = 40;              // share of TRASH that wears the affixes
    };

    // The pools the draw reads, as a view over whatever holds them. Lifting
    // this out of PDv2PackMgr is what lets the harness link the draw: the
    // manager's own file includes DatabaseEnv.h and never will.
    struct PackPools
    {
        // (packId, creature entry) in the loader's own order.
        std::vector<PackMember> melee;
        std::vector<PackMember> caster;
        std::vector<PackMember> boss;

        // melee and caster, INTERLEAVED in the same loader order instead of
        // split by role - i.e. exactly the order pdungeon_pack_members
        // loaded in ("ORDER BY p.id, m.entry"; see PDv2PackMgr::LoadFromDB),
        // boss members excluded. This exists for one reader only: the boss-
        // standin tie-break inside PDv2SelectSpawns below. That scan's
        // comparison is a strict `>`, so on a weight tie the FIRST member
        // seen wins - and every shipped pack member weighs 100
        // (mod_pdungeon_packs.sql), which makes every stand-in pick a tie.
        // Scanning melee fully and then caster (as splitting trash by role
        // and concatenating the two halves would) hands the tie to melee
        // even when a caster loaded earlier; scanning THIS vector instead
        // reproduces the pre-refactor single-pool scan exactly (git
        // 862ace7, PDv2PackMgr.cpp:512), because it IS that pool, carried
        // across rather than re-derived. Do not rebuild it by concatenating
        // melee and caster above - their order against EACH OTHER is lost
        // the instant they are split by role, and re-merging by any rule
        // other than "the order the loader actually produced" is just a
        // different, still-wrong tie-break.
        std::vector<PackMember> trash;

        // Packs holding at least one NON-BOSS member, ascending. The per-room
        // pack draw is uniform over this list; a pack that can fill nothing
        // must not be drawable, or a fifth of all rooms would come out empty.
        std::vector<int> trashPackIds;

        // The members of ONE pack, or an empty vector if it has none of that
        // role. Empty is the signal to fall back to the merged pool.
        std::vector<PackMember> const& meleeOf(int packId) const;
        std::vector<PackMember> const& casterOf(int packId) const;

        // What meleeOf/casterOf actually search - one entry per pack that
        // contributed at least one member of that role to melee/caster
        // above, built by the SAME loader pass so the grouped and flat views
        // can never disagree. A std::vector of (id, members) rather than an
        // unordered_map on purpose: nothing under src/generator/ may let a
        // hash container's iteration order anywhere near a draw, and the
        // pack count a run ever sees is in the single digits, so the linear
        // scan behind meleeOf/casterOf costs nothing that matters.
        struct PackGroup
        {
            int packId = 0;
            std::vector<PackMember> members;
        };
        std::vector<PackGroup> meleeByPack;
        std::vector<PackGroup> casterByPack;
    };

    // The whole spawn draw. Deterministic for a given (seed, inputs, pools):
    // same three on any compiler yields the same picks, which is why it lives
    // here and not beside the database.
    //
    // Returns false when `pools` holds no member of any role - the caller's
    // cue to fall back to its placeholder creature, mirroring what an empty
    // band-filtered pool did before this draw had its own pools parameter.
    //
    // `outBossStandIn`, when non-null, is written every call: to the member
    // the boss-standin tie-break picked if the fallback fired (pools.boss
    // empty but melee or caster was not), to nullptr otherwise. This file is
    // engine-free and cannot log (see the file comment above), so
    // PDv2PackMgr::SelectSpawns reads this back to LOG_WARN which entry it
    // is about to use for every boss room - and checking it for non-null IS
    // the caller's gate, rather than a second copy of the condition that
    // decides whether the fallback fired. There used to be a second,
    // hand-synced copy of the tie-break scan itself just to feed that log
    // message; the two copies had already drifted from each other by the
    // time that was noticed, which is why there is exactly one scan now.
    bool PDv2SelectSpawns(uint32_t seed, SpawnSelectInputs const& in,
                          PackPools const& pools, std::vector<SpawnPick>& out,
                          PackMember const** outBossStandIn = nullptr);
}

#endif
