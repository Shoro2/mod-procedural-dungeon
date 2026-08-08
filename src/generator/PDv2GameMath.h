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
// Two quantities are carried as INTEGERS scaled by 100: difficulty and the loot
// multiplier. 01 §7 says why for difficulty ("cfg_diff_x100 stores difficulty as
// an integer so the step-0.25 grid cannot drift through float storage"); the loot
// multiplier follows it end to end for the same reason, because a multiplier that
// drifts is a multiplier players will eventually find.
//
// Every clamp snaps DOWN rather than to-nearest, so a value a player asked for
// can never quietly become a value above the band they unlocked.
namespace PDungeon
{
    // 01 §8 difficulty grid: d in [0.5, 3.0] in steps of 0.25.
    constexpr int PD_GAME_DIFF_STEP_X100 = 25;

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

    // Distinct trash types per run, drawn from the unlocked packs. 01 §8 wrote
    // "1 + floor(dlvl / 3)" - at dlvl 0 that is ONE type, and the first
    // in-game run was wall-to-wall Ashen Wailers with the caster ratio a
    // no-op (a single type has a single role). Operator verdict 2026-08-07:
    // runs must be mixed from the start. The base moves to 4 - enough for the
    // role seeding to guarantee melee AND casters - and the §8 progression
    // slope (+1 type per 3 dlvl) stays.
    constexpr int GameCreatureTypes(int dlvl)
    {
        return 4 + (dlvl > 0 ? dlvl / 3 : 0);
    }

    // 01 §8 difficulty floor: d = 0.5.
    constexpr int GameDiffMinX100()
    {
        return 50;
    }

    // 01 §8 difficulty ceiling: d = min(3.0, 1 + 0.25 * dlvl).
    constexpr int GameDiffMaxX100(int dlvl)
    {
        int const hi = 100 + PD_GAME_DIFF_STEP_X100 * (dlvl > 0 ? dlvl : 0);
        return hi < 300 ? hi : 300;
    }

    // 01 §8: clamp into the band, then snap DOWN onto the 0.25 grid (137 -> 125).
    // Both band ends are multiples of the step, so the result is always legal.
    constexpr int GameClampDiffX100(int wanted, int dlvl)
    {
        int const lo = GameDiffMinX100();
        int const hi = GameDiffMaxX100(dlvl);
        int v = wanted < lo ? lo : wanted;
        if (v > hi)
        {
            v = hi;
        }
        return v - (v % PD_GAME_DIFF_STEP_X100);
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

    // "lootMult = d * (0.8 + 0.5 * r)", carried as x100 so no float ever
    // touches the number: at the defaults (d = 1.0, r = 0.40) this is exactly
    // 100 * (80 + 20) / 100 = 100. The 01 §8 original was 0.7 + 0.5r anchored
    // on the old 0.60 caster default; when the operator moved the default to
    // 0.40 (2026-08-07) the intercept moved with it - same slope, so a caster
    // percent is worth the same loot either way, and the DEFAULT stays the
    // 1.00 anchor a neutral run advertises. int64 intermediate because the
    // product is the only place that could overflow if the bands are widened.
    constexpr int GameLootMultX100(int diffX100, int casterPct)
    {
        int64_t const factor = 80 + GameClampCasterPct(casterPct) / 2;
        return static_cast<int>(static_cast<int64_t>(diffX100) * factor / 100);
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

    // 01 §8 leaves the dxp -> dlvl curve open; it is linear and config-tunable
    // (V2.XP.PerDlvl, V2.DlvlCap) until play says otherwise. Divided in uint32
    // and capped BEFORE the narrowing cast, because dxp / 1 can exceed INT_MAX.
    constexpr int GameDlvlFromDxp(uint32_t dxp, int xpPerDlvl, int dlvlCap)
    {
        if (xpPerDlvl <= 0 || dlvlCap <= 0)
        {
            return 0;
        }
        uint32_t const raw = dxp / static_cast<uint32_t>(xpPerDlvl);
        uint32_t const cap = static_cast<uint32_t>(dlvlCap);
        return static_cast<int>(raw < cap ? raw : cap);
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
