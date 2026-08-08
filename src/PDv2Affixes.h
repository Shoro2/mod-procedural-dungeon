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

#ifndef MOD_PDUNGEON_V2_AFFIXES_H
#define MOD_PDUNGEON_V2_AFFIXES_H

#include "Define.h"

// PDv2's half of the affixes: the BEHAVIOUR that mod-dungeon-challenge keeps in
// its own script hooks, re-gated on PDv2's own state.
//
// The spells stay that module's (mod_pdungeon_affixes.sql says so at length):
// nothing here creates, edits or reads one of its rows, and nothing here
// includes one of its headers. What is shared is the DEFINITION of an affix -
// the ids below and the numbers each mechanic uses - so that a Big Boy is the
// same creature in both dungeons. Every one of those numbers carries the
// file:line it was read from on 2026-08-08.
//
// MEMBERSHIP IS THE TAG, NEVER THE AURA. Each hook asks the spawn tag's
// `affixMask` whether this creature carries the affix, and never
// `HasAura(spellId)`. The affix auras are player-visible and can be dispelled,
// purged or stripped by an evade, and a mechanic that quietly disarms itself
// when its marker aura goes missing is the in-house "HasAura marker gating"
// trap. The aura is the look; the mask is the fact.
namespace PDungeon
{
    // The affix ids as they appear in `pdungeon_affixes`.`id`. They ARE
    // mod-dungeon-challenge's `DungeonChallengeAffix` values
    // (DungeonChallenge.h:34-49) - mod_pdungeon_affixes.sql was extracted from
    // that enum - so the two must never drift.
    //
    // Only the six with a behaviour half are named. Affixes 2 (Speedy), 5 (CC
    // Immunity) and 6 (Heavy Hits), and the damage half of 9, are pure DBC
    // auras: they arrive whole with the spell and nothing in this module may
    // implement them a second time.
    enum PDv2Affix : uint8
    {
        PD_AFFIX_CALL_FOR_HELP = 1,
        PD_AFFIX_BIG_BOY       = 3,
        PD_AFFIX_IMMOLATION    = 4,
        PD_AFFIX_LIL_BRO       = 7,
        PD_AFFIX_DAMAGE_REDUCE = 8,
        PD_AFFIX_BIGGER_BOY    = 9,
        PD_AFFIX_HELL_TOUCHED  = 10
    };

    // Bit i-1 carries affix id i, so the table's whole 1..10 range fits one
    // uint16 with room for an operator's own rows up to 16. An id outside that
    // window maps to no bit at all, which reads as "this creature does not have
    // it" everywhere rather than shifting past the end of the word.
    constexpr uint16 AffixBit(uint8 id)
    {
        return (id >= 1 && id <= 16) ? static_cast<uint16>(1u << (id - 1)) : static_cast<uint16>(0);
    }

    constexpr bool HasAffix(uint16 affixMask, uint8 id)
    {
        return (affixMask & AffixBit(id)) != 0;
    }
}

#endif
