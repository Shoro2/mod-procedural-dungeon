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
#include "PDv2InstanceScript.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"

// Round E / WP5: the event room's host, creature_template 910551 "Weary
// Pilgrim" (data/sql/db-world/mod_pdungeon_event.sql).
//
// An event pocket is the optional dead-end room the planner seats off a boss
// segment. The host stands in it, a player talks to him, and the party then
// keeps him alive while waves of this run's own packs walk in - "Hold the
// line". He is therefore the exact inverse of Chromie: she is untouchable
// (unit_flags 514, npcflag 0, spoken through by the instance script), he is
// clickable AND attackable, and the whole point is that mobs go for him.
//
// This file owns nothing about the fight. It owns the CREATURE: the gossip
// that offers the event, the AI that guarantees he never fights back, and the
// two places where he reports to the run. The state machine behind
// PDv2InstanceScript::StartEvent / OnEventHostDied / EventStateFor is WP5
// Task 4; while its stub bodies stand, this NPC is spawnable, clickable and
// completely inert.
namespace
{
    using namespace PDungeon;

    enum PDv2EventGossipActions
    {
        ACTION_START_EVENT = 1
    };

    // The run this creature belongs to, or nullptr when it stands anywhere
    // that is not a live v2 instance (a GM's test spawn in a capital, say).
    // Same shape as PDv2Scaling.cpp's helper of the same name, minus the map
    // check: a gossip click is not a hot path, and GetInstanceScript already
    // answers nullptr off an instanced map.
    PDv2InstanceScript* RunOf(Creature* creature)
    {
        if (!creature)
        {
            return nullptr;
        }
        return dynamic_cast<PDv2InstanceScript*>(creature->GetInstanceScript());
    }

    // The host's AI. PassiveAI and not the module's PDv2MobAI, and not
    // ScriptedAI either: everything this creature must NOT do is exactly what
    // PassiveAI already refuses - MoveInLineOfSight and AttackStart are both
    // empty overrides there, so no amount of provocation makes him swing.
    //
    // PDv2CreatureAIBinder yields on entry 910551 so this AI is the one that
    // actually gets installed (AllCreatureScript::GetCreatureAI runs BEFORE
    // template scripts, so without that yield the binder would win and the
    // pilgrim would hunt players through PDv2MobAI::UpdateProximityAggro).
    class EventHostAI : public PassiveAI
    {
    public:
        explicit EventHostAI(Creature* creature) : PassiveAI(creature)
        {
            // PassiveAI's own constructor already does this. Repeated on
            // purpose: it is the single most load-bearing property of this
            // NPC, and a later edit that swaps the base class must not be
            // able to drop it silently.
            me->SetReactState(REACT_PASSIVE);
        }

        // Nothing of the host's own. The base call is the whole body: it
        // drops him out of a stale engagement once the last attacker is gone
        // (IsEngaged && !IsInCombat -> evade), which on this map is harmless
        // - evade in this core does not heal a creature (Creature.cpp:1992
        // full-heals only on JustRespawned, so `RegenHealth 0` survives it),
        // and MoveTargetedHome is a zero-length move because he is summoned
        // standing on his home position and nothing ever moves him. The
        // straight-line-through-the-void trap that made PDv2MobAI override
        // JustReachedHome cannot bite a creature whose home is under its
        // feet.
        void UpdateAI(uint32 diff) override
        {
            PassiveAI::UpdateAI(diff);
        }

        // The losing condition. Called from inside Unit::setDeathState, so
        // this reports and does nothing else - no despawn, no teleport, no
        // summon. Task 4's body records the loss for the instance's own 1 Hz
        // tick to act on, which is the same discipline OnUnitDeath already
        // follows for players.
        //
        // CreatureAI::JustDied is empty in this core, so there is no base
        // call to chain.
        void JustDied(Unit* /*killer*/) override
        {
            if (PDv2InstanceScript* run = RunOf(me))
            {
                run->OnEventHostDied(me);
            }
        }
    };
}

class npc_pdungeon_event : public CreatureScript
{
public:
    npc_pdungeon_event() : CreatureScript("npc_pdungeon_event") { }

    // One offer, and only while the event has not started. Every other state
    // shows an empty menu on the default gossip text: DEFAULT_GOSSIP_MESSAGE
    // is the core's own sentinel (GossipDef.h:31), so this needs no npc_text
    // row of its own and cannot break on a database that lacks one.
    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);

        PDv2InstanceScript* run = RunOf(creature);
        if (run && run->EventStateFor(creature->GetGUID()) ==
                       PDv2InstanceScript::EventState::Idle)
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
                             "Hold the line with me!", GOSSIP_SENDER_MAIN,
                             ACTION_START_EVENT);
        }

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/,
                        uint32 action) override
    {
        if (action != ACTION_START_EVENT)
        {
            return true;
        }

        // CLOSE FIRST, start second. StartEvent summons a wave around the
        // player and talks to the party; a gossip window still open over
        // that both hides the room the fight begins in and leaves a second
        // click able to re-enter a menu whose state has already moved. The
        // v1 entrance NPC closes in the same order for the same reason.
        CloseGossipMenuFor(player);

        if (PDv2InstanceScript* run = RunOf(creature))
        {
            run->StartEvent(creature, player);
        }
        return true;
    }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new EventHostAI(creature);
    }
};

void AddPDv2EventNPCScripts()
{
    new npc_pdungeon_event();
}
