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

#ifndef MOD_PDUNGEON_V2_GAME_MATH_H
#define MOD_PDUNGEON_V2_GAME_MATH_H

#include <cstdint>

// PDv2 gameplay math - the 01 §8 formulas, and nothing else.
//
// Engine-free by the same rule as the rest of src/generator/ (PDBlockPlan.h:38-40):
// no AzerothCore include may appear here, so tests/blockplan_harness.cpp compiles
// it standalone. That is the point - every number a player can feel is decided
// here and pinned by the harness, not scattered across the instance script.
//
// The loot multiplier is carried as an INTEGER scaled by 100, because a
// multiplier that drifts through float storage is a multiplier players will
// eventually find. Difficulty used to be carried the same way (01 §7's
// cfg_diff_x100, a 0.5..3.0 band on a 0.25 grid); since the 2026-08-08 operator
// directive it is a plain integer 1..100 and needs no fixed point at all.
//
// Every clamp snaps DOWN rather than to-nearest, so a value a player asked for
// can never quietly become a value above the band they unlocked.
namespace PDungeon
{
    // Difficulty, the dungeon-challenge model adopted whole (operator directive
    // 2026-08-08, after the first live test): an integer 1..100, FREELY
    // choosable from the very first run. dlvl no longer gates it - it still
    // gates rooms and which packs are unlocked, but the dial itself is open,
    // exactly as mod-dungeon-challenge's keystone difficulty is.
    //
    // The floor is 1 rather than 0 because difficulty 0 would mean "no scaling
    // at all" and the dial would have a dead first position; at 1 the mobs are
    // 5 % tougher and 2 % harder-hitting (V2.Diff.*PctPerLevel), which is the
    // gentle floor the directive asked for.
    constexpr int PD_GAME_DIFF_MIN = 1;
    constexpr int PD_GAME_DIFF_MAX = 100;
    constexpr int PD_GAME_DIFF_DEFAULT = 1;

    // The dial's step, as a named constant rather than a literal 1 on the wire:
    // the panel is told every bound it draws (PDv2UILink.h's rule), and a step
    // it had to assume would be the first Lua copy of a server-owned number.
    constexpr int PD_GAME_DIFF_STEP = 1;

    // 01 §8 mob level band, "in steps of 5"; band = [min, min + 4], so 76 is
    // the highest legal start and the top band is 76..80.
    constexpr int PD_GAME_BAND_STEP = 5;
    constexpr int PD_GAME_BAND_MIN = 1;
    constexpr int PD_GAME_BAND_MAX = 76;

    // 01 §8 caster ratio r in [0.20, 0.80]. Default 40 - the operator flipped
    // the original 60/40 caster-heavy default to 40 % casters / 60 % melee at
    // the first in-game test (2026-08-07).
    constexpr int PD_GAME_CASTER_PCT_MIN = 20;
    constexpr int PD_GAME_CASTER_PCT_MAX = 80;
    constexpr int PD_GAME_CASTER_PCT_DEFAULT = 40;

    // Room floor. 01 §8's "3 + dlvl" is the CAP, not a floor - the floor was 3
    // until the first in-game test, when the operator asked for 1 (2026-08-07):
    // a 1-room run is entrance + boss room, a legal boss-rush micro-dungeon
    // (the planner scatters max(2, rooms + bossRooms) cells, so the layout
    // never degenerates below two rooms).
    constexpr int PD_GAME_ROOMS_MIN = 1;

    // The "3" in 01 §8's cap formula "3 + dlvl" - deliberately its own
    // constant: when the FLOOR moved from 3 to 1 the cap borrowed the floor
    // constant and silently became "1 + dlvl" until the harness's
    // independently-coded expectation caught it. Formula base and slider floor
    // are different quantities that happened to share a value once.
    constexpr int PD_GAME_ROOMS_CAP_BASE = 3;

