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

#ifndef MOD_PDUNGEON_RANDOM_H
#define MOD_PDUNGEON_RANDOM_H

#include <cstdint>
#include <random>

namespace PDungeon
{
    // Deterministic RNG wrapper. std::mt19937 output is standardized, but the
    // std distributions are not — the same seed can yield different dungeons
    // on gcc/clang/MSVC. All draws therefore go through hand-rolled,
    // rejection-sampled helpers built on the raw engine output.
    class PDRandom
    {
    public:
        explicit PDRandom(uint32_t seed) : _rng(seed) { }

        uint32_t NextUInt32()
        {
            return _rng();
        }

        // Uniform draw from [lo, hi], both inclusive.
        int UniformInt(int lo, int hi)
        {
            if (lo >= hi)
            {
                return lo;
            }

            uint32_t const range = static_cast<uint32_t>(hi - lo) + 1u;
            uint32_t const limit = UINT32_MAX - (UINT32_MAX % range);
            uint32_t value = _rng();
            while (value >= limit)
            {
                value = _rng();
            }
            return lo + static_cast<int>(value % range);
        }

        bool Chance(int pct)
        {
            if (pct <= 0)
            {
                return false;
            }
            if (pct >= 100)
            {
                return true;
            }
            return UniformInt(1, 100) <= pct;
        }

    private:
        std::mt19937 _rng;
    };
}

#endif
