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

#ifndef MOD_PDUNGEON_V2_AMBUSH_PLAN_H
#define MOD_PDUNGEON_V2_AMBUSH_PLAN_H

#include "PDBlockPlan.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// PDv2 ambush plan (Round B / B5, design 2026-09-03 §1 "B5 - ambush per boss
// segment", decision 1).
//
// One corridor per boss segment may carry an ambush: the player who walks into
// it is stunned for two seconds and a handful of mobs appear around them.
// WHICH corridor that is is a pure function of the layout and one chance, so
// it lives here - engine-free by the same rule as the rest of src/generator/
// (PDBlockPlan.h:38-40) - and tests/blockplan_harness.cpp pins it. The engine
// half (the 250 ms proximity timer, the stun, the spawns) reads this plan and
// decides nothing of its own.
//
// The draw runs on its OWN stream, layoutSeed ^ PD_AMBUSH_SEED_MIX, exactly
// like the decor and critter draws: an operator turning V2.Ambush.Chance must
// never move the layout, the decor, the critters or the spawn draw of a
// dungeon an account already owns.
namespace PDungeon
{
    // Distinct from the layout stream and from every other stream mix in the
    // module, so the ambush roll and the spawn draw can never interleave.
    uint32_t const PD_AMBUSH_SEED_MIX = 0xA3B05EEDu;

    struct AmbushSpot
    {
        size_t blockIndex = 0;      // index into BlockPlan::blocks
        int    bx = 0;              // GLOBAL block coordinate of that corridor
        int    by = 0;
        int    segment = 0;         // boss segment k = 1..N
    };

    // Per boss segment k = 1..N, in order: Chance(chancePct) is ALWAYS drawn;
    // on a hit, one corridor block of the segment's spine runs (the runs into
    // chain rooms b_{k-1}+1 .. b_k, in that order, blocks in walking order) is
    // drawn with UniformInt. Runs hold neither chest stubs nor loop strips, so
    // every candidate is a corridor the player has to walk through.
    //
    // The Chance is drawn for a segment that has no run block at all (two
    // bosses landing on the same chain index) exactly as for one that has: a
    // segment's chance draw is never skipped for want of geometry, or one
    // shifted corridor would move every later segment's roll. The pick after
    // it is the one place the geometry does reach the stream - UniformInt
    // returns lo WITHOUT drawing at lo >= hi (PDRandom.h:41-46), so the pick
    // draws only where a segment offers two or more candidates.
    std::vector<AmbushSpot> BuildAmbushPlan(BlockPlan const& plan, int chancePct, uint32_t layoutSeed);
}

#endif
