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

    // Planning-field bounds for GameFieldBlocksForRooms (bottom of this file).
    // MIN 3 keeps a corridor possible at all with MIN_ROOM_GAP = 2; HARD_MAX 8
    // is one ADT tile, the same ceiling the caller enforces against the
    // configured value - stated twice on purpose, because a field wider than a
    // tile is a client-side unknown, not a tuning choice.
    constexpr int PD_GAME_FIELD_BLOCKS_MIN = 3;
    constexpr int PD_GAME_FIELD_BLOCKS_HARD_MAX = 8;

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
    // Re-measured 2026-09-10 (Round E / R2: `rooms` counts ORDINARY rooms, so
    // every row seats one cell more than it used to - the entrance), 3000
    // seeds per row, theme 1:
    //
    //   rooms  bossRooms  room cells  gen failures  max manifest B
    //      12          1          14         0             931
    //      13          2          16         0            1093
    //      14          2          17         0            1133
    //      15          2          18         0            1173
    //
    // Theme 2's wider chunk ids are the real ceiling and were measured in the
    // same run: 1229 B at 15 rooms + 2 boss, 0 gen failures - 65 % of the
    // 1900 B budget, so the cap stays 15 rather than dropping to 14.
    //
    // Both constraints are far from binding at 15, and the chain leaves MORE
    // manifest headroom than the MST it replaced (15 rooms measured 1406 B on
    // 2026-08-07 and 1173 B now: one spine plus its pockets needs fewer
    // corridor blocks than a tree with loops and stubs did). A separate sweep
    // past the design cap (1000 seeds per row) puts the real edges at:
    //   * manifest size saturates around 1330 B - 70% of the 1900 B ceiling
    //     and 65% of the 2048 B wire budget - because an 8x8 field holds at
    //     most 64 blocks, so the manifest is bounded by the tile long before
    //     it is bounded by the room count;
    //   * ROOM PACKING is what eventually gives: MIN_ROOM_GAP = 2 Manhattan
    //     (PDBlockPlan.cpp:38) on 64 cells is clean through 24 room cells, and
    //     the search first misses seeds at 26 (88 of 1000, across all 12
    //     attempts; 25 cells is never asked for, because bossRooms steps
    //     2 -> 3 at the dlvl that would have wanted it), then collapses fast -
    //     432 of 1000 at 27 cells, and nothing generates at all at 32.
    //
    // So 15 rooms + 2 boss rooms + the entrance = 18 cells sits 8 cells below
    // the first observed failure - the R2 entrance spent one of that margin,
    // and the packing sweep is unaffected by WHERE the cell count comes from.
    // If either the gap rule or the manifest format changes,
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

    // Round E / D8 room factor, carried x100 like every other multiplier here.
    // Every currency roll and every legacy-rare roll of a run is multiplied by
    // it, so a one-room run pays a tenth of what a ten-room run pays and an
    // eleven-room run pays 1 % more. The operator's reason IS the rule ("damit
    // nicht einfach nur ein-raum-runs gespammt werden"): without it the
    // shortest run is also the most profitable one per minute, and nobody
    // would ever build a long dungeon again.
    //
    // `baseline` and `bonusPctPerRoom` are parameters and deliberately carry
    // no default value: both are operator keys (V2.Loot.Currency.RoomsBaseline
    // and .RoomsBonusPctPerRoom), and a default here would be a second copy of
    // a conf-owned number - the same thing PDv2UILink.h forbids the panel.
    //
    // Below the baseline the factor is the plain share rooms/baseline; at and
    // above it the run is "full" and only the bonus is left. rooms <= 0 is not
    // a run at all and pays nothing, which is also what keeps the division
    // safe: it is only reached with 0 < rooms < baseline, so the divisor is at
    // least 2. No floor on the result - a negative bonusPctPerRoom is not a
    // legal conf value, and GameChanceBp below clamps what it is fed anyway.
    constexpr int GameRoomFactorX100(int rooms, int baseline,
                                     int bonusPctPerRoom)
    {
        if (rooms <= 0)
        {
            return 0;
        }
        if (rooms < baseline)
        {
            return rooms * 100 / baseline;
        }
        return 100 + (rooms - baseline) * bonusPctPerRoom;
    }

    // Round E / D9 item count: "expected = Items x lootMult", split into the
    // part that is certain and the part that is rolled for. floor(items x mult)
    // items always drop; the fraction left over is the PERCENT chance of one
    // more, so one item at lootMult 2.50 is two items plus a coin flip and
    // pays 2.50 on average. Without it the difficulty dial does nothing at all
    // for gear - chest 1 / boss 1 / final 1 would be the same three items at
    // difficulty 1 and at difficulty 100.
    //
    // The roll is a PARAMETER and not a urand call. This header is engine-free
    // by the rule at the top of the file, and a function that rolled its own
    // dice could not be pinned by the harness; the engine passes urand(1, 100).
    // A rollPct of 0 is what a caller that wants no gamble passes and takes
    // the floor alone; anything else outside 1..100 does the same, because the
    // fraction can never exceed 99.
    //
    // Plain int is enough for the product, unlike GameLootMultX100's: `items`
    // is a per-source count in the low single digits and lootMultX100 tops out
    // at 360, so reaching the int range would take some 5.9 million items.
    constexpr int GameScaledCount(int items, int lootMultX100, int rollPct)
    {
        if (items <= 0 || lootMultX100 <= 0)
        {
            return 0;
        }
        int const total = items * lootMultX100;
        int const whole = total / 100;
        int const fracPct = total % 100;
        return (rollPct >= 1 && rollPct <= fracPct) ? whole + 1 : whole;
    }

    // Round E / L3 material ceiling: 1 at dlvl 0 and maxAtCap at the dlvl cap,
    // linear and integer-floored in between (with the shipped 5 per mob and a
    // cap of 30: dlvl 0 -> 1, 8 -> 2, 15 -> 3, 23 -> 4, 30 -> 5). The engine
    // then rolls urand(1, maxCount), so this is the TOP of the band and never
    // the count itself.
    //
    // Clamped to [1, maxAtCap] at both ends. The floor comes free from the
    // "1 +" and a non-negative dlvl; the ceiling is load-bearing, because dlvl
    // is not itself capped anywhere on the way in and an account that somehow
    // sits past the cap must not out-earn the cap.
    constexpr int GameMatsMaxCount(int dlvl, int dlvlCap, int maxAtCap)
    {
        if (maxAtCap <= 1 || dlvlCap <= 0)
        {
            return 1;
        }
        int const d = dlvl > 0 ? dlvl : 0;
        int const n = 1 + (maxAtCap - 1) * d / dlvlCap;
        return n > maxAtCap ? maxAtCap : n;
    }

    // A whole chance, in basis points: 10000 = 100 %. The rolls are
    // urand(1, 10000) - PDv2InstanceScript's bonus-mat roll already works this
    // way - which is the resolution a 1 % chance needs once the room factor's
    // two decimals are multiplied into it.
    constexpr int PD_GAME_CHANCE_BP_MAX = 10000;

    // Round E / L2 drop chance: a percent from the conf, times the D8 room
    // factor, in basis points. Written as the spec states it - percent to
    // basis points (x100), then the factor (x roomFactorX100 / 100) - rather
    // than collapsed into one multiplication, because those are two separate
    // decisions and whoever tunes one of them next should see which is which.
    //
    // Clamped at both ends: at 10000 because a certainty cannot be exceeded
    // (T1 is already 100 % and any room factor above 1.00 would push it past
    // the roll's range), and at 0 because a run with no rooms must pay nothing
    // rather than feed a negative product to the roll.
    //
    // int64 intermediate for the same reason GameLootMultX100 uses one: a conf
    // typo is the only realistic way to get a huge chancePct in here, and it
    // has to clamp rather than wrap.
    constexpr int GameChanceBp(int chancePct, int roomFactorX100)
    {
        if (chancePct <= 0 || roomFactorX100 <= 0)
        {
            return 0;
        }
        int64_t const bp =
            static_cast<int64_t>(chancePct) * 100 * roomFactorX100 / 100;
        return bp > PD_GAME_CHANCE_BP_MAX ? PD_GAME_CHANCE_BP_MAX
                                          : static_cast<int>(bp);
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

    // How large the planning field should be for a given room count.
    //
    // `rooms` is the TOTAL room count the planner will seat - cfg.rooms PLUS
    // cfg.bossRooms PLUS the entrance (Round E / R2) - and not the player's
    // room slider on its own. Every cell
    // the layout claims counts here: a boss room needs the same cell and the
    // same MIN_ROOM_GAP as any other room, so a field sized from the slider
    // alone is a field the plan provably does not fit in. That is not
    // hypothetical - it shipped: at dlvl 30 (four boss rooms) a two-room
    // choice asked for a 3x3 field for six rooms, and 3x3 holds at most five
    // cells pairwise Manhattan >= 2, so generation failed on every seed and
    // the account got no dungeon at all (found by the final review of B0,
    // 2026-09-03; the caller was fixed in the same wave).
    //
    // THE PROBLEM THIS SOLVES (operator, first live run 2026-08-10): the field
    // was a constant 8x8 at every dlvl, so a three-room dungeon put its three
    // rooms anywhere in 64 cells and the corridors between them became the
    // whole experience - "the walks between platforms are far too long, above
    // all at low levels". Density, not room count, is what makes a small
    // dungeon feel small.
    //
    // Target: keep cells-per-room roughly constant at ~4. With the planner's
    // MIN_ROOM_GAP of 2 (Manhattan, so no two rooms are ever adjacent) about
    // half the cells of a field can hold a room, which leaves ~2x headroom for
    // the placement retries - tight enough to shorten the walk, loose enough
    // that generation does not start failing.
    //
    // The CALLER caps this at the configured fieldBlocks and must keep doing
    // so: 8 blocks is exactly one ADT tile and a multi-tile plan is untested on
    // the client. This function only ever proposes something SMALLER.
    constexpr int GameFieldBlocksForRooms(int rooms)
    {
        if (rooms <= 1)
        {
            return PD_GAME_FIELD_BLOCKS_MIN;
        }

        // Integer ceil(sqrt(rooms * 4)) without <cmath>, so this stays
        // constexpr and harness-checkable on every toolchain.
        int const wantCells = rooms * 4;
        int side = PD_GAME_FIELD_BLOCKS_MIN;
        while (side * side < wantCells && side < PD_GAME_FIELD_BLOCKS_HARD_MAX)
        {
            ++side;
        }
        return side;
    }

    // --- Round E / D5: does an item fit a class? ---------------------------
    //
    // The pure half of the loot class filter. PDv2LootMgr::ItemFitsPlayer is
    // the two-line wrapper that reads the ItemTemplate fields and adds the
    // race mask; everything decidable from four integers lives here, so
    // tests/blockplan_harness.cpp pins the whole table without a worldserver.
    // A table this large is only safe to carry BECAUSE it is pinned.
    //
    // Why a table at all, when Player::CanUseItem exists: that function
    // answers the EQUIP question (AllowableClass/Race, level, required skill
    // and spell), and a mage may legally carry a plate helm - it simply may
    // not wear it. D5 asks a different question, "is this a sensible reward
    // for this looter", and the answer to that is a design decision, not a
    // client rule. It is also why the table is deliberately generous: a
    // spell-power ring for a rogue still passes, because stat profile is
    // explicitly NOT filtered (spec D5) and second-guessing itemisation is
    // how a filter starts handing out nothing at all.
    //
    // The four numbers are exactly item_template.AllowableClass, .class,
    // .subclass and the looter's class id. Every id below is a 3.3.5a
    // ItemClass.dbc / ItemSubClass.dbc value, restated as a constant rather
    // than included from SharedDefines.h, because this header is engine-free
    // by the rule at the top of the file.

    // The two item classes this filter has an opinion about.
    constexpr uint8_t PD_ITEM_CLASS_WEAPON = 2;
    constexpr uint8_t PD_ITEM_CLASS_ARMOR = 4;

    // Armour subclasses. 0 is misc (rings, necks, cloaks, trinkets - anyone),
    // 1..4 are the four armour types in ascending order, 5 is the deprecated
    // buckler, 6 is the shield, and 7..10 are the four relic slots (libram,
    // idol, totem, sigil), which AllowableClass gates on its own.
    constexpr uint8_t PD_ITEM_SUBCLASS_ARMOR_CLOTH = 1;
    constexpr uint8_t PD_ITEM_SUBCLASS_ARMOR_PLATE = 4;
    constexpr uint8_t PD_ITEM_SUBCLASS_ARMOR_SHIELD = 6;

    // The playable class ids. 10 exists in the enum and in no character.
    constexpr uint8_t PD_CLASS_WARRIOR = 1;
    constexpr uint8_t PD_CLASS_PALADIN = 2;
    constexpr uint8_t PD_CLASS_HUNTER = 3;
    constexpr uint8_t PD_CLASS_ROGUE = 4;
    constexpr uint8_t PD_CLASS_PRIEST = 5;
    constexpr uint8_t PD_CLASS_DEATH_KNIGHT = 6;
    constexpr uint8_t PD_CLASS_SHAMAN = 7;
    constexpr uint8_t PD_CLASS_MAGE = 8;
    constexpr uint8_t PD_CLASS_WARLOCK = 9;
    constexpr uint8_t PD_CLASS_DRUID = 11;
    constexpr uint8_t PD_CLASS_MAX = 11;

    // The armour types, as the INDEX the bonus rows' armor_pick adds to their
    // base item (cloth 0, leather 1, mail 2, plate 3 - the order the four
    // Mystery Box entries were created in). Subclass = index + 1, which is
    // why the two are one table and not two.
    constexpr uint8_t PD_ARMOUR_CLOTH = 0;
    constexpr uint8_t PD_ARMOUR_LEATHER = 1;
    constexpr uint8_t PD_ARMOUR_MAIL = 2;
    constexpr uint8_t PD_ARMOUR_PLATE = 3;

    // One bit per weapon subclass, so a class's whole weapon table is a
    // single mask and the ten tables below read like the design doc. The
    // gaps are real: 9, 11, 12, 14, 17 and 20 are obsolete, exotic, misc,
    // spear and fishing pole - nothing in the loot pools, nobody's table.
    constexpr uint32_t PD_W_AXE1 = 1u << 0;
    constexpr uint32_t PD_W_AXE2 = 1u << 1;
    constexpr uint32_t PD_W_BOW = 1u << 2;
    constexpr uint32_t PD_W_GUN = 1u << 3;
    constexpr uint32_t PD_W_MACE1 = 1u << 4;
    constexpr uint32_t PD_W_MACE2 = 1u << 5;
    constexpr uint32_t PD_W_POLEARM = 1u << 6;
    constexpr uint32_t PD_W_SWORD1 = 1u << 7;
    constexpr uint32_t PD_W_SWORD2 = 1u << 8;
    constexpr uint32_t PD_W_STAFF = 1u << 10;
    constexpr uint32_t PD_W_FIST = 1u << 13;
    constexpr uint32_t PD_W_DAGGER = 1u << 15;
    constexpr uint32_t PD_W_THROWN = 1u << 16;
    constexpr uint32_t PD_W_CROSSBOW = 1u << 18;
    constexpr uint32_t PD_W_WAND = 1u << 19;

    // The ten class weapon tables, transcribed from spec D5. Ranged slots are
    // in them (a hunter's bow, a warrior's thrown) because those are real
    // rewards; a wand is a priest/mage/warlock item and nobody else's.
    constexpr uint32_t PD_WEAPONS_WARRIOR =
        PD_W_AXE1 | PD_W_AXE2 | PD_W_BOW | PD_W_GUN | PD_W_MACE1 |
        PD_W_MACE2 | PD_W_POLEARM | PD_W_SWORD1 | PD_W_SWORD2 | PD_W_STAFF |
        PD_W_FIST | PD_W_DAGGER | PD_W_THROWN | PD_W_CROSSBOW;
    constexpr uint32_t PD_WEAPONS_PALADIN =
        PD_W_AXE1 | PD_W_AXE2 | PD_W_MACE1 | PD_W_MACE2 | PD_W_POLEARM |
        PD_W_SWORD1 | PD_W_SWORD2;
    constexpr uint32_t PD_WEAPONS_HUNTER =
        PD_W_AXE1 | PD_W_AXE2 | PD_W_BOW | PD_W_GUN | PD_W_POLEARM |
        PD_W_SWORD1 | PD_W_SWORD2 | PD_W_STAFF | PD_W_FIST | PD_W_DAGGER |
        PD_W_THROWN | PD_W_CROSSBOW;
    constexpr uint32_t PD_WEAPONS_ROGUE =
        PD_W_AXE1 | PD_W_BOW | PD_W_GUN | PD_W_MACE1 | PD_W_SWORD1 |
        PD_W_FIST | PD_W_DAGGER | PD_W_THROWN | PD_W_CROSSBOW;
    constexpr uint32_t PD_WEAPONS_PRIEST =
        PD_W_MACE1 | PD_W_STAFF | PD_W_DAGGER | PD_W_WAND;
    constexpr uint32_t PD_WEAPONS_DEATH_KNIGHT =
        PD_W_AXE1 | PD_W_AXE2 | PD_W_MACE1 | PD_W_MACE2 | PD_W_POLEARM |
        PD_W_SWORD1 | PD_W_SWORD2;
    constexpr uint32_t PD_WEAPONS_SHAMAN =
        PD_W_AXE1 | PD_W_AXE2 | PD_W_MACE1 | PD_W_MACE2 | PD_W_STAFF |
        PD_W_FIST | PD_W_DAGGER;
    constexpr uint32_t PD_WEAPONS_MAGE =
        PD_W_SWORD1 | PD_W_STAFF | PD_W_DAGGER | PD_W_WAND;
    constexpr uint32_t PD_WEAPONS_WARLOCK =
        PD_W_SWORD1 | PD_W_STAFF | PD_W_DAGGER | PD_W_WAND;
    constexpr uint32_t PD_WEAPONS_DRUID =
        PD_W_MACE1 | PD_W_MACE2 | PD_W_POLEARM | PD_W_STAFF | PD_W_FIST |
        PD_W_DAGGER;

    // Which armour type a class is rewarded in - the highest it wears at 80,
    // which is also the index armor_pick adds. An id outside 1..11 (and the
    // unused 10) answers cloth rather than refusing: this feeds an item id,
    // and a bonus row must resolve to SOMETHING even for a class that cannot
    // exist. FitsClassRaw below rejects such an id outright, so the two
    // together never hand a nonexistent class a piece of gear.
    constexpr uint8_t GameArmourIndexForClass(uint8_t classId)
    {
        constexpr uint8_t BY_CLASS[PD_CLASS_MAX + 1] =
        {
            PD_ARMOUR_CLOTH,        //  0 no class
            PD_ARMOUR_PLATE,        //  1 warrior
            PD_ARMOUR_PLATE,        //  2 paladin
            PD_ARMOUR_MAIL,         //  3 hunter
            PD_ARMOUR_LEATHER,      //  4 rogue
            PD_ARMOUR_CLOTH,        //  5 priest
            PD_ARMOUR_PLATE,        //  6 death knight
            PD_ARMOUR_MAIL,         //  7 shaman
            PD_ARMOUR_CLOTH,        //  8 mage
            PD_ARMOUR_CLOTH,        //  9 warlock
            PD_ARMOUR_CLOTH,        // 10 unused in 3.3.5a
            PD_ARMOUR_LEATHER       // 11 druid
        };
        return classId <= PD_CLASS_MAX ? BY_CLASS[classId] : PD_ARMOUR_CLOTH;
    }

    // The class's weapon table. 0 for an id that is not a class, which makes
    // every weapon fail for it rather than every weapon pass.
    constexpr uint32_t GameWeaponMaskForClass(uint8_t classId)
    {
        constexpr uint32_t BY_CLASS[PD_CLASS_MAX + 1] =
        {
            0,                          //  0 no class
            PD_WEAPONS_WARRIOR,         //  1
            PD_WEAPONS_PALADIN,         //  2
            PD_WEAPONS_HUNTER,          //  3
            PD_WEAPONS_ROGUE,           //  4
            PD_WEAPONS_PRIEST,          //  5
            PD_WEAPONS_DEATH_KNIGHT,    //  6
            PD_WEAPONS_SHAMAN,          //  7
            PD_WEAPONS_MAGE,            //  8
            PD_WEAPONS_WARLOCK,         //  9
            0,                          // 10 unused in 3.3.5a
            PD_WEAPONS_DRUID            // 11
        };
        return classId <= PD_CLASS_MAX ? BY_CLASS[classId] : 0u;
    }

    // The D5 fit itself. Order matters: AllowableClass first, because it is
    // the item's own statement about who may have it and it overrules
    // nothing below - a plate item flagged mage-only is still plate.
    constexpr bool FitsClassRaw(uint32_t allowableClass, uint8_t itemClass,
                                uint8_t subclass, uint8_t classId)
    {
        if (classId < PD_CLASS_WARRIOR || classId > PD_CLASS_MAX)
        {
            return false;
        }

        // -1 in the column arrives here as 0xFFFFFFFF and passes every class
        // by itself, so the "all classes" convention needs no special case -
        // which is exactly how the core tests it (Player.cpp:10746,
        // `AllowableClass & getClassMask()`, and getClassMask() is
        // 1 << (class - 1)).
        if ((allowableClass & (1u << (classId - 1))) == 0)
        {
            return false;
        }

        if (itemClass == PD_ITEM_CLASS_ARMOR)
        {
            if (subclass >= PD_ITEM_SUBCLASS_ARMOR_CLOTH &&
                subclass <= PD_ITEM_SUBCLASS_ARMOR_PLATE)
            {
                // The ONE type, not "up to": a level-80 druid is rewarded in
                // leather, and cloth for a druid is the disenchant fodder
                // this filter exists to stop.
                return subclass == GameArmourIndexForClass(classId) +
                                       PD_ITEM_SUBCLASS_ARMOR_CLOTH;
            }
            if (subclass == PD_ITEM_SUBCLASS_ARMOR_SHIELD)
            {
                return classId == PD_CLASS_WARRIOR ||
                       classId == PD_CLASS_PALADIN ||
                       classId == PD_CLASS_SHAMAN;
            }
            // Misc (0), the deprecated buckler (5) and the four relic slots
            // (7..10): AllowableClass above is the whole gate, which is what
            // "relics via AllowableClass" means in spec D5.
            return true;
        }

        if (itemClass == PD_ITEM_CLASS_WEAPON)
        {
            // A subclass the mask cannot hold is one 3.3.5a does not have.
            // Refusing it also keeps the shift below defined, which is the
            // real reason the guard is here and not an assumption.
            if (subclass >= 32)
            {
                return false;
            }
            return (GameWeaponMaskForClass(classId) & (1u << subclass)) != 0;
        }

        // Everything else a pool can hold - a container, a consumable, a
        // recipe - is gated by AllowableClass alone.
        return true;
    }
}

#endif