    // 01 §8 design ceiling on rooms. PD_GAME_ROOMS_CAP_MEASURED below is what
    // the kit actually supports and wins where the two disagree.
    constexpr int PD_GAME_ROOMS_CAP_DESIGN = 15;

    // The manifest is ONE addon packet by design (PDClientLink.h:42-44): no
    // fragmentation protocol exists, and building one would touch the addon and
    // the DLL. 2048 is the hard wire budget; 1900 is the ceiling the room cap
    // was measured against, so a longer header or one more per-block field
    // later does not silently invalidate the measurement.
    constexpr int PD_GAME_MANIFEST_BUDGET_B = 1900;

    // MEASURED ceiling on rooms - not a design number, even though it happens
    // to land on the same value 01 §8 designed for.
    //
    // Two physical constraints bound the room count, and `pdblock --roomcap`
    // measures both against the shipped generator on a field of 8x8 blocks
    // (ONE ADT tile - multi-tile plans are untested client-side, so the field
    // is never widened to buy rooms). bossRooms per row is GameBossRooms at the
    // dlvl where that room count unlocks, i.e. what a player there would run.
    // Measured 2026-08-07, 3000 seeds per row:
    //
    //   rooms  bossRooms  room cells  gen failures  max manifest B
    //      12          1          13         0            1309
    //      13          2          15         0            1371
    //      14          2          16         0            1391
    //      15          2          17         0            1406
    //
    // Both constraints are far from binding at 15. A separate sweep past the
    // design cap (1000 seeds per row) puts the real edges at:
    //   * manifest size saturates around 1425 B - 75% of the 1900 B ceiling
    //     and 70% of the 2048 B wire budget - because block count grows much
    //     more slowly than room count once corridors start being shared;
    //   * ROOM PACKING is what eventually gives: MIN_ROOM_GAP = 2 Manhattan
    //     (PDBlockPlan.cpp:34) on 64 cells is clean through 25 room cells, and
    //     the rejection sampler first misses a seed at 26 (7 of 1000, even
    //     across all 12 retries), then collapses fast - 93 of 1000 at 27.
    //
    // So 15 rooms + 2 boss rooms = 17 cells sits 8 cells below the first
    // observed failure. If either the gap rule or the manifest format changes,
    // re-run `pdblock --roomcap 3000`; the batch re-measures this constant on
    // every run so it cannot rot silently.
    constexpr int PD_GAME_ROOMS_CAP_MEASURED = 15;

    // 01 §8 "Rooms 3 + dlvl, hard cap 15", further bounded by what the 8x8
    // field can actually be asked for (see the measurement above).
    constexpr int GameRoomsCap(int dlvl)
    {
        int const safeDlvl = dlvl > 0 ? dlvl : 0;
        int cap = PD_GAME_ROOMS_CAP_BASE + safeDlvl;
        if (cap > PD_GAME_ROOMS_CAP_DESIGN)
        {
            cap = PD_GAME_ROOMS_CAP_DESIGN;
        }
        if (cap > PD_GAME_ROOMS_CAP_MEASURED)
        {
            cap = PD_GAME_ROOMS_CAP_MEASURED;
        }
        return cap;
    }

    // 01 §8 "Boss rooms 1 + floor(dlvl / 10)".
    constexpr int GameBossRooms(int dlvl)
    {
        return 1 + (dlvl > 0 ? dlvl / 10 : 0);
    }

    // There is deliberately no GameCreatureTypes() any more. 01 §8 capped a run
    // at "1 + floor(dlvl / 3)" DISTINCT trash entries and PDv2PackMgr drew that
    // subset once per run; the second live test read exactly what that does -
    // "only a few of the available mobs get picked and then only those are
    // used". Operator verdict 2026-08-08: every spawn slot rolls independently
    // from the whole unlocked pool, so there is no cap left to compute.

