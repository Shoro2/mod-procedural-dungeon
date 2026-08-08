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

#include "PDv2Affixes.h"

#include "Creature.h"
#include "Unit.h"

namespace PDungeon
{
    void SetDungeonHealth(Creature* creature, uint32 health)
    {
        if (!creature)
        {
            return;
        }

        // At least 1, or a rounding step could hand out a corpse that never
        // died - Lil' Bro's 10 % of a small creature gets close to the floor.
        uint32 const value = health > 1 ? health : 1;

        creature->SetCreateHealth(value);
        creature->SetMaxHealth(value);
        creature->SetHealth(value);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, static_cast<float>(value));
    }

    void ApplyAffixSpawnHealth(Creature* creature, uint16 affixMask)
    {
        if (!creature || !affixMask)
        {
            return;
        }

        // Multiplicative and independent, exactly as that module applies them:
        // its switch runs once per affix and each case reads the health the
        // previous one left behind (DungeonChallenge.cpp:682-702).
        double factor = 1.0;
        if (HasAffix(affixMask, PD_AFFIX_BIG_BOY))
        {
            factor *= AFFIX_BIG_BOY_HEALTH_MULT;
        }
        if (HasAffix(affixMask, PD_AFFIX_BIGGER_BOY))
        {
            factor *= AFFIX_BIG_BOY_HEALTH_MULT;
        }
        if (factor == 1.0)
        {
            return;
        }

        SetDungeonHealth(creature,
                         static_cast<uint32>(static_cast<double>(creature->GetMaxHealth()) * factor));
    }
}
