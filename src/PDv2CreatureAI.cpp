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

#include "PDv2CreatureAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PDDefines.h"
#include "PDv2Affixes.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

namespace PDungeon
{
    namespace
    {
        // How often the chase decision is re-taken. 500 ms was v1's number
        // and survived live testing; the tick is cheap, because an A* runs
        // only when the straight line is blocked and no run is active.
        uint32 const REPATH_INTERVAL_MS = 500;

        // Cells searched around a live position for walkable ground, ~2 cells
        // = up to ~16 yd. Anything further off the surface is not a position
        // to walk to but one to refuse - a target mid-jump over the void.
        int const SNAP_RADIUS_CELLS = 2;

        PDv2InstanceScript* GetV2Instance(Creature* creature)
        {
            return dynamic_cast<PDv2InstanceScript*>(creature->GetInstanceScript());
        }

        GridPoint CellOf(WalkGrid const& grid, float x, float y)
        {
            int gcx = 0, gcy = 0;
            WorldToCell(x, y, gcx, gcy);
            // May land outside the grid; PlanApproach's snap (or its refusal)
            // is the policy for that, not this conversion.
            return grid.LocalFromGlobalCell(gcx, gcy);
        }
    }

    PDv2MobAI::PDv2MobAI(Creature* creature) : ScriptedAI(creature)
    {
        _instance = GetV2Instance(creature);

        // The tag may not be there YET. This AI is built inside SummonCreature
        // (AddToWorld -> AIM_Initialize, Creature.cpp:319), which runs before
        // the instance script has the pointer back and can tag anything - so
        // the constructor takes the tag if it exists and UpdateAI keeps asking
        // until it does. Get, never GetDefault: the tag's PRESENCE is what says
        // "the dungeon spawned this", and creating one here would hand that
        // status to every creature the AI has ever been attached to.
        _mob = creature->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY);
    }

    void PDv2MobAI::JustEngagedWith(Unit* /*who*/)
    {
        // A previous fight may have left a stale run behind (evade clears the
        // motion, not these flags), and the automatic chase that AttackStart
        // starts walks a straight line - which this map cannot promise is
        // floor. Decide on the very next UpdateAI tick.
        StopWaypointRun(false);
        _repathTimer = 0;

        // Same for the caster's plant. AttackStart has just put this creature
        // back into a chase, so a _holding left over from the last fight would
        // skip the stop and leave it running at the target while casting.
        _holding = false;
        _lineOk = false;
        _lineTimer = 0;
    }

    void PDv2MobAI::JustDied(Unit* killer)
    {
        // The instance owns the counters, and it is the only thing that knows
        // how many mobs a room had. The AI just tells it who died - including
        // for the placeholder fallback spawns, which are tagged too.
        if (_instance)
        {
            _instance->OnMobDied(me, killer);
        }
    }

    void PDv2MobAI::MoveToWaypoint(size_t index, WalkGrid const& grid)
    {
        int gcx = 0, gcy = 0;
        grid.GlobalFromLocalCell(_waypoints[index], gcx, gcy);
        double wx = 0.0, wy = 0.0;
        CellCentreToWorld(gcx, gcy, wx, wy);
        me->GetMotionMaster()->MovePoint(WAYPOINT_MOVE_ID_BASE + static_cast<uint32>(index),
                                         static_cast<float>(wx), static_cast<float>(wy),
                                         sPDv2Mgr->GetConfig().floorZ,
                                         FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
                                         /*generatePath=*/false);
    }

    void PDv2MobAI::StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid)
    {
        _waypoints = std::move(waypoints);
        _waypointIndex = 1; // index 0 is the cell the creature stands on
        if (_waypoints.size() < 2)
        {
            _followingPath = false;
            return;
        }
        _followingPath = true;
        MoveToWaypoint(_waypointIndex, grid);
    }

    void PDv2MobAI::StopWaypointRun(bool resumeChase)
    {
        _followingPath = false;
        _waypoints.clear();
        _waypointIndex = 0;
        if (resumeChase)
        {
            if (Unit* victim = me->GetVictim())
            {
                me->GetMotionMaster()->MoveChase(victim);
            }
        }
    }

    void PDv2MobAI::MovementInform(uint32 type, uint32 id)
    {
        if (type != POINT_MOTION_TYPE || !_followingPath || id < WAYPOINT_MOVE_ID_BASE)
        {
            return;
        }

        ++_waypointIndex;
        if (_waypointIndex >= _waypoints.size())
        {
            // Arrived where the target WAS when the path was planned. Resume
            // the chase and force the next tick to re-decide, so a target
            // that moved on is followed by plan rather than by shortcut.
            StopWaypointRun(true);
            _repathTimer = 0;
            return;
        }

        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (!grid)
        {
            StopWaypointRun(false);
            return;
        }
        MoveToWaypoint(_waypointIndex, *grid);
    }

    bool PDv2MobAI::UpdateGridChase(uint32 diff)
    {
        if (_repathTimer > diff)
        {
            _repathTimer -= diff;
            return _followingPath;
        }
        _repathTimer = REPATH_INTERVAL_MS;

        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        Unit* victim = me->GetVictim();
        if (!grid || !victim)
        {
            return _followingPath;
        }

        if (_followingPath)
        {
            // A knockback or crowd-control effect can displace the point run
            // without a MovementInform. Once the motion stack is back to
            // something ordinary with our flag still set, the run is dead -
            // drop it so the decision below starts a fresh one.
            MovementGeneratorType const current =
                me->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (current == IDLE_MOTION_TYPE || current == CHASE_MOTION_TYPE)
            {
                StopWaypointRun(false);
            }
        }

        std::vector<GridPoint> waypoints;
        switch (PlanApproach(*grid,
                             CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                             CellOf(*grid, victim->GetPositionX(), victim->GetPositionY()),
                             SNAP_RADIUS_CELLS, waypoints))
        {
            case ApproachKind::Direct:
                // The straight line is floor the whole way, so the core chase
                // is safe: with no mmaps its generated path degenerates to
                // exactly that straight line.
                if (_followingPath)
                {
                    StopWaypointRun(true);
                }
                else if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                {
                    me->GetMotionMaster()->MoveChase(victim);
                }
                return false;

            case ApproachKind::Path:
                // An active run is left to finish; MovementInform re-decides
                // the moment it ends. Replanning every tick would make the
                // mob stutter each time the target strafes a cell.
                if (!_followingPath)
                {
                    StartWaypointRun(std::move(waypoints), *grid);
                }
                return _followingPath;

            case ApproachKind::Unreachable:
            default:
                // The target is not on walkable ground - mid-air over the
                // void, or off the layout entirely. Walking toward them would
                // walk off the world; hold until they land somewhere real or
                // the fall catcher returns them to the entrance.
                if (_followingPath)
                {
                    StopWaypointRun(false);
                }
                return false;
        }
    }

    bool PDv2MobAI::GridLineOkTo(Unit* victim) const
    {
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (!victim)
        {
            return false;
        }
        if (!grid)
        {
            // No grid means the server knows nothing about floor at all, which
            // EnsureWalkGrid already reported as an error. In that state the
            // melee mobs stand still but still swing at whatever reaches them;
            // a caster that refused to cast on top of that would add nothing
            // but a second symptom. Distance alone decides.
            return true;
        }

        // The same policy the chase runs on, not a second line test:
        // PlanApproach snaps both ends onto the walkable surface first (a live
        // position rarely sits on a cell centre) and answers Direct exactly
        // when the straight line between them is floor the whole way. That
        // equivalence is what the harness pins in CheckApproachPolicy.
        //
        // NEVER IsWithinLOSInMap here. Map 760 has no VMAP and no terrain, so
        // engine line of sight is true between any two points including
        // straight across the void (pd/02 §7) - a caster gated on it would
        // plant itself and nuke a player it has no floor to reach.
        std::vector<GridPoint> waypoints;
        return PlanApproach(*grid,
                            CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                            CellOf(*grid, victim->GetPositionX(), victim->GetPositionY()),
                            SNAP_RADIUS_CELLS, waypoints) == ApproachKind::Direct;
    }

    void PDv2MobAI::UpdateCasterCombat(uint32 diff)
    {
        Unit* victim = me->GetVictim();
        float const castRange = sPDv2Mgr->GetConfig().castRangeYd;

        // Distance first, because it is a subtraction and it decides whether
        // the expensive half is worth running at all. The line test is a grid
        // search, so it rides the chase decision's own 500 ms cadence; between
        // refreshes the last verdict stands.
        bool hold = victim && me->IsWithinCombatRange(victim, castRange);
        if (hold)
        {
            if (_lineTimer > diff)
            {
                _lineTimer -= diff;
            }
            else
            {
                _lineTimer = REPATH_INTERVAL_MS;
                _lineOk = GridLineOkTo(victim);
            }
            hold = _lineOk;
        }

        if (hold)
        {
            if (!_holding)
            {
                // Once per plant, not once per tick: re-clearing the motion
                // master every 500 ms would restart the spline of a creature
                // that is already standing still.
                StopWaypointRun(false);
                me->GetMotionMaster()->Clear(false);
                me->GetMotionMaster()->MoveIdle();
                _holding = true;
            }
            me->SetFacingToObject(victim);

            // ScriptedAI's own form (UnitAI.cpp:76-92): it respects
            // UNIT_STATE_CASTING and the attack timer, and it refuses when the
            // SPELL's own range cannot reach - which is why every caster in
            // mod_pdungeon_packs.sql was given a spell that reaches at least
            // V2.CastRangeYd. Raise that key past a spell's range and its
            // caster silently stops casting.
            DoSpellAttackIfReady(_mob->casterSpellId);
        }
        else
        {
            if (_holding)
            {
                _holding = false;
                _repathTimer = 0;       // decide the approach on this very tick
            }
            UpdateGridChase(diff);
        }

        // Point blank: a caster a melee player has walked into swings too. No
        // out-of-mana melee fallback beyond that in v1 - after the planner's
        // role demotions every caster in the packs is unit_class 8 with a
        // level-80 mana pool, so running dry is not a state these fights reach.
        DoMeleeAttackIfReady();
    }

    void PDv2MobAI::UpdateProximityAggro(uint32 diff)
    {
        if (_aggroTimer > diff)
        {
            _aggroTimer -= diff;
            return;
        }
        _aggroTimer = REPATH_INTERVAL_MS;

        // WHY the module aggroes at all instead of letting the core do it: the
        // dungeon spawns creature_template rows it SHARES with
        // fl-underground-dungeon, where they balance map 741, so this module
        // must not edit them - and six of them are near-blind by design
        // (detection_range 1 on 84284-84287, 2 on the bosses; measured live
        // 2026-08-07). Left to their templates they would stand there while a
        // player walked through the room. So the AI supplies its own eyes and
        // the row stays untouched.
        if (!me->IsAlive() || me->IsInCombat() || me->IsInEvadeMode())
        {
            return;
        }

        float const range = sPDv2Mgr->GetConfig().aggroRangeYd;
        if (range <= 0.0f)
        {
            return;
        }

        Player* target = me->SelectNearestPlayer(range);
        if (!target || !me->IsValidAttackTarget(target))
        {
            return;
        }

        // Never across the void. A mob that pulls something it cannot walk to
        // either stands in combat for ever or walks off the world, so the aggro
        // test is the SAME reachability test the chase uses - "it noticed me"
        // and "it can get to me" must never disagree.
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (grid)
        {
            std::vector<GridPoint> waypoints;
            if (PlanApproach(*grid,
                             CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                             CellOf(*grid, target->GetPositionX(), target->GetPositionY()),
                             SNAP_RADIUS_CELLS, waypoints) == ApproachKind::Unreachable)
            {
                return;
            }
        }

        AttackStart(target);
    }

    void PDv2MobAI::UpdateImmolation(uint32 diff)
    {
        // The bit, never the aura: a player who dispels the fire visual off a
        // mob must not also switch its burn off (PDv2Affixes.h).
        if (!_mob || !HasAffix(_mob->affixMask, PD_AFFIX_IMMOLATION))
        {
            return;
        }

        _immolationTimer += diff;
        if (_immolationTimer < AFFIX_IMMOLATION_INTERVAL_MS)
        {
            return;
        }
        // Subtract rather than reset, so a long map tick does not swallow the
        // remainder and stretch the interval.
        _immolationTimer -= AFFIX_IMMOLATION_INTERVAL_MS;

        // OUT OF COMBAT TOO, which is deliberate and mirrored: that module's
        // tick is gated on the creature being alive and nothing else
        // (DungeonChallengeScripts.cpp:622 and :708-726). It is an aura, so
        // walking past one hurts and pulls the mob - which is the mechanic.
        uint32 const difficulty = _instance ? _instance->GetRunState().difficulty : 0u;
        if (!difficulty)
        {
            return;
        }
        uint32 const damage = difficulty * AFFIX_IMMOLATION_DMG_PER_DIFF;

        // No grid-line gate here, unlike every ranged decision in this file:
        // the radius is 8 yd and a walk-grid cell is 8.3 yd, so the grid cannot
        // resolve anything at this scale and a line test would answer about the
        // cells the two stand in rather than about the 8 yd between them.
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive())
            {
                continue;
            }
            if (me->GetDistance(player) > AFFIX_IMMOLATION_RANGE_YD)
            {
                continue;
            }

            // ENVIRONMENTAL damage, exactly as that module deals it
            // (DungeonChallengeScripts.cpp:722): it has no caster, so it is not
            // resisted, not reflected, and not run through the difficulty
            // damage lever a second time - the difficulty is already the 80x
            // factor above.
            player->EnvironmentalDamage(DAMAGE_FIRE, damage);
        }
    }

    void PDv2MobAI::UpdateAI(uint32 diff)
    {
        if (!_mob)
        {
            _mob = me->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY);
        }

        // Before the victim check: an aura burns whoever stands next to it,
        // fight or no fight.
        UpdateImmolation(diff);

        // Deliberately no distance leash: dungeon mobs chase for as long as
        // the target exists on the map, like any stock instance (operator
        // decision 2026-08-06, replacing a working 150 yd leash). Reset still
        // happens the normal way - UpdateVictim() fails when the target dies
        // or leaves the map - and a target the grid cannot reach is held at
        // bay by UpdateGridChase's Unreachable case, not by walking after it.
        if (!UpdateVictim())
        {
            UpdateProximityAggro(diff);
            return;
        }

        if (_mob && _mob->role == PACK_ROLE_CASTER && _mob->casterSpellId)
        {
            UpdateCasterCombat(diff);
            return;
        }

        // Melee, and bosses with it: in v1 a boss's menace is its stats, and a
        // boss that kited would be unfightable in a 66 yd room.
        UpdateGridChase(diff);
        DoMeleeAttackIfReady();
    }
}

