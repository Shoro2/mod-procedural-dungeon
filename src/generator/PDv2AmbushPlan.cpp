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

#include "PDv2AmbushPlan.h"

#include "PDRandom.h"

namespace PDungeon
{
    // DRAW ORDER on the ambush stream, and the whole of it:
    //   for k = 1..N, in ascending k:
    //     1. Chance(chancePct)                      - always, every segment
    //     2. UniformInt(0, candidates - 1)          - only on a hit with a
    //                                                 non-empty candidate list
    // PDRandom draws nothing at Chance 0 / 100 and nothing at UniformInt with
    // a single candidate, which is what makes the two config extremes free of
    // stream drift rather than merely deterministic.
    std::vector<AmbushSpot> BuildAmbushPlan(BlockPlan const& plan, int chancePct, uint32_t layoutSeed)
    {
        std::vector<AmbushSpot> out;

        int const len = ChainLength(plan);
        if (len < 2)
        {
            // A plan with no chain (or a one-room chain) has no spine run at
            // all, so there is nothing to ambush from. Answered before the RNG
            // is even constructed - not that it would matter, since the stream
            // is this function's alone.
            return out;
        }

        // max(1, bossRooms) without <algorithm>: the planner seats one boss
        // even when the config asks for none, and BossChainIndex applies the
        // same rule internally.
        int const bosses = plan.config.bossRooms > 0 ? plan.config.bossRooms : 1;

        PDRandom rng(layoutSeed ^ PD_AMBUSH_SEED_MIX);
        int prevBoss = 0;
        for (int k = 1; k <= bosses; ++k)
        {
            // The RAW bossRooms into BossChainIndex, exactly like SegmentOf
            // and SpawnBarriers: segment k here and segment k there cannot
            // drift apart under a change to the rounding formula.
            int const bossAt = BossChainIndex(len, plan.config.bossRooms, k);

            // Drawn FIRST and unconditionally, before the geometry is even
            // looked at (see the header): the sequence depends on the boss
            // count alone.
            bool const wants = rng.Chance(chancePct);

            // Every corridor between the previous boss room (or the entrance)
            // and this one, in walking order. SpineRunInto is the same walk
            // the validator proved the spine with and the same one the barrier
            // and the patrol read, so an ambush can only ever sit on a
            // corridor the player must pass.
            std::vector<size_t> candidates;
            for (int i = prevBoss + 1; i <= bossAt; ++i)
            {
                std::vector<size_t> run;
                if (SpineRunInto(plan, i, &run) != 0)
                {
                    candidates.insert(candidates.end(), run.begin(), run.end());
                }
            }

            if (wants && !candidates.empty())
            {
                size_t const pick = candidates[static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(candidates.size()) - 1))];
                AmbushSpot spot;
                spot.blockIndex = pick;
                spot.bx = plan.blocks[pick].bx;
                spot.by = plan.blocks[pick].by;
                spot.segment = k;
                out.push_back(spot);
            }

            prevBoss = bossAt;
        }

        return out;
    }
}
