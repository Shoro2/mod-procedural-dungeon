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

#ifndef MOD_PDUNGEON_V2_CREATURE_AI_H
#define MOD_PDUNGEON_V2_CREATURE_AI_H

#include "ScriptedCreature.h"
#include "generator/PDv2WalkGrid.h"

namespace PDungeon
{
    class PDv2InstanceScript;
    struct PDv2MobData;

    // Mob AI for the file-less v2 map.
    //
    // Map 760 has no mmaps and never will, so core pathfinding degenerates to
    // straight lines - and a straight line between two platforms crosses the
    // void. Engine line-of-sight cannot gate the chase either: with no VMAP
    // and no server terrain, everything on the map "sees" everything else.
    // The walk grid is the only thing on the server that knows where floor
    // is, so every movement decision here goes through it:
    //
    //   * straight line walkable  -> core chase (its straight line IS safe)
    //   * otherwise               -> module A* waypoints, executed with
    //                                MovePoint(generatePath = false)
    //   * target not on the grid  -> hold; the fall catcher will deal with a
    //                                player who is over the void
    //
    // v1's PDMobAI (PDCreatureAI.h) is the model; the differences are the
    // grid (8.3 yd cells instead of v1's room tiles) and the gate (grid line
    // walkability instead of engine LoS, which v1 could use because its walls
    // were dynamic-tree GameObjects).
    //
    // Two things ride on the same grid for the same reason. A caster holds at
    // range only where the LINE to its target is floor, so it can never plant
    // itself and nuke across a gap it has no fight on. And the AI supplies its
    // own proximity aggro, because the templates it spawns are SHARED with
    // another dungeon and cannot be edited - several of them are deliberately
    // near-blind there - so a mob that would otherwise ignore a player next to
    // it engages, but only one it could walk to.
    //
    // WHAT A MOB CASTS comes from pdungeon_member_spells and nowhere else in
    // code (mod_pdungeon_member_spells.sql carries the whole argument for each
    // pick). Two shapes, one scheduler:
    //
    //   RANGE  holds, then spams its slot-0 FILLER back to back for as long as
    //          it holds, and drops a slot-1 spell in whenever one comes off
    //          cooldown. No gap between fillers beyond the cast itself.
    //   MELEE  chases, swings, and casts a slot-1 spell from melee whenever
    //          one is ready. Bosses use the same shape.
    //
    // The per-fight cooldowns live on the AI rather than in the creature's
    // spell history so that a pack pulled twice opens the same way twice.
    struct PDv2MobAI : public ScriptedAI
    {
        explicit PDv2MobAI(Creature* creature);

        void JustEngagedWith(Unit* who) override;
        void JustDied(Unit* killer) override;
        void JustReachedHome() override;
        void UpdateAI(uint32 diff) override;
        void MovementInform(uint32 type, uint32 id) override;

        // Round B / B4. The core's evade sends a creature home in a STRAIGHT
        // LINE (HomeMovementGenerator, MoveTo with pathfinding disabled on a
        // map with no mmaps), and on this map a straight line crosses the void
        // - so a patroller pulled halfway down its beat would walk back to its
        // spawn block through nothing. The override lets the base do the whole
        // combat stop, then moves the home position to wherever the creature
        // stands, throws that home walk away and puts the patrol back on the
        // grid instead. Every other mob keeps the core's behaviour untouched.
        void EnterEvadeMode(EvadeReason why) override;

    protected:
        // One slot-1 spell with its own live timer. A plain vector rather than
        // an EventMap: the whole schedule is "cast the first one that is
        // ready", and a vector says that in one loop.
        struct KitSpell
        {
            uint32 spellId = 0;
            uint32 cooldownMs = 0;
            uint32 remainingMs = 0;     // 0 = ready
        };

        bool UpdateGridChase(uint32 diff);
        void UpdateCasterCombat(uint32 diff);
        void UpdateProximityAggro(uint32 diff);
        void UpdateImmolation(uint32 diff);
        void CallAlliesForHelp(Unit* victim);
        bool GridLineOkTo(Unit* victim) const;
        void StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid);
        void MoveToWaypoint(size_t index, WalkGrid const& grid);
        void StopWaypointRun(bool resumeChase);

        // Round B / B4, the out-of-combat half of a patroller's life. The
        // route is planned ONCE, on the first idle tick - an A* from where the
        // creature stands to the goal cell its spawn tag carries - and then
        // walked back and forth for the rest of the run, reversed by
        // MovementInform at either end. Everything the chase already owns is
        // reused: the same grid, the same 500 ms decision interval, the same
        // waypoint runner and the same snap radius, because a patrol that
        // agreed with the chase about walkable ground only approximately would
        // eventually step off the world.
        //
        // ONCE is load-bearing. _patrolRoute is never re-planned once it holds
        // a beat: a fight that ends anywhere but on the route is rejoined (see
        // ResumePatrol), not answered with a shorter beat.
        void UpdatePatrol(uint32 diff);
        // Ends the current LEG and asks the next tick to rejoin the beat.
        // Called after an evade (and from JustReachedHome): the creature is
        // rarely standing on one of its own waypoints by then, so the leg it
        // was walking is no longer a leg it can walk from here - but the BEAT
        // still is, which is why the route itself survives untouched.
        void ResumePatrol();

        // Reads the creature's rows once and keeps only what this run's
        // difficulty unlocks, so no tick ever looks at minDiff again.
        void BuildKit();
        // Draws a fresh opening delay for every cooldown spell. Named for what
        // it does: it does NOT zero the kit, it staggers it (see the .cpp).
        void ArmKitForNewFight();
        void TickKit(uint32 diff);
        bool CastReadyKitSpell();
        bool CastFiller();

        PDv2InstanceScript* _instance = nullptr;
        // The spawn tag. Not owned, and not resolvable in the constructor for
        // every creature - see the comment there.
        PDv2MobData const* _mob = nullptr;
        uint32 _repathTimer = 0;
        uint32 _aggroTimer = 0;
        uint32 _lineTimer = 0;
        uint32 _immolationTimer = 0;    // affix 4, accumulates toward 2 s
        std::vector<GridPoint> _waypoints;
        size_t _waypointIndex = 0;
        // B4. The whole beat, kept across runs and reversed at either end -
        // _waypoints is only ever the leg currently being walked, and
        // StopWaypointRun clears it. _patrolTimer shares REPATH_INTERVAL_MS
        // with the chase for the same reason the snap radius is shared.
        std::vector<GridPoint> _patrolRoute;
        uint32 _patrolTimer = 0;
        std::vector<KitSpell> _kit;     // slot-1 spells this run unlocked
        uint32 _fillerSpellId = 0;      // slot-0 spell, or the pack fallback
        uint32 _fillerCooldownMs = 0;   // normally 0 - the filler is spammed
        uint32 _fillerRemainingMs = 0;
        bool _kitBuilt = false;
        bool _followingPath = false;
        bool _lineOk = false;       // last grid-line verdict, refreshed on the tick
        bool _holding = false;      // a caster that has planted itself at range
        bool _hasCalled = false;    // affix 1 already shouted for THIS fight
        bool _patrolActive = false; // _patrolRoute holds a beat worth walking
        // B4. The next patrol tick has to REJOIN the beat rather than start a
        // leg from _patrolRoute[0]: something moved the creature off its route
        // (an evade, which is how every patroller's fight ends, or a
        // knockback). Set by ResumePatrol, cleared by the tick that acts on it.
        bool _patrolRejoin = false;
    };
}

#endif