// Binds the AI to every hostile creature on the v2 map, whatever its template.
// The dungeon deliberately spawns stock Blizzard creatures (native loot tables
// are the farming design, 01 §8), so v1's route - a ScriptName on the module's
// own creature_template rows - cannot work here: it would mean editing every
// stock template the packs might ever use. The AI-selection hook binds without
// touching a row; it runs before template scripts and after the selector's pet
// special case (CreatureAISelector.cpp:78-88), so player pets never reach it.
class PDv2CreatureAIBinder : public AllCreatureScript
{
public:
    PDv2CreatureAIBinder() : AllCreatureScript("PDv2CreatureAIBinder") { }

    CreatureAI* GetCreatureAI(Creature* creature) const override
    {
        if (!creature || !sPDv2Mgr->IsEnabled())
        {
            return nullptr;
        }

        Map* map = creature->FindMap();
        if (!map || map->GetId() != sPDv2Mgr->GetConfig().mapId)
        {
            return nullptr;
        }

        // Guardians and totems a player brings along keep their normal AI
        // (pets were already filtered by the selector's special case).
        if (creature->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            return nullptr;
        }

        return new PDungeon::PDv2MobAI(creature);
    }
};

void AddPDv2CreatureScripts()
{
    new PDv2CreatureAIBinder();
}
