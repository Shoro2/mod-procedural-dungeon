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

#ifndef MOD_PDUNGEON_DUNGEON_GENERATOR_H
#define MOD_PDUNGEON_DUNGEON_GENERATOR_H

#include "PDLayout.h"

namespace PDungeon
{
    // Seed-deterministic layout generator, modeled after the classic
    // scatter -> separate -> connect -> MST(+loops) -> semantics -> rasterize
    // pipeline. Pure logic, no engine dependencies.
    class PDDungeonGenerator
    {
    public:
        // Generates a layout for config.seed. Individual attempts can fail
        // (disconnected graph, over-packed grid, failed validation); up to
        // config.maxGenerateTries consecutive seeds (seed, seed+1, ...) are
        // tried. Returns false only if all attempts failed; out.effectiveSeed
        // holds the seed that succeeded.
        static bool Generate(GenConfig const& config, PDLayout& out);

    private:
        static bool TryGenerate(GenConfig const& config, uint32_t seed, PDLayout& out);
    };
}

#endif
