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

#include "Creature.h"
#include "PDDefines.h"
#include "PDMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include <cstdlib>

enum PDEntranceActions
{
    ACTION_ENTER_RANDOM = 1,
    ACTION_ENTER_SEED   = 2
};

// Spawn this NPC (entry 910510) wherever players should queue up, e.g. via
// '.npc add 910510' in a capital city.
class npc_pdungeon_entrance : public CreatureScript
{
public:
    npc_pdungeon_entrance() : CreatureScript("npc_pdungeon_entrance") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Enter the shifting halls (random layout)", GOSSIP_SENDER_MAIN, ACTION_ENTER_RANDOM);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Enter a specific layout...", GOSSIP_SENDER_MAIN, ACTION_ENTER_SEED, "Enter the layout seed number:", 0, true);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action) override
    {
        if (action == ACTION_ENTER_RANDOM)
        {
            CloseGossipMenuFor(player);
            sPDMgr->StartRun(player, urand(1, 0x7FFFFFFE), false);
        }
        return true;
    }

    bool OnGossipSelectCode(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action, char const* code) override
    {
        if (action == ACTION_ENTER_SEED && code)
        {
            CloseGossipMenuFor(player);
            uint32 seed = static_cast<uint32>(std::strtoul(code, nullptr, 10));
            if (!seed)
            {
                seed = urand(1, 0x7FFFFFFE);
            }
            sPDMgr->StartRun(player, seed, false);
        }
        return true;
    }
};

void AddPDEntranceNPCScripts()
{
    new npc_pdungeon_entrance();
}