    // Difficulty clamp. No grid to snap onto and no dlvl argument: the dial is
    // every integer in [1, 100] and a fresh account may pick any of them.
    constexpr int GameClampDiff(int wanted)
    {
        if (wanted < PD_GAME_DIFF_MIN)
        {
            return PD_GAME_DIFF_MIN;
        }
        return wanted > PD_GAME_DIFF_MAX ? PD_GAME_DIFF_MAX : wanted;
    }

    // 01 §8 caster ratio, as a percent.
    constexpr int GameClampCasterPct(int pct)
    {
        if (pct < PD_GAME_CASTER_PCT_MIN)
        {
            return PD_GAME_CASTER_PCT_MIN;
        }
        return pct > PD_GAME_CASTER_PCT_MAX ? PD_GAME_CASTER_PCT_MAX : pct;
    }

    // 01 §8 room count, clamped into [3, cap].
    constexpr int GameClampRooms(int wanted, int dlvl)
    {
        int const hi = GameRoomsCap(dlvl);
        if (wanted < PD_GAME_ROOMS_MIN)
        {
            return PD_GAME_ROOMS_MIN;
        }
        return wanted > hi ? hi : wanted;
    }

    // "lootMult = base(d) * (0.8 + 0.5 * r)", carried as x100 so no float ever
    // touches the number.
    //
    // base(d) is a LINE across the whole dial: 1.00 at difficulty 1, 3.00 at
    // difficulty 100 (operator decision 2026-08-08). The old formula multiplied
    // by the difficulty itself, which only worked while difficulty WAS a
    // multiplier around 1.0; on a 1..100 dial that would pay a hundredfold. The
    // 3x ceiling is kept from the old band deliberately - the dial got longer,
    // the reward for the top of it did not.
    //
    // The caster factor is 01 §8's, unchanged: 0.7 + 0.5r originally, moved to
    // 0.8 + 0.5r when the operator flipped the caster default to 0.40
    // (2026-08-07) so the DEFAULT run still advertises exactly 1.00.
    //
    // Anchors, exact by construction (200 / 99 never rounds at the ends):
    //   (1, 40)   -> 100 * 100 / 100 = 100      the neutral run
    //   (100, 40) -> 300 * 100 / 100 = 300      the top of the dial
    //   (100, 80) -> 300 * 120 / 100 = 360      top dial, all casters
    //
    // int64 intermediate because the product is the only place that could
    // overflow if the dial or the caster band is ever widened.
    constexpr int GameLootMultX100(int diff, int casterPct)
    {
        int const d = GameClampDiff(diff);
        int64_t const base = 100 + static_cast<int64_t>(d - PD_GAME_DIFF_MIN) * 200 /
                                       (PD_GAME_DIFF_MAX - PD_GAME_DIFF_MIN);
        int64_t const factor = 80 + GameClampCasterPct(casterPct) / 2;
        return static_cast<int>(base * factor / 100);
    }

    // 01 §8 mob level band in steps of 5: clamp to [1, 76], snap DOWN onto the
    // 1, 6, 11, ... 76 grid. The band the caller gets is [min, min + 4].
    constexpr int GameClampBandMin(int lvl)
    {
        int v = lvl < PD_GAME_BAND_MIN ? PD_GAME_BAND_MIN : lvl;
        if (v > PD_GAME_BAND_MAX)
        {
            v = PD_GAME_BAND_MAX;
        }
        return v - ((v - PD_GAME_BAND_MIN) % PD_GAME_BAND_STEP);
    }

    // 01 §8 left the dxp -> dlvl curve open. It was linear until 2026-08-08,
    // when the operator asked for each dungeon level to cost 10 % more than the
    // one before it: V2.XP.PerDlvl is now the price of the FIRST level and
    // every next one grows from it.
    constexpr int PD_GAME_XP_GROWTH_PCT = 10;

