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

#ifndef MOD_PDUNGEON_V2_TAGGED_AURA_H
#define MOD_PDUNGEON_V2_TAGGED_AURA_H

#include "SpellAuraEffects.h"
#include "Unit.h"

// How this module reads a Forgotten Talents node, and it names NO SPELL ID on
// purpose: FT synthesises the spell records for its extension nodes and its id
// map is sticky rather than frozen, so an id copied over here would be a
// promise the other module never made. The TAG is the promise -
// EffectMiscValue_1 on a passive SPELL_AURA_DUMMY effect - and the rank's
// value rides in the amount (FT writes BasePoints = value - 1 with DieSides 1,
// which is the spell_dbc off-by-one, so GetAmount() hands back the value
// itself). The tags themselves are the PD_TALENT_TAG_* constants in
// PDDefines.h; the band 76001-76004 is registered in share-public
// 06-custom-ids.md.
//
// A HEADER since Round E / WP9, and file-local before it. WP6 wrote these ten
// lines into PDv2InstanceScript.cpp's anonymous namespace and said outright
// that they were deliberately not shared; WP9 gave the loot path a second
// reason to ask the same question, in two other translation units, and two
// copies of "what does the player own" inside ONE module is how the answer
// starts differing by call site. mod-paragon-itemgen still keeps its own copy
// for its own two tags, and that is still right - a header BETWEEN two modules
// that otherwise do not know each other would buy nothing and cost a build
// dependency. This one stays inside these four walls.
namespace PDungeon
{
    // The MAXIMUM over the matching effects, never the sum: the ranks of one
    // node are separate spells and a player who bought rank 2 may still be
    // carrying rank 1, in which case a sum would pay for a rank nobody ever
    // learned. A negative amount folds to 0 - a tag is a count here - and
    // GetAmount() is already 0 for an aura the core disabled.
    //
    // For a node that has exactly one rank (Discerning Eye, WP9) the whole
    // answer is "0 or not 0", which is what an unlock is.
    inline uint32 TaggedAuraAmount(Unit const* unit, int32 miscValue)
    {
        if (!unit)
        {
            return 0;
        }

        int32 best = 0;
        for (AuraEffect const* effect : unit->GetAuraEffectsByType(SPELL_AURA_DUMMY))
        {
            if (effect && effect->GetMiscValue() == miscValue
                && effect->GetAmount() > best)
            {
                best = effect->GetAmount();
            }
        }
        return static_cast<uint32>(best);
    }
}

#endif