    // One growth step, INTEGER FLOOR - the chain is the definition, not a
    // rounded pow(): 100, 110, 121, 133, 146, 160, 176, 193, 212, 233. A float
    // formula would agree for a while and then disagree by one at some level
    // nobody is watching, and a level that costs a different amount to reach
    // than to display is the kind of bug players report as "my bar is stuck".
    //
    // Saturating: past the guard one more multiplication would wrap uint32 and
    // hand out a level for free. Unreachable with any sane V2.XP.PerDlvl (from
    // 100 it takes ~175 levels), which is exactly why it is written down.
    constexpr uint32_t PD_GAME_XP_COST_MAX = 0xFFFFFFFFu / (100 + PD_GAME_XP_GROWTH_PCT);

    constexpr uint32_t GameGrowDlvlCost(uint32_t cost)
    {
        return cost > PD_GAME_XP_COST_MAX
                   ? cost
                   : cost * (100 + PD_GAME_XP_GROWTH_PCT) / 100;
    }

    // What the step dlvl -> dlvl+1 costs, in dungeon XP.
    constexpr uint32_t GameDlvlCost(int dlvl, int xpPerDlvl)
    {
        if (xpPerDlvl <= 0)
        {
            return 0;
        }
        uint32_t cost = static_cast<uint32_t>(xpPerDlvl);
        for (int n = 0; n < dlvl; ++n)
        {
            cost = GameGrowDlvlCost(cost);
        }
        return cost;
    }

    // The walk both public accessors below are made of, so the level a player
    // is on and the XP they have INTO it can never be computed two different
    // ways. dxp stays LIFETIME in the database; this is what turns it into the
    // per-level pair the panel shows.
    //
    // Bounded by dlvlCap iterations by construction, which is what replaced the
    // old "cap before the narrowing cast" trap: there is no division left to
    // overflow, and a huge dxp simply stops at the cap.
    struct DlvlWalk
    {
        int      dlvl = 0;
        uint32_t into = 0;      // dxp INTO the current level
        uint32_t cost = 0;      // what the current level costs
    };

    constexpr DlvlWalk GameWalkDlvl(uint32_t dxp, int xpPerDlvl, int dlvlCap)
    {
        DlvlWalk walk;
        if (xpPerDlvl <= 0 || dlvlCap <= 0)
        {
            return walk;
        }

        walk.into = dxp;
        walk.cost = static_cast<uint32_t>(xpPerDlvl);
        while (walk.dlvl < dlvlCap && walk.cost > 0 && walk.into >= walk.cost)
        {
            walk.into -= walk.cost;
            ++walk.dlvl;
            walk.cost = GameGrowDlvlCost(walk.cost);
        }
        return walk;
    }

    constexpr int GameDlvlFromDxp(uint32_t dxp, int xpPerDlvl, int dlvlCap)
    {
        return GameWalkDlvl(dxp, xpPerDlvl, dlvlCap).dlvl;
    }

    // The remainder INTO the current level - what the XP bar fills with. At the
    // cap the walk stops and this keeps counting the overflow, because dxp is a
    // lifetime total and lying about it here would make the two disagree; the
    // display decides what to do with a level that has no next one.
    constexpr uint32_t GameDxpIntoLevel(uint32_t dxp, int xpPerDlvl, int dlvlCap)
    {
        return GameWalkDlvl(dxp, xpPerDlvl, dlvlCap).into;
    }

    // 01 §8 "Dungeon XP = XP.PerRoom * rooms", difficulty-INDEPENDENT by
    // design: the moment difficulty becomes an XP lever, the only rational
    // setting is the highest one and the difficulty slider is dead content.
    // That is why no difficulty argument exists here, and must never be added.
    constexpr uint32_t GameRunDxp(int roomsUsed, int xpPerRoom)
    {
        if (roomsUsed <= 0 || xpPerRoom <= 0)
        {
            return 0u;
        }
        return static_cast<uint32_t>(roomsUsed) * static_cast<uint32_t>(xpPerRoom);
    }
}

#endif
