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

#include "PDv2InstanceScript.h"

#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PDDefines.h"
#include "PDv2Affixes.h"
#include "PDv2CreatureAI.h"
#include "PDv2LootMgr.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "PDv2TaggedAura.h"
#include "PDv2UILink.h"
#include "Player.h"
#include "Position.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "WorldSession.h"

// Round E / WP5. Paragon integration is OPTIONAL: mod-paragon owns
// ParagonUtils.h, and in the full server build every module's src/ is on the
// include path (modules/CMakeLists.txt), so the header and IncreaseParagonXP
// are there and a won event pays XP. A module-only CI build has no
// mod-paragon, so both the include and the call are guarded and this file
// still compiles standalone - the same shape fl-underground-dungeon uses.
#if __has_include("ParagonUtils.h")
#include "ParagonUtils.h"
#define PDV2_HAS_PARAGON 1
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace PDungeon
{
    namespace
    {
        // How far below the floor plane counts as "fallen off the world".
        // Generous, because a client-authoritative jump can dip briefly.
        float const FALL_MARGIN_YD = 60.0f;

        // Checking every tick would be waste; a fall is not urgent to the yard.
        uint32 const FALL_CHECK_INTERVAL_MS = 1000;

        // Stand-in for a run whose packs could not be drawn (the SQL was not
        // applied, or every pack is disabled). Chosen because it is a stock,
        // level-appropriate humanoid every client already has art for.
        // 29402 is 'Ironwool Mammoth' (level 77-78, faction 190) in this
        // world DB, not the Anub'ar Skirmisher this comment used to
        // claim - and the operator reported exactly that surprise. It
        // stays as the fallback BECAUSE it is absurd in a dungeon: a
        // herd of mammoths is an unmistakable "the packs did not load"
        // signal, which a plausible-looking undead would hide.
        uint32 const PLACEHOLDER_CREATURE = 29402;   // Ironwool Mammoth

        // The radius of the OVERFLOW ring, and nothing else since Round B / B2.
        // Creatures stand on the spawn anchors the kit publishes per chunk
        // (PlanSpawnPoints, PDv2SpawnAnchors.h); this circle around the block
        // centre is what is left for the two cases that have no anchor to
        // stand on: a chunk whose SQL row carries no typed anchors at all
        // (an unapplied mod_pdungeon_chunk_meta.sql, or an older kit), and the
        // picks past the six an ordinary room publishes, which only a raised
        // V2.SpawnsPerRoom / V2.BossRoomAdds can produce. PlanSpawnPoints
        // hard-codes the same 12.0 for its own overflow tail - the harness
        // pins both, so the two must not drift apart.
        float const SPAWN_SPREAD_YD = 12.0f;

        // How far SpawnFromPlan may look for floor when the point a vetoed
        // pick falls back to - the chunk's entry anchor, or the block centre -
        // is itself off the grid. Two cells (16.67 yd) is PDv2CreatureAI's
        // SNAP_RADIUS_CELLS, the same "a position rarely sits dead on a
        // walkable cell centre" tolerance, and it stays inside the room a
        // fallback belongs to. Nothing walkable within it means the fallback
        // stands where it was, which is what this code did before B2.
        int const SPAWN_FALLBACK_SNAP_CELLS = 2;

        // How close a critter may land to a prop before SpawnCritters drops
        // it rather than summon it. A scatter decor rule and the critter rule
        // both call CollectScatter on the SAME block, on two independent RNG
        // streams, and BuildCritterPlan has no spacing gate of its own - so
        // with up to 82 props and dozens of critters in one layout, a rubble
        // pile and a rat will eventually land on the same cell centre. 2 yd
        // is comfortably inside "same spot" and comfortably outside "next
        // cell over", which is all this needs to be.
        double const CRITTER_DECOR_CLEAR_YD = 2.0;

        // Round B / B3: how close a player has to come to a closed barrier
        // before it tells them why it is closed. 12 yd is a bit less than two
        // cells (8.33 yd each), so the hint fires when the portcullis fills
        // the screen and not from the far end of the corridor run.
        float const BARRIER_HINT_YD = 12.0f;

        // Round B / B4: the patrol's own RNG stream. The module's precedent is
        // layoutSeed ^ CONSTANT (PD_DECOR_SEED_MIX, PD_CRITTER_SEED_MIX), and
        // the reason is the same one every time: a draw that shares the layout
        // stream cannot be added, removed or retuned without moving every pick
        // that follows it. The patrol takes it one step further and mixes the
        // SEGMENT in as well, so a run with three bosses draws the same first
        // patroller as a run with one.
        uint32 const PD_PATROL_SEED_MIX = 0x9A7201EDu;

        // The odd golden-ratio word, the standard way to fold an index into a
        // seed without the low bits marching in lockstep. Multiplied, not
        // added: with `+` a segment step of 1 would leave the low bits of two
        // neighbouring segments' seeds one apart.
        uint32 const PD_SEGMENT_SEED_STEP = 0x9E3779B1u;

        // Round B / B5: how often the armed corridors are measured against the
        // players standing in the dungeon. Four times a second, not the 1 Hz
        // branch's once: a player runs at about 7 yd/s, so the default 9 yd
        // radius is crossed in under three seconds and a one-second scan would
        // let somebody walk through an armed corridor untouched. A DBC-free
        // area trigger is impossible on this map (the id comes from the
        // client), so an own timer in Update IS the trigger.
        uint32 const AMBUSH_SCAN_MS = 250;

        // Where an ambush's mobs land relative to the player it fires on:
        // along the corridor's own axis and across it, in yards. 6 yd along
        // puts two in front and two behind - close enough to be an ambush, far
        // enough not to spawn inside the player - and 4 yd across stays inside
        // the lane, which is two cells of 8.333 yd and therefore 8.33 yd of
        // floor either side of its centre. Whether any offset is actually
        // floor is not assumed: FireAmbush vetoes every one of them against
        // the walk grid and falls back to the player's own cell.
        //
        // EIGHT of them for a key that allows up to eight mobs: a full
        // V2.Ambush.Mobs = 8 puts eight creatures in eight distinct places
        // rather than four pairs, which is what the key's own 0..8 clamp has
        // always been documented to mean. The second four sit at 2 yd along -
        // a nearer rank on the same two lines across, because the lane has no
        // room for a second rank ACROSS it (4 yd is already most of the
        // 8.33 yd half-width) while the corridor is 66.67 yd long and has
        // room to spare along it. The first four are unchanged and in their
        // original order, so the default of 4 places its mobs exactly where
        // it did before.
        struct AmbushOffset
        {
            float along;
            float across;
        };

        size_t const AMBUSH_OFFSET_COUNT = 8;

        AmbushOffset const AMBUSH_OFFSETS[AMBUSH_OFFSET_COUNT] = {
            {  6.0f,  4.0f },
            {  6.0f, -4.0f },
            { -6.0f,  4.0f },
            { -6.0f, -4.0f },
            {  2.0f,  4.0f },
            {  2.0f, -4.0f },
            { -2.0f,  4.0f },
            { -2.0f, -4.0f }
        };

        // Round E / WP5: the threat an arriving event attacker is handed on
        // the host. AttackStart alone only picks the FIRST victim; this is the
        // standing entry on the mob's threat list that decides who it goes for
        // NEXT - so a wave mob whose target dies, vanishes or gets out of
        // reach turns back to the pilgrim instead of finding an empty list and
        // dropping combat. A full evade is a different thing and takes the
        // list with it (CreatureAI::_EnterEvadeMode -> CombatStop ->
        // EndAllPvECombat); PDv2MobAI's proximity aggro is what re-engages
        // then, which is the same recovery every other mob on this map has.
        //
        // 1000 is far above the few points a first swing generates and far
        // below what a player produces in a second of real threat, so a party
        // that fights for him still takes the wave off him - which is the
        // whole mechanic.
        float const EVENT_WAVE_HOST_THREAT = 1000.0f;

        // Where a Lil' Bro's two children land relative to the corpse. That
        // module's own offsets, mirrored (DungeonChallengeScripts.cpp:877-878);
        // the second child takes the negatives.
        float const LIL_BRO_OFFSET_X_YD = 2.0f;
        float const LIL_BRO_OFFSET_Y_YD = 1.0f;

        // The 01 §8 bonus-roll table: the FL mats from mod_pdungeon_flmats.sql,
        // weighted hard toward the cheap end (the weights are percent and sum
        // to 100). A flat table would make the top mat as common as the bottom
        // one and there would be no ladder left to climb.
        struct BonusMat
        {
            uint32 entry;
            int    weight;
        };

        BonusMat const BONUS_MATS[5] = {
            { 920100, 60 },     // Forgotten Shard
            { 920101, 25 },     // Forgotten Sliver
            { 920102, 10 },     // Forgotten Fragment
            { 920103,  4 },     // Forgotten Core
            { 920104,  1 }      // Forgotten Relic
        };

        // Round E / L2. How many of the five conf currency tiers a KILL may
        // pay: T1..T3, indices 0..2. T4 and T5 share the same arrays because
        // the roll is one formula, but they belong to Chromie's cache (Task 7)
        // and are deliberately out of reach here - a trash mob paying the
        // run's closing tier would leave the finale with nothing to give.
        int const LOOT_CURRENCY_MOB_TIERS = 3;

        // The gear pool a run boss pays from, at every dlvl. Named here rather
        // than in PDv2LootMgr.h because the pools are DATA (the header says
        // so): the engine knows only which name each source asks for, and
        // pointing the boss at another pool is one string, not a redesign.
        // Chests and the final cache switch pool with dlvl (V2.Loot.IccDlvl);
        // the boss does not, because RAID_HC is already the top of the ladder
        // a five-man boss is worth.
        std::string_view const LOOT_POOL_BOSS = "RAID_HC";

        // The sender and the subject of the bags-are-full letter. Chromie is
        // the module's own NPC and the one the player already met at the
        // entrance, so a letter from her is the dungeon writing rather than an
        // unattributed system mail.
        char const* const LOOT_MAIL_SUBJECT = "The Forgotten Depths";
        char const* const LOOT_MAIL_BODY = "Your bags were full.";

        // Round C / C8, the finale's clock. Four seconds is long enough to
        // read a line of chat and short enough that nobody walks off before
        // the portal is up; design §C8.2 names it, so it is a constant and not
        // a conf key - the beat is authored, not tuned per realm.
        //
        // The 1 Hz branch is what measures it, so a line lands at the first
        // tick at or after its deadline: the spacing is four seconds plus at
        // most one tick's phase, never less than four.
        //
        // It spaces the LINES only. The whole beat, measured from the last
        // boss's death: Chromie and the cache at 0 s, the three lines at ~4,
        // ~8 and ~12 s, and the portal on the very next tick after the third
        // line, ~13 s (TickFinale says why it is not a fourth 4 s beat).
        uint32 const FINALE_STEP_MS = 4000;

        // Where the three objects stand, relative to the arena centre the last
        // boss died in. Chromie and the cache share the +x side so the player
        // finds both in one glance, the portal takes the -x side so nobody
        // walks into it while looting; 6 yd clears a player's own body and
        // stays well inside the smallest arena the kit ships (a 33 yd room is
        // 16.67 yd of floor either side of its centre). Deliberately axis
        // offsets and not a ring: the finale is staged for a player standing
        // in the middle of the room, and an axis reads as a line-up.
        float const FINALE_CHROMIE_OFFSET_X_YD = 6.0f;
        float const FINALE_CACHE_OFFSET_Y_YD = 4.0f;
        float const FINALE_PORTAL_OFFSET_X_YD = -6.0f;

        // -pi/2, and MEASURED rather than derived: at orientation 0.0f the
        // operator reported the dead-end cache standing "90 Grad nach rechts"
        // (T2 2026-09-08), and 220a295 answered it by summoning those at a
        // fixed 4.712389f = 3*pi/2 = -pi/2 normalised. GO 910068 carries the
        // SAME display 259, so if that quarter turn is a property of the model
        // and not a one-off scene choice, the finale cache needs it too. It is
        // therefore subtracted from the angle to the arena centre rather than
        // replacing it: the chest still faces inward, one model-forward
        // correction later. Written as the literal radian and not float(M_PI)/2
        // because M_PI is not portably visible through <cmath> on MSVC.
        //
        // T2 OBSERVABLE, not a proof: runde29 §C8 asks whether the cache faces
        // the middle of the room or is turned 90 degrees, and the answer is
        // what decides whether this term stays.
        float const FINALE_CACHE_MODEL_FACING_OFFSET = -1.5707964f;

        // Chromie's three lines, spoken in order. English like every other
        // module text; authored in design §C8.2 and quoted verbatim, so an
        // edit here is a content change and belongs in the spec first.
        // No creature_text rows and no Talk(): the module has never had either,
        // and three lines do not justify a DB table plus a locale pipeline.
        uint32 const CHROMIE_LINE_COUNT = 3;

        char const* const CHROMIE_LINES[CHROMIE_LINE_COUNT] = {
            "Well, that took you long enough! The timeways are humming again.",
            "Take what the Depths owe you - you have earned every bit of it.",
            "When you are ready, step through. Azealia is waiting."
        };

        // WP6's TaggedAuraAmount stood here, file-local, and said so at
        // length. Round E / WP9 moved it to PDv2TaggedAura.h unchanged: the
        // loot path asks the same question of a SECOND tag, from two other
        // translation units, and two copies of "what has this player
        // unlocked" inside one module is how the answer starts differing by
        // call site. The header carries the whole FT contract with it.
    }

    PDv2InstanceScript::PDv2InstanceScript(InstanceMap* map) : InstanceScript(map)
    {
    }

    void PDv2InstanceScript::OnPlayerEnter(Player* player)
    {
        if (!player || !player->GetSession())
        {
            return;
        }

        // The plan is per account, and an instance belongs to whoever first
        // walked into it. Binding here rather than at teleport time keeps this
        // script usable no matter how the player got onto the map.
        if (!_accountId)
        {
            _accountId = player->GetSession()->GetAccountId();
        }

        auto const plan = sPDv2Mgr->GetPlan(_accountId);
        if (!plan)
        {
            LOG_WARN(PD_LOG, "PDv2: player {} entered map {} with no stored plan - "
                             "nothing to spawn", player->GetName(), instance->GetId());
            return;
        }

        if (sPDv2Mgr->EntranceWorldPos(*plan, _entranceX, _entranceY, _entranceZ))
        {
            _haveEntrance = true;
        }

        // Two reasons to rebuild what is standing here.
        //
        // SEED CHANGED: a plan can be re-rolled while this instance is alive -
        // the account keeps the instance, so the old creatures and the old walk
        // grid would otherwise survive under new terrain. Rebuilding on a seed
        // change is what makes `.pdungeon v2 gen` mean the same thing inside as
        // outside.
        //
        // RUN ALREADY FINISHED: walking back into a cleared dungeon used to
        // hand the player an empty one - the boss dead, the platforms bare, and
        // no way to start again short of re-rolling the layout (operator,
        // 2026-08-10: "the instance should be reset first, so players can just
        // re-enter"). Now the same seed re-populates. Deliberately a REBUILD
        // and not an instance reset: resetting would kick everyone standing in
        // here, and on this map a kick means a teleport out of a dungeon that
        // has no terrain to fall back to.
        bool const seedChanged = _spawned && _spawnedSeed != plan->effectiveSeed;
        bool const runFinished = _spawned && _run.complete;
        if (seedChanged || runFinished)
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} rebuilding ({}) - seed {} -> {}",
                     instance->GetInstanceId(),
                     seedChanged ? "plan re-rolled" : "previous run was completed",
                     _spawnedSeed, plan->effectiveSeed);
            DespawnAll();
            _spawned = false;
            _gridReady = false;
            _gridTried = false;

            // A fresh run, not the old one with its boss counter already full.
            // SpawnFromPlan re-derives difficulty, roomsTotal and bossTotal, and
            // the `!_run.started` block below re-arms the clock and the leader.
            _run = PDv2RunState{};

            // ...and the per-room bookkeeping with it. SpawnFromPlan refills
            // every one of these, but a rebuild that failed halfway must not
            // leave the previous layout's room segments behind for the next
            // OnMobDied to index into.
            _roomAlive.clear();
            _roomPlanned.clear();
            _roomSegment.clear();
            _roomIsBoss.clear();
            _segmentPlanned.clear();
            _segmentKilled.clear();
            // Round C / C5, the same reasoning one layout further: a rebuilt
            // dungeon must not hand a corpse a checkpoint in a room that no
            // longer exists, so the four per-room facts and the checkpoint
            // they feed go with the rest. The run starts at the entrance again.
            _roomSpot.clear();
            _roomBX.clear();
            _roomBY.clear();
            _roomChain.clear();
            _checkpointChain = -1;
            _checkpointRoom = -1;
            // The portcullis GameObjects themselves went with _decorGuids in
            // DespawnAll, and the grid holes they cut go with the grid the
            // rebuild throws away - what is left here is the run's memory of
            // which segment was already paid for.
            _barriers.clear();
            // ...and which corridors were already sprung (design §B5.4: "spots
            // rebuilt with the run"). A rebuild re-arms every one of them,
            // which is the whole difference between a trap and a one-off.
            _ambushes.clear();
            // Round E / WP5, and for the same reason: the pilgrim and every
            // attacker still standing went with _spawnedGuids in DespawnAll
            // and the won event's cache with _decorGuids, so what is left
            // here is only the run's memory that a pocket was already played.
            // A rebuild re-stages every event, which is what makes a re-entry
            // a new dungeon rather than a spent one.
            _events.clear();
            // Round C / C8. Chromie, her cache and the portal went with
            // _spawnedGuids and _decorGuids in DespawnAll above; this is the
            // state machine that was walking them, and it has to go too. Note
            // which rebuild reason usually gets here: `runFinished` - a
            // completed run is re-entered, so the ordinary way the finale ends
            // is that somebody walks back in and the dungeon re-populates
            // (design §C8.4). A finale still mid-line when that happens is cut
            // off, which is correct: the room it was staged in no longer
            // exists. Whole-struct assignment rather than field by field, so a
            // field added to Finale later cannot be forgotten here.
            _finale = Finale{};
            // Round C / C7 review fold, and the one thing in this block that
            // is NOT a reset: the run generation counts UP. Every other line
            // here throws away what the previous run knew; this one is how the
            // UI link learns that it happened, so that a client whose K record
            // says "0 rooms cleared" is told again rather than keeping the old
            // layout's green blocks (RunGeneration says why the count alone is
            // not enough).
            ++_runGeneration;
            MarkRunDirty();
        }

        EnsureWalkGrid(*plan);

        if (!_spawned)
        {
            SpawnFromPlan(*plan);
            // Under the SAME guard as the creatures, deliberately: one flag
            // decides what this instance has standing in it, so a re-entry
            // cannot double the props while leaving the mobs alone, and the
            // rebuild above tore both down together.
            std::vector<Position> decorPositions;
            SpawnDecor(*plan, decorPositions);
            SpawnKitProps(*plan);
            // Round D / D2. Both prop passes are in, none of the barriers is,
            // so _decorGuids holds exactly the furniture a patrol has to walk
            // around - and the map is wanted before SpawnPatrols, which plans
            // each file's beat with it.
            BuildPropCells();
            // Reads decorPositions, so it must come after SpawnDecor filled
            // it. Ambient life, same guard, own GUID list and own teardown.
            SpawnCritters(*plan, decorPositions);
            SpawnDeadEndChests(*plan);
            // After SpawnFromPlan, which is what filled _segmentPlanned: a
            // barrier is evaluated the moment it is placed, and a segment
            // whose denominator is zero has to open right there.
            SpawnBarriers(*plan);
            // After the barriers on purpose - and since Round D / D2 for a
            // different reason than the one that used to stand here. A patrol
            // belongs to a CORRIDOR now, not to the segment a barrier defines,
            // so the two are no longer paired by ownership at all. What still
            // pairs them is the GRID: SpawnBarriers takes a closed portcullis'
            // four lane cells out of it, and every file's beat is planned on
            // that same grid, so running second is what makes a beat into a
            // sealed boss corridor end AT the gate instead of failing to plan.
            // The declaration of SpawnPatrols carries the other half - the
            // spawn tag keeps the TRUE doorway cell, so the beat grows to it on
            // the first plan after the barrier falls.
            SpawnPatrols(*plan);
            // Last. Nothing is summoned here - the ambush only ARMS a corridor
            // and remembers what it will spawn - so it has nothing to race, but
            // it belongs at the end of the same guard as everything else the
            // rebuild tears down.
            SpawnAmbushPlan(*plan);
            // Round E / WP5, and genuinely last: the pilgrim is a summon, so
            // it belongs after everything that reads the walk grid rather than
            // adds to it, and its rim points are vetoed against the grid
            // SpawnBarriers has already cut. Nothing else in the build depends
            // on it - the event pocket was skipped by the room pass, plans no
            // trash, moves no counter and carries no barrier denominator.
            SpawnEventRooms(*plan);
            _spawned = true;
            _spawnedSeed = plan->effectiveSeed;
        }

        // The run starts when someone walks in, not when the plan is generated:
        // the clock has to measure play, and `.pdungeon v2 gen` can be minutes
        // before anyone enters. A rebuild reset the state above, so a re-roll
        // starts a fresh run with a fresh clock.
        if (!_run.started)
        {
            _run.started = true;
            _run.startedMs = getMSTime();
            _leaderGuid = player->GetGUID().GetCounter();
            MarkRunDirty();
        }

        // Push the panel state to whoever just walked in.
        //
        // It used to be pushed only by `gen` and by the link handshake, so
        // entering a dungeon that was generated earlier - the ordinary "I am
        // back, let me run it again" case - left the UI closed with no way to
        // reopen it (operator, 2026-08-10). Arrival is the right trigger
        // because it is the one event every entry path shares: the command, the
        // panel's own Enter button, and a summon all end up here.
        sPDv2UILink->SendCfg(player);
    }

    // A logout sends the player home from OnPlayerBeforeLogout, and that far
    // teleport takes them off this map (Player.cpp:1569-1571) BEFORE the core's
    // own RepopAtGraveyard for a dead character (WorldSession.cpp:633-637) - so
    // the pending death has to be forgotten here, or the release veto in
    // PDClientLink would still answer "wait" for someone the dungeon no longer
    // has, and the logout-while-dead path stops being the core's (design
    // 2026-09-03 §B1.2).
    void PDv2InstanceScript::OnPlayerLeave(Player* player)
    {
        if (player)
        {
            _pendingRespawn.erase(player->GetGUID());
        }
    }

    // Ground-effect carriers must decorate, not fight.
    //
    // Several stock kit spells drop a "void zone": a creature with no AI whose
    // whole job is to carry a persistent area aura, painted where it lands. The
    // one that surfaced is Swarming Shadows (71264 -> creature 38163, aura
    // 71267 from creature_template_addon) - the Blood-Queen mechanic, and the
    // operator wants exactly what it looks like: the fire spawns on the player
    // and their movement paints a line with it. What is NOT wanted is the
    // damage. Six of those auras ticking at once read as "invisible things are
    // attacking me", because the carrier cannot be selected, targeted or seen
    // as a source - and in melee there is nowhere to step out to anyway.
    //
    // Neutralising it HERE and not in the shared rows is the point: entry 38163
    // and its addon aura belong to Icecrown Citadel, which runs on this same
    // server. Editing creature_template would defuse the real encounter too.
    //
    // The rule is deliberately about the FLAG, not about a list of entries: on
    // this map a creature the player cannot select is by definition scenery or
    // a marker, never a fight, so any future kit spell with the same shape is
    // covered without a code change. Our own spawns are always selectable -
    // they are what the dungeon is - so they never match.
    //
    // Friendly rather than aura-stripped on purpose: the aura IS the visual,
    // and a persistent area aura re-picks its targets every tick, so a carrier
    // that is no longer an enemy keeps painting and stops hurting.
    void PDv2InstanceScript::OnCreatureCreate(Creature* creature)
    {
        if (!creature)
        {
            return;
        }

        // Spawn telemetry. Written at INFO during the 2026-08-10 invisible-
        // attacker hunt, where it settled the case in one session: the only
        // unselectable spawn was Swarming Shadows (38163), forty of them.
        // The next hunt will want it again - and since Round E / R3 it takes
        // V2.Debug rather than a logger level, so the operator turns on one
        // key instead of learning what Logger.module 6 means.
        //
        // ONE LINE PER CREATURE, which is a hundred-odd of them per run: the
        // gate is what keeps this out of a host's log, and INFO under the gate
        // is what makes it visible the moment the key is on.
        if (PDv2Debug())
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} spawn: entry {} '{}' faction {} "
                             "selectable {} visible-model {}",
                     instance->GetInstanceId(), creature->GetEntry(),
                     creature->GetName(), creature->GetFaction(),
                     creature->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) ? "no" : "yes",
                     creature->GetDisplayId());
        }

        if (!creature->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE))
        {
            return;
        }

        creature->SetFaction(FACTION_FRIENDLY);
        creature->SetReactState(REACT_PASSIVE);

        // Tracked so TickVoidZones can make the ground under it hurt again -
        // friendliness took the aura's targets away, not the hazard's job.
        _voidZones.push_back(creature->GetGUID());
    }

    void PDv2InstanceScript::TickVoidZones()
    {
        if (_voidZones.empty())
        {
            return;
        }

        SpellInfo const* dmgSpell = sSpellMgr->GetSpellInfo(SPELL_SWARMING_SHADOWS_DMG);
        if (!dmgSpell)
        {
            return;
        }

        // The zone's own numbers, not invented ones: 4 yd radius and the 2925
        // base roll both come out of Spell.dbc at runtime, so a data edit to
        // the spell keeps working here without a rebuild.
        float const radius = dmgSpell->Effects[EFFECT_0].CalcRadius();
        int32 const base = dmgSpell->Effects[EFFECT_0].CalcValue();

        // Prune despawned carriers first; the survivors are this tick's zones.
        std::vector<Creature*> zones;
        zones.reserve(_voidZones.size());
        for (size_t i = 0; i < _voidZones.size();)
        {
            if (Creature* c = instance->GetCreature(_voidZones[i]))
            {
                zones.push_back(c);
                ++i;
            }
            else
            {
                _voidZones[i] = _voidZones.back();
                _voidZones.pop_back();
            }
        }

        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->IsGameMaster())
            {
                continue;
            }

            bool inside = false;
            Creature* source = nullptr;
            for (Creature* zone : zones)
            {
                if (player->IsWithinDist(zone, radius, true))
                {
                    inside = true;
                    source = zone;
                    break;
                }
            }
            if (!inside)
            {
                continue;
            }

            // ONE application per player per second, from one named source,
            // however many pools overlap underfoot. The full spell-damage
            // path is deliberately skipped - it is what produced forty
            // parallel ticks - but the school, the log line and the number
            // are the original's, so absorb-less is the one honest deviation.
            uint32 const dealt = Unit::DealDamage(source, player, uint32(base), nullptr,
                                                  SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW,
                                                  dmgSpell, false);
            source->SendSpellNonMeleeDamageLog(player, dmgSpell, dealt,
                                               SPELL_SCHOOL_MASK_SHADOW, 0, 0, false, 0);
        }
    }

    bool PDv2InstanceScript::ConsumeRunDirty()
    {
        bool const dirty = _runDirty;
        _runDirty = false;
        return dirty;
    }

    void PDv2InstanceScript::RollBonusLoot(Unit* killer)
    {
        // A pet, guardian or totem got the last hit as often as its owner did;
        // the credit belongs to the player either way.
        Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player || !player->GetSession())
        {
            return;
        }

        int const bonusPct = sPDv2Mgr->GetConfig().lootBonusRollPct;
        if (bonusPct <= 0)
        {
            return;
        }

        // Basis points, so the loot multiplier's two decimals survive the roll:
        // chance% is BonusRollPct x lootMult, and lootMult is already carried
        // x100, so their product IS the chance in 1/10000.
        int const chanceBp = bonusPct * static_cast<int>(_run.lootMultX100);

        // THE DETERMINISM BOUNDARY RUNS HERE. Layout and spawn selection go
        // through PDRandom because a dungeon that reshuffles itself between
        // visits is a different dungeon. A loot roll must not: seeded, the same
        // kill on the same seed would drop the same mat for ever, which turns
        // farming into a lookup table. So the rolls below use the core's urand.
        if (static_cast<int>(urand(1, 10000)) > chanceBp)
        {
            return;
        }

        int roll = static_cast<int>(urand(1, 100));
        uint32 entry = BONUS_MATS[0].entry;
        for (BonusMat const& mat : BONUS_MATS)
        {
            roll -= mat.weight;
            if (roll <= 0)
            {
                entry = mat.entry;
                break;
            }
        }

        // AddItem sends the standard received-item line, and says so itself
        // when the bags are full - nothing to add in that case.
        if (!player->AddItem(entry, 1))
        {
            return;
        }

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        ChatHandler(player->GetSession()).PSendSysMessage(
            "Bonus: {}", proto ? proto->Name1 : std::string("?"));
    }

    void PDv2InstanceScript::ForEachRunPlayer(Map* map,
                                              std::function<void(Player*)> const& fn)
    {
        if (!map)
        {
            return;
        }

        // The same walk, and the same null-session skip, FinishRun makes: a
        // player whose session has gone is on the list for a few more ticks
        // and can be handed nothing - AddItem would work and the mail would
        // not, and neither would ever be seen.
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin();
             it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->GetSession())
            {
                continue;
            }
            fn(player);
        }
    }

    void PDv2InstanceScript::GrantItem(Player* player, uint32 item, uint32 count) const
    {
        if (!player || !item || !count)
        {
            return;
        }

        // LOAD-BEARING, and not a defensive formality: Item::CreateItem below
        // calls ABORT() when the template is unknown (Item.cpp:1120), so an
        // item id that only exists in the conf - a typo in
        // V2.Loot.Currency.Tier2.Item, a pool regenerated against a world DB
        // this realm does not have - would take the worldserver down the first
        // time somebody's bags were full. Player::AddItem answers that case
        // with a plain false, which is why the crash is only reachable through
        // the fallback and only through this one lookup.
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
        if (!proto)
        {
            return;
        }

        // AddItem already sends the client's own "You receive item" line, so a
        // grant that lands needs nothing said about it.
        if (player->AddItem(item, count))
        {
            return;
        }

        // Bags full. The drop is NOT dropped: it goes to the mailbox, the way
        // fl-underground-dungeon has always handled the same moment
        // (UndergroundUtils.h:127), from the NPC the player already knows.
        MailDraft draft(LOOT_MAIL_SUBJECT, LOOT_MAIL_BODY);
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (Item* mailItem = Item::CreateItem(item, count, player))
        {
            // Saved INSIDE the transaction that sends the mail, which is what
            // every core caller does (cs_send.cpp:129): the item row has to
            // exist before the mail row points at it, or a crash between the
            // two leaves a letter holding an item that was never written.
            mailItem->SaveToDB(trans);
            draft.AddItem(mailItem);
        }
        draft.SendMailTo(trans, MailReceiver(player),
                         MailSender(MAIL_CREATURE, NPC_CHROMIE));
        CharacterDatabase.CommitTransaction(trans);

        // The one line the funnel says out loud. Guarded rather than assumed:
        // every caller today comes through ForEachRunPlayer, which has already
        // skipped a sessionless player, but the mail above works without a
        // session and this does not.
        if (WorldSession* session = player->GetSession())
        {
            ChatHandler(session).PSendSysMessage(
                "Your bags are full - {} was mailed to you.", proto->Name1);
        }
    }

    void PDv2InstanceScript::RollCurrency(Creature* creature,
                                          PDv2MobData const& /*tag*/)
    {
        if (!creature)
        {
            return;
        }

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();

        for (int tier = 0; tier < LOOT_CURRENCY_MOB_TIERS; ++tier)
        {
            uint32 const item = cfg.lootCurrencyItem[tier];
            if (!item)
            {
                // 0 is how an operator turns one tier off, and the conf is
                // read live, so it is a per-kill question and not a load-time
                // one.
                continue;
            }

            // The conf's own difficulty gate. The three mob tiers ship at 1,
            // the bottom of the dial, which is the same as ungated - but the
            // key is documented as "the run difficulty a tier needs before it
            // drops at all", and an operator who raises T3's would otherwise
            // find that it did nothing. The dial is the run's FROZEN one, like
            // every other gameplay read in this file.
            if (cfg.lootCurrencyMinDiff[tier] > static_cast<int>(_run.difficulty))
            {
                continue;
            }

            int const chanceBp = GameChanceBp(cfg.lootCurrencyChancePct[tier],
                                              static_cast<int>(_run.roomFactorX100));
            if (chanceBp <= 0)
            {
                continue;
            }

            // PERSONAL, per player and per tier (D6). Five people in a dungeon
            // are five independent rolls of the same chance, not one drop for
            // five people to argue about - and the roll is urand, never
            // PDRandom, for the reason RollBonusLoot spells out above.
            ForEachRunPlayer(creature->GetMap(),
                             [this, item, chanceBp](Player* player)
            {
                if (static_cast<int>(urand(1, PD_GAME_CHANCE_BP_MAX)) <= chanceBp)
                {
                    GrantItem(player, item, 1);
                }
            });
        }
    }

    void PDv2InstanceScript::RollMaterials(Creature* creature,
                                           PDv2MobData const& /*tag*/)
    {
        if (!creature)
        {
            return;
        }

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();

        // The band's TOP, not the count: the run's frozen dlvl decides how
        // large a stack this dungeon can pay, and the stack itself is rolled
        // inside it per player. One computation for the whole kill, because
        // nothing in it is per player.
        int const maxCount = GameMatsMaxCount(static_cast<int>(_run.dlvl),
                                              cfg.dlvlCap,
                                              cfg.lootMatsMaxPerMobAtCap);

        // Same funnel and the same recipients as the currency above, and the
        // chance roll is INSIDE the walk on purpose: D6 makes materials a
        // personal roll, so each player either gets their own stack or does
        // not. Rolling once for the kill would have made it one shared drop
        // wearing a per-player payout, which is invisible at the shipped 100 %
        // and wrong at every other value.
        ForEachRunPlayer(creature->GetMap(),
                         [this, &cfg, maxCount](Player* player)
        {
            if (static_cast<int>(urand(1, 100)) > cfg.lootMatsChancePct)
            {
                return;
            }

            // ONE item id per player per kill, with the count as its stack: a
            // player who is owed four materials is owed four of SOMETHING, and
            // four separate draws would fill a bag with singles instead.
            uint32 const item = sPDv2LootMgr->RollMaterial();
            if (!item)
            {
                return;
            }
            GrantItem(player, item, urand(1, static_cast<uint32>(maxCount)));
        });
    }

    void PDv2InstanceScript::InjectBossGear(Creature* creature, Unit* killer)
    {
        if (!creature)
        {
            return;
        }

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();

        // D9: the whole part of Items x lootMult always drops, the fraction is
        // the percent chance of one more. The engine rolls the dice the
        // engine-free formula deliberately does not (PDv2GameMath.h says why).
        int const count = GameScaledCount(cfg.lootBossItems,
                                          static_cast<int>(_run.lootMultX100),
                                          static_cast<int>(urand(1, 100)));
        if (count <= 0)
        {
            return;
        }

        // The class filter's looter, and nothing more: a pet or a totem landed
        // the blow as often as its owner did, so the credit follows the same
        // walk RollBonusLoot takes. Who may actually take the item out of the
        // corpse is the group's loot rules, which we do not touch - and a
        // killer we cannot resolve to a player simply rolls unfiltered.
        Player* looter = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself()
                                : nullptr;

        // Round E / WP9, once per corpse rather than once per item, and a
        // looter we could not resolve above answers Off here - so a boss that
        // died to a void zone rolls exactly what it rolled before this
        // existed, unfiltered on both counts.
        uint8_t const profile = PDv2LootMgr::StatProfileFor(looter);

        for (int i = 0; i < count; ++i)
        {
            uint32 const item =
                sPDv2LootMgr->RollGear(LOOT_POOL_BOSS, looter, profile);
            if (!item)
            {
                // An empty or unloaded pool. Nothing to add, and nothing to
                // say either - PDv2LootMgr::Describe is where an operator sees
                // that a pool is at 0 rows.
                continue;
            }

            // chance 100, no quest flag, group 0, exactly one: a row that is
            // certain by construction, because the roll already happened above
            // and the loot table is only being used to carry the result into
            // the window the player is about to open.
            creature->loot.AddItem(
                LootStoreItem(item, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, 1, 1));
        }
    }

    void PDv2InstanceScript::OnMobDied(Creature* creature, Unit* killer)
    {
        if (!creature)
        {
            return;
        }

        // Get, not GetDefault: a creature the dungeon did not spawn has no tag
        // and must not move a counter. Nothing else on map 760 carries one.
        PDv2MobData* tag = creature->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY);
        if (!tag || tag->counted)
        {
            // `counted` guards the one case that would otherwise double-score:
            // a creature can die more than once as far as the AI is concerned
            // (JustDied fires again after a resurrect or a feign-death path).
            return;
        }
        tag->counted = true;

        // THE SPLIT GOES FIRST, before a single counter moves. It raises the
        // run total and the room's live count by the number of children it
        // actually managed to summon, so the decrements below land on numbers
        // that already know about them: "Mobs 23/25" becomes "24/27" instead of
        // overflowing past its own total, and a room whose last mob just became
        // two children is not announced as cleared and then cleared again.
        if (HasAffix(tag->affixMask, PD_AFFIX_LIL_BRO)
            && tag->splitDepth < AFFIX_LIL_BRO_MAX_DEPTH)
        {
            SplitOnDeath(creature, *tag, killer);
        }

        // Round B / B4-B5: the patrol and the ambush are RISK, not progress.
        // They fight, scale, split and drop loot like any dungeon mob, but a
        // run whose total counted them could not be finished without hunting
        // down a patroller, and a barrier whose denominator counted them would
        // seal itself behind mobs that may never be pulled at all.
        if (tag->countsForRun)
        {
            ++_run.killed;
            if (tag->isRunBoss && _run.bossKilled < _run.bossTotal)
            {
                ++_run.bossKilled;
                // Round C / C5: the checkpoint is the FURTHEST cleared boss
                // hall, so it moves only forward. `>` against the running
                // maximum, not "the latest kill", makes it order-proof: the
                // barriers gate the bosses in chain order in practice, but
                // nothing in the code enforces that, and a boss pulled out of
                // order must not drag the respawn point backwards. Pockets
                // carry chainIndex -1 and never hold a boss, so the initial
                // -1 can only be beaten by a real spine room - the entrance
                // is chain 0 and holds no pack at all (SpawnFromPlan skips
                // RoomEntrance), so chain 0 never enters this vector.
                if (tag->roomIndex < _roomChain.size() &&
                    _roomChain[tag->roomIndex] > _checkpointChain)
                {
                    _checkpointChain = _roomChain[tag->roomIndex];
                    _checkpointRoom = static_cast<int>(tag->roomIndex);
                }
            }

            if (tag->roomIndex < _roomAlive.size() && _roomAlive[tag->roomIndex] > 0)
            {
                if (--_roomAlive[tag->roomIndex] == 0)
                {
                    // Every emptied room, boss halls included: this is what
                    // the panel's cleared-room map is resent on, and that map
                    // paints the halls (PDv2InstanceScript.h, RoomsEmptied-
                    // Count).
                    ++_run.roomsEmptied;

                    // Round E / R2: the numerator has to count the same rooms
                    // the denominator does, and roomsTotal is the ORDINARY
                    // count since the dial started meaning ordinary rooms. A
                    // boss hall is reported by bossKilled/bossTotal on the same
                    // HUD line; counting it here as well would push the room
                    // pair past its own total on the last pull of the run.
                    // _roomAlive is only stepped for a room that HAD a pack, so
                    // the bounds check above already proved the index is live.
                    if (!_roomIsBoss[tag->roomIndex])
                    {
                        ++_run.roomsCleared;
                    }
                }
            }

            // B3's numerator, and the boss room is out of it on purpose: its
            // pack stands BEHIND the barrier, so counting it would ask the
            // player to clear a room they cannot reach yet (design 2026-09-03
            // §B3.1, the single-boss-segment softlock).
            if (tag->roomIndex < _roomSegment.size() && !_roomIsBoss[tag->roomIndex])
            {
                int const seg = _roomSegment[tag->roomIndex];
                if (seg >= 1 && static_cast<size_t>(seg) < _segmentKilled.size())
                {
                    ++_segmentKilled[static_cast<size_t>(seg)];
                    EvaluateBarrier(seg);
                }
            }
        }
        MarkRunDirty();

        // 01 §8: the FL mats drop ON TOP of whatever the creature's own loot
        // table gives, which is what makes the dungeon a way to farm existing
        // content rather than a replacement for it. Round E / WP8 narrowed
        // "whatever it gives" to the GOLD by default - the items are dropped
        // unless V2.Loot.NativeItems says otherwise, and the block below it
        // says why.
        //
        // A Lil' Bro child pays nothing, native table included - one carrier is
        // seven corpses (1 -> 2 -> 4), and seven loot tables plus seven bonus
        // rolls out of one mob would make the highest difficulty the cheapest
        // place to farm. That module suppresses exactly this, and by both
        // halves: the children are flagged noLoot at birth
        // (DungeonChallengeScripts.cpp:898) and the flag is spent on death by
        // clearing the loot and the lootable dynamic flag (:838-843).
        if (tag->splitDepth)
        {
            creature->loot.clear();
            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        }
        else
        {
            RollBonusLoot(killer);

            // Round E / L2-L4, the kill funnel. Everything a dead dungeon mob
            // is worth beyond its own table, in the one place a reader can see
            // all of it: currency and materials into the bags of every player
            // on the map, gear into the corpse when the mob was the room's
            // boss. Split children are excluded by the branch above, which is
            // the same reason they pay no native loot either.
            PDv2Config const& cfg = sPDv2Mgr->GetConfig();

            // D7. An extra mob - a WP5 event wave, a WP6 respawn copy - pays
            // currency only when the operator says so, because it can be
            // farmed in place and currency is progression. Materials it always
            // pays, which is why only this half is gated.
            if (!tag->isExtra || cfg.lootExtraMobsDropCurrency)
            {
                RollCurrency(creature, *tag);
            }
            RollMaterials(creature, *tag);

            // WP8, operator finding 4 of Runde 31: "the Shadowfang mobs keep
            // their stock loot". They do, and that is the bug - the packs are
            // drawn from STOCK creature_template entries (SFK 2529/3853-3859,
            // Scholomance 10471-11551, Ahn'kahet, Twilight), so a PDv2 corpse
            // hands out those dungeons' loot tables: their greens, their
            // quest-chain drops, their vendor trash. None of it is FL content
            // and none of it is what a PDv2 kill is supposed to be worth - the
            // currency and the materials go straight to the bags above, and a
            // room boss carries the injected gear below. So the item half of
            // the native table goes, and BEFORE InjectBossGear: this clear
            // would otherwise wipe the very gear the next lines add.
            //
            // The GOLD is kept ON PURPOSE, and it is the whole reason the loot
            // is not simply emptied: Unit::Kill filled this loot at
            // Unit.cpp:14083-14092 - FillLoot for the items, generateMoneyLoot
            // for the coins - and nothing has opened the corpse yet, so the
            // money is sitting there ready. Money is not content: it does not
            // belong to Shadowfang, it is what any mob of level 80 is worth,
            // and taking it away would make every kill in the dungeon a net
            // repair-bill loss. SetLootMode(0) would have killed both halves
            // at once, which is exactly why it is not the tool here.
            if (!cfg.lootNativeItems)
            {
                uint32 const gold = creature->loot.gold;
                creature->loot.clear();
                creature->loot.gold = gold;
            }

            // isRunBoss, NOT the creature's entry: the boss slot of a room is
            // whatever PDv2PackMgr put in it, trash stand-in included, and the
            // tag is the only thing that knows the room is done with it. The
            // same flag the completion counter above reads.
            if (tag->isRunBoss)
            {
                InjectBossGear(creature, killer);
            }
        }

        // An empty corpse must not sparkle. Unit::Kill set UNIT_DYNFLAG_LOOTABLE
        // a few lines before JustDied (Unit.cpp:14221-14227) on a loot that was
        // still full, so a mob that dropped no gold and whose items were just
        // dropped would keep the golden shimmer and hand every player an empty
        // loot window. The Lil' Bro branch above clears the flag for the same
        // reason; this is the same statement made once for every other path
        // through the funnel, and Loot::empty() is what decides - it counts the
        // GOLD as well (LootMgr.h:367), so a corpse that still carries coins
        // stays lootable exactly as it should.
        if (creature->loot.empty())
        {
            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        }

        // Round E / WP6 / D7: the respawn echoes, and they come after the whole
        // funnel above for the same reason the boss gear comes before the
        // finale - the kill has to be FINISHED, scored and looted and its
        // corpse complete, before that corpse is allowed to raise anything.
        //
        // The guard list, in order: the operator has the mechanic on
        // (V2.Respawn.Enable); this was an ordinary LAYOUT mob, because a
        // patrol, an ambush or an event attacker is risk on the road and
        // echoing one would turn a corridor into a treadmill; it was not the
        // room's boss, whose hall the run has already been credited for and
        // whose second corpse would read as a second boss; it was not a Lil'
        // Bro child, not an echo itself and not any other extra, or one corpse
        // would breed for ever; and the finale is not staged yet, because
        // nothing new may walk in over Chromie's three lines.
        //
        // AND NO COUNTER MOVES HERE - the whole difference to SplitOnDeath,
        // which raises _run.total and _roomAlive on purpose. A split child IS
        // the planned mob, still owed to the run; an echo is a reward paid for
        // a kill that is already scored, so adding it to the total would push
        // the denominator up on every single pull and leave a HUD that can
        // never reach its own number. The copies carry countsForRun = false
        // and PD_ROOM_NONE, so OnMobDied will never score them either - both
        // halves have to agree or the arithmetic drifts apart.
        if (sPDv2Mgr->GetConfig().respawnEnable && tag->countsForRun && !tag->isRunBoss
            && !tag->splitDepth && !tag->isRespawnCopy && !tag->isExtra && !_run.complete)
        {
            SpawnRespawnCopies(creature, *tag, killer);
        }

        // AFTER the injection, and that order is the point: FinishRun stages
        // the finale, and the corpse the player is about to loot has to be
        // complete before Chromie starts talking over it.
        if (!_run.complete && _run.bossTotal > 0 && _run.bossKilled >= _run.bossTotal)
        {
            FinishRun();
        }
    }

    void PDv2InstanceScript::FinishRun()
    {
        _run.complete = true;
        MarkRunDirty();

        // 01 §8 pays for the dungeon that was BUILT, not for the fraction of it
        // that was walked: the room count is what dlvl bought, and the boss is
        // what proves the run. Difficulty pays nothing on purpose (the formula
        // has no difficulty argument, and PDv2GameMath.h says why).
        //
        // What was BUILT is the ordinary rooms PLUS the boss halls, and that
        // sum is exactly the number the pre-R2 roomsTotal carried by itself.
        // Round E / R2 narrowed roomsTotal to the ORDINARY rooms - the dial's
        // own number - so the boss halls are added back HERE; without them the
        // payout would silently drop by bossRooms at every dial, and a rename
        // of one counter would have retuned the reward curve. bossTotal counts
        // one boss per boss room (PDv2PackMgr.h's contract, pinned at the spawn
        // site), so the sum is the old total and a run pays the dxp it always
        // did.
        PDv2RunReward const reward = sPDv2Mgr->GrantRunReward(
            _accountId, static_cast<int>(_run.roomsTotal) + _run.bossTotal);

        // The HUD's completion toast rides the same grant the chat lines below
        // announce, and fires exactly once because FinishRun does.
        sPDv2UILink->SendEnd(instance, reward);

        // Round E / R1 (spec D15). The difficulty cap moves HERE and nowhere
        // else on the gameplay path: finishing a run at dial D unlocks
        // D + unlock, and the unlock is the smaller death value when the run
        // cost anybody a death and the larger clean value when it did not.
        //
        // Measured against `_run.difficulty` - the dial FROZEN at spawn - and
        // never against the account's live cfg_difficulty or its current cap.
        // Against the live setting, a player who lowered the dial mid-run
        // would be paid for a run they did not play; against the cap, farming
        // easy runs at a high cap would inch the cap upwards for ever, which
        // is the loop PDv2Mgr.h refuses from the other end.
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        int const unlock = _run.deaths ? cfg.capDeathUnlock : cfg.capCleanUnlock;
        int const capBefore = sPDv2Mgr->GetAccountState(_accountId).diffCap;
        int const capNow = sPDv2Mgr->RaiseDiffCap(_accountId,
                                                  int(_run.difficulty) + unlock);

        // STRICTLY greater, which is the whole reason `capBefore` is read at
        // all: RaiseDiffCap is a ratchet and answers the cap in force whether
        // or not it moved, so the return value alone cannot tell an unlock
        // from a no-op. Announcing "difficulty 40 unlocked" to a party that
        // has been capped at 40 for a week is exactly the kind of noise a
        // raid warning must never carry.
        bool const capRose = capNow > capBefore;

        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->GetSession())
            {
                continue;
            }
            ChatHandler handler(player->GetSession());
            handler.PSendSysMessage("The Forgotten Depths yield: +{} dungeon XP (dlvl {}).",
                                    reward.dxpGained, reward.newDlvl);
            if (reward.leveledUp)
            {
                // No longer "a wider difficulty band": the dial has been open
                // from the first run since 2026-08-08, and telling a player
                // they just unlocked something they always had is the kind of
                // small lie that makes the rest of the UI untrustworthy.
                handler.PSendSysMessage("Your dungeon level is now {} - a deeper layout and "
                                        "more of the depths are open.", reward.newDlvl);
            }

            if (capRose)
            {
                // The finale's raid-warning voice rather than another sys
                // message: the N kind reaches chat AND the warning frame, and
                // of everything this completion says, the ceiling on the NEXT
                // run is the one line worth the frame.
                sPDv2UILink->SendNotice(player, Acore::StringFormat(
                    "Difficulty {} unlocked ({} death(s)).", capNow,
                    uint32(_run.deaths)));
                // ...and the panel behind it, because `c.diffMax` is what
                // bounds the slider (flpdui.lua). Without this push the player
                // is told about a ceiling their own dial still refuses to
                // reach, until whatever happens to send the next C payload.
                sPDv2UILink->SendCfg(player);
            }
        }

        // History is written on COMPLETION only, so an abandoned run leaves no
        // row. Accepted for v1: a run that was never finished has nothing to
        // rank, and an "open" row would need a writer for every way a player
        // can walk away. Full history is a later slice.
        // `difficulty` is the 1..100 dial the run was played at;
        // `difficulty_x100` is deliberately absent - it is the retired band
        // column and is left at its default rather than fed a number from a
        // different scale (mod_pdungeon_runs_difficulty.sql says why the column
        // survives). loot_mult_x100 stays, because the loot multiplier really
        // is a x100 quantity.
        // Round E / R1 adds `deaths`: the row already records the difficulty
        // the run was PLAYED at, and the death count is what turns that pair
        // into the cap decision this run made - without it the history cannot
        // say why one clear at 40 unlocked 45 and the next unlocked 43.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_runs (seed, map_id, instance_id, leader_guid, account_id, "
            "dlvl, difficulty, loot_mult_x100, rooms_cleared, deaths, result, completed_at) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, 1, NOW())",
            _spawnedSeed, instance->GetId(), instance->GetInstanceId(), _leaderGuid,
            _accountId, reward.newDlvl, uint32(_run.difficulty), _run.lootMultX100,
            _run.roomsCleared, uint32(_run.deaths));

        LOG_INFO(PD_LOG, "PDv2: account {} completed instance {} (seed {}): {}/{} rooms, "
                         "{} kill(s), {} death(s), +{} dxp -> dlvl {}, cap {} -> {}",
                 _accountId, instance->GetInstanceId(), _spawnedSeed,
                 uint32(_run.roomsCleared), uint32(_run.roomsTotal), uint32(_run.killed),
                 uint32(_run.deaths), reward.dxpGained, reward.newDlvl, capBefore, capNow);

        // Nobody is teleported out. The dungeon stays walkable after its last
        // boss because farming it is the point (01 §8) - the way out is the way
        // the player came in.
        //
        // Round C / C8 adds a way out that is OFFERED rather than taken: the
        // finale's portal is a click, so the decision above is untouched. Last
        // in FinishRun on purpose - the reward, the toast, the chat lines and
        // the history row are what completing a run means, and none of them
        // may wait on a summon.
        StartFinale();
    }

    void PDv2InstanceScript::StartFinale()
    {
        // The last boss's own hall: OnMobDied moved the checkpoint onto the
        // room it just cleared before it called FinishRun, so this reads the
        // arena centre of the boss room with the HIGHEST chainIndex that is
        // dead - which, when the bosses are killed in chain order, is the one
        // the players are standing in. Killed out of order it is not: the
        // checkpoint only ever moves forward (OnMobDied says why), so a run
        // whose second boss fell before its first stages the finale in boss
        // 2's hall. Accepted - "the furthest hall the run reached" is a
        // defensible place for the reward to stand, and the alternative would
        // be a second, contradictory notion of "last".
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!CheckpointSpot(x, y, z))
        {
            // No boss room to stand in. Only reachable when the run was
            // completed without a tagged boss kill moving the checkpoint -
            // a GM finishing a run by hand, or a layout whose boss room lost
            // its pack - so it is a warning, not a normal branch, and the
            // entrance is the one position this script always knows.
            if (!_haveEntrance)
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} completed with neither a checkpoint nor "
                                 "an entrance - no finale", instance->GetInstanceId());
                return;
            }
            x = _entranceX;
            y = _entranceY;
            z = _entranceZ;
            LOG_WARN(PD_LOG, "PDv2: instance {} completed with no cleared boss hall "
                             "(checkpoint room {}) - the finale is staged at the entrance",
                     instance->GetInstanceId(), _checkpointRoom);
        }

        // Facing the centre, all three of them: GetAngle is the angle FROM
        // this position TO the one named, so an object standing off-centre and
        // asked for the angle to the centre looks inward at the players.
        //
        // Each spot is grid-vetoed first (VetoFinaleSpot says why), and the
        // veto runs on the POSITION, before the angle is taken: a snapped spot
        // one cell over still has to look at the centre, not at where it used
        // to stand.
        float chromieX = x + FINALE_CHROMIE_OFFSET_X_YD;
        float chromieY = y;
        VetoFinaleSpot(chromieX, chromieY, "Chromie");
        Position chromiePos(chromieX, chromieY, z, 0.0f);
        chromiePos.SetOrientation(chromiePos.GetAngle(x, y));

        Creature* chromie = instance->SummonCreature(NPC_CHROMIE, chromiePos);
        if (!chromie)
        {
            LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon Chromie "
                              "(missing creature_template {}?) - no finale",
                      instance->GetInstanceId(), uint32(NPC_CHROMIE));
            return;
        }

        // No gravity flag, for the same reason no other summon on this map
        // carries one any more (Round D / D3, SpawnTaggedMob says it at
        // length): the core strips it on the first movement update and until
        // then it is a visible hover, while nothing falls without it. Nothing
        // else of SpawnTaggedMob applies - she is NOT a run mob: no
        // PDv2MobData, so she cannot move a counter, cannot be scaled, cannot
        // be affixed and cannot be split. Her template makes her unattackable
        // in the first place; the missing tag is what makes that structural.
        //
        // And no SetReputationRewardDisabled either, unlike every other summon
        // this module makes: she cannot be killed, so there is no kill for the
        // core to reward. Measured 2026-09-09 - creature_template 910550 has
        // unit_flags 514 = 0x2 UNIT_FLAG_NON_ATTACKABLE | 0x200
        // UNIT_FLAG_IMMUNE_TO_NPC, and no creature_onkill_reputation row.
        chromie->SetHomePosition(chromie->GetPositionX(), chromie->GetPositionY(),
                                 chromie->GetPositionZ(), chromie->GetOrientation());

        // _spawnedGuids, not _decorGuids: she is a creature, and that list is
        // the one DespawnAll walks with DespawnOrUnsummon. It looks up each
        // GUID and never reads a tag, so an untagged creature in it is torn
        // down exactly like a tagged one.
        _spawnedGuids.push_back(chromie->GetGUID());

        // The cache, beside her rather than behind her: 4 yd on +y from her
        // own spot, still facing the centre. The four zeros after the angle
        // are the quaternion, and an all-zero quaternion is not a facing -
        // SummonGameObject rebuilds the rotation from this angle about +Z, the
        // same reasoning SpawnDeadEndChests spells out.
        float cacheX = x + FINALE_CHROMIE_OFFSET_X_YD;
        float cacheY = y + FINALE_CACHE_OFFSET_Y_YD;
        VetoFinaleSpot(cacheX, cacheY, "cache");
        Position const cachePos(cacheX, cacheY, z, 0.0f);
        // ...and one quarter turn back off that angle, because display 259
        // does not point where its orientation says it does
        // (FINALE_CACHE_MODEL_FACING_OFFSET carries the measurement).
        float const cacheFacing = Position::NormalizeOrientation(
            cachePos.GetAngle(x, y) + FINALE_CACHE_MODEL_FACING_OFFSET);
        if (GameObject* cache = instance->SummonGameObject(
                GO_REWARD_CHEST, cacheX, cacheY, z, cacheFacing,
                0.0f, 0.0f, 0.0f, 0.0f, 0))
        {
            _decorGuids.push_back(cache->GetGUID());
        }
        else
        {
            // Chromie still speaks and the portal still opens: a missing chest
            // costs the reward, not the way home.
            LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon the finale cache "
                              "(missing gameobject_template {}?)",
                      instance->GetInstanceId(), uint32(GO_REWARD_CHEST));
        }

        _finale.active = true;
        _finale.step = 0;
        _finale.nextAtMs = getMSTime() + FINALE_STEP_MS;
        _finale.chromie = chromie->GetGUID();
        _finale.x = x;
        _finale.y = y;
        _finale.z = z;

        // "chain index", not "chain room": the number is _checkpointChain, the
        // position IN the chain, while the LOG_WARN on the entrance fallback
        // above prints _checkpointRoom under the words "checkpoint room". Two
        // similar phrases carrying two different numbers is how a reader
        // misreads the honest `-1` this line shows on that fallback path as a
        // bug (C8 review, minor 6).
        LOG_INFO(PD_LOG, "PDv2: instance {} finale staged at ({:.1f}, {:.1f}, {:.1f}) - "
                         "chain index {}, first line in {} ms",
                 instance->GetInstanceId(), x, y, z, _checkpointChain, FINALE_STEP_MS);
    }

    void PDv2InstanceScript::VetoFinaleSpot(float& x, float& y, char const* what) const
    {
        // The same veto SpawnFromPlan's room pass, SpawnPatrols' spawn point
        // and SplitOnDeath's child offsets take, applied to the three finale
        // spots as well (C8 review, minor 5). On the shipped kit the risk is
        // low: the checkpoint spot the offsets hang off is itself vetoed, all
        // 60 room_boss chunks are floor across the whole centre quad, and a
        // 33 yd arena has 16.67 yd of floor either side of centre against a
        // worst-case 7.21 yd offset. The ENTRANCE fallback has no such
        // guarantee, and the failure mode there is silent - a GameObject does
        // not fall and Chromie has gravity off, so all three would simply
        // hover over the void with nothing in the log.
        WalkGrid const* grid = GetWalkGrid();
        if (!grid)
        {
            return;
        }

        int gcx = 0, gcy = 0;
        WorldToCell(x, y, gcx, gcy);
        GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
        if (grid->At(cell.x, cell.y))
        {
            return;
        }

        GridPoint snapped;
        if (!NearestWalkable(*grid, cell.x, cell.y, SPAWN_FALLBACK_SNAP_CELLS, snapped))
        {
            // The offset stands, which is exactly what this code did before
            // the veto existed - a reward standing over the void is still
            // better than no reward, and the line below is what tells the
            // operator which of the three to look for.
            LOG_WARN(PD_LOG, "PDv2: instance {} finale {} stands on a void cell and found no "
                             "floor within {} cell(s) - it stays where it was",
                     instance->GetInstanceId(), what, SPAWN_FALLBACK_SNAP_CELLS);
            return;
        }

        int scx = 0, scy = 0;
        grid->GlobalFromLocalCell(snapped, scx, scy);
        double wx = 0.0, wy = 0.0;
        CellCentreToWorld(scx, scy, wx, wy);
        x = static_cast<float>(wx);
        y = static_cast<float>(wy);

        LOG_INFO(PD_LOG, "PDv2: instance {} moved the finale {} onto cell ({}, {}) - "
                         "its offset landed off the walk grid",
                 instance->GetInstanceId(), what, snapped.x, snapped.y);
    }

    void PDv2InstanceScript::TickFinale()
    {
        if (!_finale.active)
        {
            return;
        }

        // Signed difference, not `getMSTime() >= nextAtMs`: getMSTime() is a
        // uint32 of milliseconds since start-up and wraps every 49.7 days, and
        // a plain `>=` across that wrap would park the finale for another 49
        // days. This is the same wrap-safe reading GetMSTimeDiffToNow gives
        // the rest of the module, written as a deadline rather than an age.
        if (static_cast<int32>(getMSTime() - _finale.nextAtMs) < 0)
        {
            return;
        }

        if (_finale.step < CHROMIE_LINE_COUNT)
        {
            Creature* chromie = instance->GetCreature(_finale.chromie);
            if (!chromie)
            {
                // She is in _spawnedGuids, and DespawnAll - its only caller
                // being the rebuild, which resets this struct in the same
                // block - is the only thing in the module that takes her off
                // the map. So this is a guard against a core-side despawn
                // nobody has seen rather than a path a player can walk into;
                // it is loud because if it ever fires, the cause is worth the
                // log line.
                LOG_WARN(PD_LOG, "PDv2: instance {} lost Chromie before line {} - "
                                 "the finale ends here",
                         instance->GetInstanceId(), _finale.step + 1);
                _finale.active = false;
                return;
            }

            // Say, not Yell: the dungeon is one room wide at this point and
            // everyone who finished the boss is standing in it. LANG_UNIVERSAL
            // so both factions read it.
            chromie->Say(CHROMIE_LINES[_finale.step], LANG_UNIVERSAL);
            // The portal follows the LAST line by one tick, not by a fourth
            // beat: leaving the deadline where it is makes step 3 already due,
            // so it fires on the next 1 Hz tick (design §C8.3). Advancing it
            // here unconditionally would put the portal at ~16 s and leave
            // four silent seconds after the last line, which reads as "did it
            // break?" - so only the gaps BETWEEN lines get the four seconds.
            if (_finale.step + 1 < CHROMIE_LINE_COUNT)
            {
                _finale.nextAtMs += FINALE_STEP_MS;
            }
            ++_finale.step;
            return;
        }

        // The way home, and the last beat. -6 yd on x puts it on the opposite
        // side of the arena centre from Chromie and her cache, so the players
        // walk past the reward to reach it.
        //
        // Vetoed here rather than in StartFinale, and that costs nothing: the
        // only thing that moves the walk grid during a run is a barrier
        // opening, which only ever ADDS walkable cells (SetCellsWalkable), so
        // a spot that was floor 13 seconds ago is still floor. Doing it here
        // keeps the portal's position in one place instead of storing a fourth
        // and fifth float on Finale.
        float portalX = _finale.x + FINALE_PORTAL_OFFSET_X_YD;
        float portalY = _finale.y;
        VetoFinaleSpot(portalX, portalY, "portal");
        Position const portalPos(portalX, portalY, _finale.z, 0.0f);
        if (GameObject* portal = instance->SummonGameObject(
                GO_AZEALIA_PORTAL, portalX, portalY, _finale.z,
                portalPos.GetAngle(_finale.x, _finale.y), 0.0f, 0.0f, 0.0f, 0.0f, 0))
        {
            _decorGuids.push_back(portal->GetGUID());

            // The OpenBarrier pattern: one notice per player on the map. It is
            // a notice and not a chat line because the addon paints those as
            // raid warnings since C7, which is what makes a portal appearing
            // behind the player impossible to miss.
            Map::PlayerList const& players = instance->GetPlayers();
            for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
            {
                sPDv2UILink->SendNotice(it->GetSource(), "A portal to Azealia opens.");
            }

            LOG_INFO(PD_LOG, "PDv2: instance {} opened the portal to Azealia at "
                             "({:.1f}, {:.1f}, {:.1f})",
                     instance->GetInstanceId(), portalX, portalY, _finale.z);
        }
        else
        {
            // Nobody is stranded by this: the dungeon stays walkable and the
            // way the player came in is still open (01 §8).
            LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon the portal to Azealia "
                              "(missing gameobject_template {}?)",
                      instance->GetInstanceId(), uint32(GO_AZEALIA_PORTAL));
        }

        // Spent either way. The finale plays once per run, and the rebuild -
        // not this line - is what lets the next run play it again.
        _finale.active = false;
    }

    std::vector<std::string> PDv2InstanceScript::PatrolSnapshot() const
    {
        std::vector<std::string> lines;
        for (ObjectGuid const& guid : _spawnedGuids)
        {
            Creature* c = instance->GetCreature(guid);
            if (!c)
            {
                continue;   // despawned, or this run was torn down under us
            }
            PDv2MobData const* tag = c->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY);
            if (!tag || !tag->isPatrol)
            {
                continue;
            }
            PDv2MobAI const* ai = dynamic_cast<PDv2MobAI const*>(c->AI());
            if (!ai)
            {
                // Tagged as a patroller but not carrying this AI: that is a
                // finding rather than a nuisance, so it gets a line of its own.
                lines.push_back(Acore::StringFormat(
                    "{} entry {} guid {} | NO PDv2MobAI attached",
                    c->GetName(), c->GetEntry(), guid.GetCounter()));
                continue;
            }
            lines.push_back(c->IsAlive() ? ai->PatrolStateLine()
                                         : "DEAD " + ai->PatrolStateLine());
        }
        return lines;
    }

    void PDv2InstanceScript::DespawnAll()
    {
        for (ObjectGuid const& guid : _spawnedGuids)
        {
            if (Creature* c = instance->GetCreature(guid))
            {
                c->DespawnOrUnsummon();
            }
        }
        _spawnedGuids.clear();

        // Delete(), not DespawnOrUnsummon(): a GameObject summoned with
        // respawnTime 0 has no spawn record, and GameObject::DespawnOrUnsummon
        // only deactivates such an object - it stays on the map, which on a
        // rebuild means the old dungeon's torches float over the new one.
        // Delete() is the call that puts it on the removal list.
        for (ObjectGuid const& guid : _decorGuids)
        {
            if (GameObject* go = instance->GetGameObject(guid))
            {
                go->Delete();
            }
        }
        _decorGuids.clear();
        // Round D. The prop map DESCRIBES those objects, so it dies with them:
        // a rebuild that kept it would cost the next layout's patrols a turn
        // around furniture the previous dungeon owned.
        _propCells.clear();

        // A death recorded against the layout being torn down has nothing left
        // to be teleported to, so it is dropped here rather than answered by
        // the next tick against a dungeon that no longer exists.
        _pendingRespawn.clear();

        // Critters are creatures, not GameObjects: DespawnOrUnsummon is their
        // teardown path, the same one _spawnedGuids uses above, not the
        // GameObject Delete() the props get.
        for (ObjectGuid const& guid : _critterGuids)
        {
            if (Creature* c = instance->GetCreature(guid))
            {
                c->DespawnOrUnsummon();
            }
        }
        _critterGuids.clear();
    }

    void PDv2InstanceScript::EnsureWalkGrid(BlockPlan const& plan)
    {
        if (_gridTried)
        {
            return;
        }
        _gridTried = true;

        std::string error;
        // Round D / D2: the clearance layer is laid in by the same call and in
        // the same loop as the mask, so a cell's "is floor" and its "how much
        // room is there" can never come from different chunk records.
        if (!BuildWalkGrid(plan, [](int chunkId) { return sPDv2Mgr->WalkMaskFor(chunkId); },
                           &_grid, &error,
                           [](int chunkId) { return sPDv2Mgr->PatrolLayersFor(chunkId); }))
        {
            // Without the grid the mobs stand where they spawned and never
            // chase - the dungeon degrades, it does not crash. Loud log line
            // because the only known cause is kit metadata that was not
            // applied or does not match the plan's chunk ids.
            LOG_ERROR(PD_LOG, "PDv2: instance {} could not build its walk grid ({}) - "
                              "creatures will not chase", instance->GetInstanceId(), error);
            return;
        }
        _gridReady = true;
        LOG_DEBUG(PD_LOG, "PDv2: instance {} walk grid {}x{} cells, {} walkable",
                  instance->GetInstanceId(), _grid.width, _grid.height,
                  uint32(_grid.WalkableCount()));
    }

    float PDv2InstanceScript::FacingTowardDoorway(BlockPlan const& plan,
                                                  PlacedBlock const& block,
                                                  float fromX, float fromY) const
    {
        unsigned bit = 0;
        if (block.isEvent)
        {
            // A pocket is a dead end, so its ONE socket is the way in and the
            // way out. The popcount test is what says "one", and a mask with
            // two bits on an event block would mean the planner changed under
            // this function - in which case guessing is worse than declining.
            if (block.socketMask && (block.socketMask & (block.socketMask - 1u)) == 0)
            {
                bit = block.socketMask;
            }
        }
        else if (block.chainIndex >= 1)
        {
            // The SAME walk SpawnBarriers seals and ValidateBlockPlan proved,
            // so the doorway the boss looks at is the doorway the party's own
            // portcullis stands in. Chain 0 is the entrance and answers 0.
            bit = SpineRunInto(plan, block.chainIndex, nullptr);
        }

        if (bit != SOCKET_N && bit != SOCKET_E && bit != SOCKET_S && bit != SOCKET_W)
        {
            // The contract check SpawnBarriers makes for the same reason:
            // LaneCellsForSocket reads anything that is not one of the four as
            // SOCKET_E, and a boss staring at the wrong wall is worse than one
            // staring due north, which is what 0 keeps.
            return 0.0f;
        }

        int cells[2][2] = { { 0, 0 }, { 0, 0 } };
        LaneCellsForSocket(bit, cells);

        // The midpoint of the doorway's two lane cells, i.e. the centre of the
        // gap rather than one of its jambs. (row, col) -> global cell
        // (x = col, y = row) is the one translation the planner's table leaves
        // to an engine - the same one SpawnBarriers' laneCells makes.
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 2; ++i)
        {
            double wx = 0.0, wy = 0.0;
            CellCentreToWorld(block.bx * PD_CELLS_PER_BLOCK + cells[i][1],
                              block.by * PD_CELLS_PER_BLOCK + cells[i][0], wx, wy);
            mx += wx * 0.5;
            my += wy * 0.5;
        }

        // 2D, like every other geometry question this module asks: the dungeon
        // is one floor plane, and a Z term would only measure the height of a
        // jump. GetAngle already answers in [0, 2 PI); the normalise is the
        // house shape for "this is an orientation" (the finale cache's facing
        // is built the same way).
        Position const from(fromX, fromY, 0.0f, 0.0f);
        return Position::NormalizeOrientation(
            from.GetAngle(static_cast<float>(mx), static_cast<float>(my)));
    }

    Creature* PDv2InstanceScript::SpawnTaggedMob(uint32 entry, PDv2MobData const& proto,
                                                 float x, float y, float z,
                                                 uint32 baseHealthOverride, float orientation)
    {
        // Exactly ON the floor plane. This used to add 0.5 yd "so a creature is
        // not spawned inside the floor", and the offset was a PERMANENT hover:
        // there is no server-side gravity on this map to settle it (the
        // paragraph below carries the core lines), so a mob spawned half a yard
        // up stayed half a yard up until a pull and evade walked it onto its
        // home position - operator report 2026-08-06. Round D / D3 removed the
        // gravity flag that made the same mistake a second time; the floor
        // plane this function is handed is the only Z a summon ever gets.
        //
        // The facing is the caller's (Round E / WP8) and defaults to the 0.0f
        // every spawn used before, so a mob nobody has an opinion about still
        // looks due north.
        Creature* c = instance->SummonCreature(entry, Position(x, y, z, orientation));
        if (!c)
        {
            return nullptr;
        }

        // ...and the SAME angle into the home position, because an evade snaps
        // a creature back to it: a boss turned to face his doorway only at the
        // summon would forget it the first time a party pulled him and ran.
        c->SetHomePosition(x, y, z, orientation);

        // NOTHING THIS DUNGEON SUMMONS PAYS KILL REPUTATION. A policy of the
        // module, not a patch for one pack: PDv2 fills its rooms from arbitrary
        // stock creature_template entries, and a stock entry can carry a
        // creature_onkill_reputation row that was written for hand-placed,
        // finite spawns in a real zone. The same row inside an infinitely
        // repeatable procedural dungeon is a faucet, and no pack file can be
        // trusted to notice - so the switch lives on the spawn path, where every
        // mob the module creates has to pass.
        //
        // The case that produced the rule, measured on this box 2026-09-09: all
        // seven trash members of pack 7 "Cult of the Damned" (10471, 10476,
        // 10477, 10486, 10488, 10489, 11551 - stock Scholomance) carry
        // RewOnKillRepFaction1 529 (Argent Dawn), RewOnKillRepValue1 10,
        // MaxStanding1 6 = REP_EXALTED, IsTeamAward1 0. That is +10 Argent Dawn
        // per kill, for either faction, all the way to Exalted, with no turn-in
        // and no NPC visit - about 4200 trash kills for Neutral -> Exalted. Not
        // one of the 52 pack members that shipped before it had such a row.
        //
        // Creature::SetReputationRewardDisabled (Creature.h:378) sets the flag
        // the core tests FIRST: Player::RewardReputation returns before it even
        // looks the ReputationOnKillEntry up when IsReputationRewardDisabled()
        // is true (Player.cpp:5962-5963), and that function is the only way a
        // creature death grants reputation - KillRewarder::_RewardReputation
        // (KillRewarder.cpp:192-196) is its sole kill-side caller, reached from
        // _RewardPlayer (KillRewarder.cpp:237). The flag is initialised false in
        // the Creature constructor (Creature.cpp:280) and nothing else resets
        // it, so this one call is the whole switch. It is the same call the
        // core's own instance scripts make for the same purpose
        // (instance_hyjal.cpp:197).
        //
        // Deliberately NOT a data patch: editing creature_template or
        // creature_onkill_reputation here would retune Scholomance itself, and
        // the next pack drawn from stock entries would reopen the hole.
        c->SetReputationRewardDisabled(true);

        // NO SetDisableGravity(true) here, and the absence is the fix (Round D
        // / D3). Until 2026-09-08 every summon on this map set it, on the
        // theory that a creature with gravity would fall through the platforms
        // only the client draws. The operator's T2 report of 2026-09-08 - some
        // mobs hover at spawn and stand normally after a pull or a reset - is
        // that flag and nothing else: the hover IS the levitation, and the pull
        // is what removes it.
        //
        // The core drops the flag on its own, on the FIRST movement update.
        // None of these templates has a `creature_template_movement` row, so
        // CreatureMovementData's defaults apply (Creature.cpp:60-62) with
        // Flight = None, IsFlightAllowed() is false (CreatureData.h:141-144),
        // and Creature::UpdateMovementFlags takes its else branch
        // (Creature.cpp:3460, 3470) to call SetDisableGravity(false) on
        // anything levitating (Creature.cpp:3475-3476). The one opt-out,
        // CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE (Creature.cpp:3452), is set
        // on none of them (flags_extra = 0, measured). The path there is the
        // creature's first step: Unit::Update drives the spline (Unit.cpp:635
        // -> UpdateSplineMovement :695 -> UpdateSplinePosition :727) into
        // Creature::SetPosition (Creature.cpp:3287-3292), and
        // Map::CreatureRelocation refreshes the position data (Map.cpp:834 ->
        // Unit::ProcessPositionDataChanged Unit.cpp:4467-4470 ->
        // ProcessTerrainStatusUpdate :4473-4476). A respawn strips it too
        // (Creature.cpp:2004). So the flag only ever lived from the summon to
        // the first leg - visible as the hover, gone after the first move.
        //
        // Nothing falls without it. The server does not simulate creature
        // gravity: the only downward motion is MotionMaster::MoveFall
        // (MotionMaster.cpp:689), which this module never issues - the core's
        // callers are the corpse fall (Creature.cpp:1981-1986, gated on
        // IsFlying()/IsHovering()), a vehicle exit (Unit.cpp:15855), totems, a
        // SmartScript action and fly/levitate aura removal
        // (SpellAuraEffects.cpp:3446-3447) - and on this map it would bail out
        // anyway, because GetMapHeight answers INVALID_HEIGHT with no terrain
        // and no vmaps and MoveFall returns on that (MotionMaster.cpp:695-701).
        // Creature::Update (Creature.cpp:706) samples no ground height per
        // tick either: the only GetFloorZ() on this path sits inside
        // UpdateMovementFlags (Creature.cpp:3455), which Update does not call.
        // And UpdateAllowedPositionZ leaves Z alone without height data
        // (Object.cpp:1610, the `max_z > INVALID_HEIGHT` gate). The mob stands
        // at the floorZ this function was handed, flag or no flag.

        // The tag is what makes this creature a PDv2 mob for every other hook
        // in the module. GetDefault here (it creates), Get everywhere else (it
        // must not). Field by field rather than one assignment: DataMap::Base
        // declares a destructor, so copying a derived tag wholesale leans on an
        // implicitly generated operator the standard has deprecated.
        PDv2MobData* tag = c->CustomData.GetDefault<PDv2MobData>(PD_MOB_DATA_KEY);
        tag->role = proto.role;
        tag->casterSpellId = proto.casterSpellId;
        tag->roomIndex = proto.roomIndex;
        tag->counted = false;
        tag->isRunBoss = proto.isRunBoss;
        tag->affixMask = proto.affixMask;
        tag->splitDepth = proto.splitDepth;
        // Round B: everything the module spawns comes through here, the patrol
        // and the ambush included, so the four fields that say "this one is not
        // part of the run's arithmetic" are copied here and nowhere else.
        tag->countsForRun = proto.countsForRun;
        tag->isPatrol = proto.isPatrol;
        // Round D / D2: the beat's two ends on the leader, the leader on a
        // follower. Copied unconditionally like everything else here - the
        // caller decides which half of the pair is filled, and a mob that is
        // not a patrol carries the zeroes the struct's own defaults gave it.
        tag->patrolStartCellX = proto.patrolStartCellX;
        tag->patrolStartCellY = proto.patrolStartCellY;
        tag->patrolGoalCellX = proto.patrolGoalCellX;
        tag->patrolGoalCellY = proto.patrolGoalCellY;
        tag->patrolLeader = proto.patrolLeader;
        tag->patrolRank = proto.patrolRank;
        // Round E / D7: set by TickEvents on an event-wave attacker and by
        // SpawnRespawnCopies on an echo, read by the currency gate in
        // OnMobDied.
        tag->isExtra = proto.isExtra;
        // Round E / WP6: set by SpawnRespawnCopies, read by its own guard in
        // OnMobDied so an echo cannot echo. Copied here for the reason 1f66c74
        // paid for with isExtra - EVERY field a proto can carry is copied in
        // this block, or the spawner's intent is silently thrown away.
        tag->isRespawnCopy = proto.isRespawnCopy;

        // Before the affixes, never after: a Lil' Bro child is a TENTH of its
        // parent that a Big Boy bit then grows by half again, and reversing
        // these two would hand the child a full-size bar instead.
        if (baseHealthOverride)
        {
            SetDungeonHealth(c, baseHealthOverride);
        }

        if (tag->affixMask)
        {
            // The affixes, cast the way mod-dungeon-challenge casts them
            // (ApplyAffixToCreature: CastSpell on self, triggered,
            // DungeonChallenge.cpp:676-768) so a mob wearing one looks the same
            // in both dungeons. Which mobs are affixed came out of the SEEDED
            // draw, so nothing about it is stored: a rebuild of the same plan
            // re-rolls the identical set.
            for (AffixDef const& affix : _runAffixes)
            {
                c->CastSpell(c, affix.spellId, true);
            }

            // ...and the spawn-time teeth: Big Boy and Bigger Boy are x1.5 max
            // health each. This lands AFTER the difficulty HP scale, which
            // PDv2Scaling's OnCreatureSelectLevel already applied inside the
            // SummonCreature above - the same order that module uses, and the
            // reason both affixes together are x2.25 of a difficulty-scaled bar
            // rather than of the template's.
            ApplyAffixSpawnHealth(c, tag->affixMask);
        }

        _spawnedGuids.push_back(c->GetGUID());
        return c;
    }

    void PDv2InstanceScript::ReapplyAffixAuras(Creature* creature, uint16 affixMask) const
    {
        if (!creature || !affixMask)
        {
            return;
        }
        // The same triggered self-casts the spawn used. _runAffixes is frozen
        // at spawn and a carrier's mask is all-or-nothing against it, so
        // filtering by bit here is exact rather than approximate.
        for (AffixDef const& affix : _runAffixes)
        {
            if (HasAffix(affixMask, affix.id))
            {
                creature->CastSpell(creature, affix.spellId, true);
            }
        }
    }

    void PDv2InstanceScript::SplitOnDeath(Creature* parent, PDv2MobData const& parentTag,
                                          Unit* killer)
    {
        // 10 % of what the PARENT ended up with, so the halving compounds down
        // the generations exactly as it does in that module
        // (DungeonChallengeScripts.cpp:868).
        uint32 const childHealth = static_cast<uint32>(
            static_cast<double>(parent->GetMaxHealth()) * AFFIX_LIL_BRO_HEALTH_PCT);

        // A dead creature has usually lost its victim already; the killer is
        // the fallback that module uses too (DungeonChallengeScripts.cpp:870-873).
        Unit* target = parent->GetVictim();
        if (!target)
        {
            target = killer;
        }

        PDv2MobData proto;
        proto.role = parentTag.role;
        proto.casterSpellId = parentTag.casterSpellId;
        proto.roomIndex = parentTag.roomIndex;

        // The children inherit the WHOLE set, which is what that module does
        // (DungeonChallengeScripts.cpp:890 copies the parent's affix vector and
        // :901-902 re-applies every one of them). So a Big Boy's child is a big
        // child of a small corpse: SpawnTaggedMob writes the tenth first and
        // the x1.5 on top of it.
        proto.affixMask = parentTag.affixMask;
        proto.splitDepth = static_cast<uint8>(parentTag.splitDepth + 1);

        // Never a boss, whatever the parent was: `bossTotal` was counted once
        // at spawn and a child that could be killed for boss credit would let a
        // run finish twice over.
        proto.isRunBoss = false;

        // Round B: the children of an uncounted mob are uncounted too, or a
        // patroller with Lil' Bro would quietly hand the run two kills it was
        // never asked to earn. `isPatrol` stays false - a child inherits the
        // exemption, not the beat; it has no route and no goal cell.
        proto.countsForRun = parentTag.countsForRun;

        WalkGrid const* grid = GetWalkGrid();
        float const floorZ = sPDv2Mgr->GetConfig().floorZ;

        uint16 born = 0;
        for (uint8 i = 0; i < AFFIX_LIL_BRO_CHILDREN; ++i)
        {
            // That module's own offsets (DungeonChallengeScripts.cpp:877-878):
            // far enough apart to be two creatures, close enough to read as one
            // splitting.
            float const sign = (i == 0) ? 1.0f : -1.0f;
            float cx = parent->GetPositionX() + sign * LIL_BRO_OFFSET_X_YD;
            float cy = parent->GetPositionY() + sign * LIL_BRO_OFFSET_Y_YD;

            // A parent can die standing at a platform edge, and 2 yd past that
            // edge is the void - where a gravity-less child would hover for
            // ever, unreachable and uncountable. The walk grid is the only
            // thing on this server that knows where floor is (pd/02 §7), so it
            // decides: not floor, no offset.
            if (grid)
            {
                int gcx = 0, gcy = 0;
                WorldToCell(cx, cy, gcx, gcy);
                GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                if (!grid->At(cell.x, cell.y))
                {
                    cx = parent->GetPositionX();
                    cy = parent->GetPositionY();
                }
            }

            // Z comes from the config, not from the corpse: the floor plane is
            // where every other spawn stands, and a parent that was knocked
            // upward must not hand its children a hover.
            Creature* child = SpawnTaggedMob(parent->GetEntry(), proto, cx, cy, floorZ,
                                             childHealth);
            if (!child)
            {
                continue;
            }
            ++born;

            if (target)
            {
                if (CreatureAI* ai = child->AI())
                {
                    ai->AttackStart(target);
                }
            }
        }

        if (!born)
        {
            return;
        }

        // THE COUNTERS, AND THIS IS WHY THE CALLER RUNS US FIRST. OnMobDied has
        // not scored the parent yet, so raising the totals here means "killed"
        // never passes "total" (23/25 becomes 24/27, not 24/25) and the room's
        // live count never touches zero while children are standing in it -
        // which would otherwise count the room cleared and then count it again
        // when the children died.
        //
        // ...and only for a counted parent (Round B): OnMobDied will never
        // score an uncounted child, so raising the run total for one would
        // leave a HUD that can never reach its own denominator. Design
        // 2026-09-03 B4.3 - "_run.total excludes them".
        if (proto.countsForRun)
        {
            _run.total = static_cast<uint16>(_run.total + born);
            if (proto.roomIndex < _roomAlive.size())
            {
                _roomAlive[proto.roomIndex] = static_cast<uint16>(_roomAlive[proto.roomIndex] + born);
            }
        }
        MarkRunDirty();

        // One line per SPLIT, so one per kill of a splitting mob and one more
        // per kill of every child - the fastest way there is to fill a log
        // with a mechanic working exactly as designed (Round E / R3).
        if (PDv2Debug())
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} Lil' Bro depth {} split into {} - run total {}",
                     instance->GetInstanceId(), uint32(proto.splitDepth), uint32(born),
                     uint32(_run.total));
        }
    }

    void PDv2InstanceScript::SpawnRespawnCopies(Creature* corpse, PDv2MobData const& tag,
                                                Unit* killer)
    {
        // The talent belongs to a PLAYER, so a kill by a pet, a guardian or a
        // totem has to be walked back to its owner - the same walk-back
        // RollBonusLoot and InjectBossGear do, for the same reason. A kill by
        // nobody at all (a void zone, a fall, a despawn the AI reports as a
        // death) owes nothing and leaves here.
        Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player)
        {
            return;
        }

        // The node's RANK is the count - that is the whole talent - and the
        // conf ceiling is the operator's last word on it (PDv2Mgr.h says why
        // both exist). A player without the node reads 0 here, which is the
        // common case and the cheapest one: one walk of the dummy-aura list.
        uint32 const copies = std::min(TaggedAuraAmount(player, PD_TALENT_TAG_RESPAWN),
                                       sPDv2Mgr->GetConfig().respawnMaxCopies);
        if (!copies)
        {
            return;
        }

        // What an echo INHERITS is what it looks and fights like; what it does
        // not inherit is the run's arithmetic. Field by field rather than a
        // copy of `tag`, so a field added to PDv2MobData has to be decided on
        // here instead of arriving by accident.
        PDv2MobData proto;
        proto.role = tag.role;
        proto.casterSpellId = tag.casterSpellId;

        // The affixes come along, exactly as they do for a Lil' Bro child: the
        // echo is the same creature the player just fought, and one that shed
        // its affixes would be a different, easier fight wearing its face.
        proto.affixMask = tag.affixMask;

        // In no room and in no counter. The room this mob was planned for has
        // already been credited for it, and PD_ROOM_NONE is the value every
        // `roomIndex < _roomAlive.size()` guard in this file rejects on its own
        // - the tag's 0 default would have decremented room 0 instead.
        proto.roomIndex = PD_ROOM_NONE;
        proto.countsForRun = false;

        // Never a boss, whatever the corpse was. The call site's guard already
        // keeps a boss out; this says it a second time at the place where it
        // would matter if that guard were ever loosened, because a second
        // killable boss is a run that can be finished twice.
        proto.isRunBoss = false;

        // D7, both halves at once. isExtra is what pays the materials and puts
        // the currency behind V2.Loot.Currency.ExtraMobsDropCurrency;
        // isRespawnCopy is what stops the echo from echoing. splitDepth is
        // left at 0 DELIBERATELY rather than inherited: a Lil' Bro echo may
        // still split, and its children pay nothing exactly as they do today.
        //
        // The patrol block and the Damage Reduce cache keep the struct's own
        // defaults - an echo is born in combat and walks no beat, and the
        // cache is per-creature runtime state no proto ever carries.
        proto.isExtra = true;
        proto.isRespawnCopy = true;
        proto.splitDepth = 0;

        WalkGrid const* grid = GetWalkGrid();
        float const floorZ = sPDv2Mgr->GetConfig().floorZ;

        uint32 born = 0;
        for (uint32 i = 0; i < copies; ++i)
        {
            // SplitOnDeath's offsets and SplitOnDeath's veto, because this is
            // the same problem: two creatures out of one corpse, far enough
            // apart to be two bodies and close enough to read as the corpse
            // getting back up. Past the second they stack on the pair, which
            // no shipped rank reaches - the conf ceiling is what an operator
            // would have to raise to get there.
            float const sign = (i == 0) ? 1.0f : -1.0f;
            float cx = corpse->GetPositionX() + sign * LIL_BRO_OFFSET_X_YD;
            float cy = corpse->GetPositionY() + sign * LIL_BRO_OFFSET_Y_YD;

            // A mob can die standing at a platform edge, and 2 yd past that
            // edge is the void - where a gravity-less echo would hover for
            // ever, unreachable and permanently in combat with the player who
            // earned it. The walk grid is the only thing on this server that
            // knows where floor is (pd/02 §7), so it decides; the corpse's own
            // feet are the fallback, because the corpse is provably on floor.
            if (grid)
            {
                int gcx = 0, gcy = 0;
                WorldToCell(cx, cy, gcx, gcy);
                GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                if (!grid->At(cell.x, cell.y))
                {
                    cx = corpse->GetPositionX();
                    cy = corpse->GetPositionY();
                }
            }

            // Z from the config, never from the corpse: the floor plane is
            // where every other spawn on this map stands, and a mob that was
            // knocked upward before it died must not hand its echo a hover.
            // No baseHealthOverride either - an echo is the mob at full size,
            // which is exactly what makes the node worth its place at the far
            // end of the tree.
            Creature* echo = SpawnTaggedMob(corpse->GetEntry(), proto, cx, cy, floorZ);
            if (!echo)
            {
                continue;
            }
            ++born;

            // Straight at the player who earned it, and not at whatever the
            // corpse was last fighting: the talent is that player's, the echo
            // is that player's consequence, and a corpse has usually lost its
            // victim by the time JustDied runs anyway.
            if (CreatureAI* ai = echo->AI())
            {
                ai->AttackStart(player);
            }
        }

        // One line per echoing kill, gated like every other R3 diagnostic - the
        // one an operator arms for a single pull when somebody reports too many
        // echoes or none at all. No MarkRunDirty(): nothing the HUD shows moved.
        if (born && PDv2Debug())
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} respawn echo x{} (of {} owed) of entry {} for {}",
                     instance->GetInstanceId(), born, copies, corpse->GetEntry(),
                     player->GetName());
        }
    }

    void PDv2InstanceScript::SpawnFromPlan(BlockPlan const& plan)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2AccountState const account = sPDv2Mgr->GetAccountState(_accountId);
        int const dlvl = static_cast<int>(account.dlvl);

        // The run's numbers are FROZEN here and every later reader - the damage
        // hooks, the gold hook, the bonus roll - goes through the run state
        // rather than the live account row. A `.pdungeon v2 set` in the middle
        // of a run must not retune the mobs already standing in the dungeon;
        // the account row is what the NEXT run is built from.
        _run = PDv2RunState();
        _run.difficulty = static_cast<uint8>(GameClampDiff(account.cfgDifficulty));
        _run.lootMultX100 = static_cast<uint16>(
            GameLootMultX100(account.cfgDifficulty, account.cfgCasterPct));
        // Round E / L3. The material band's ceiling is the ACCOUNT's dungeon
        // level, and it is frozen here with the rest of them: finishing this
        // run grants dxp and can level the account on the way out, and a mob
        // killed before that must not pay a different band than the one killed
        // after it. Clamped into the byte the run state carries - dlvl is a
        // uint32 on the account row and 255 is far past any cap a conf can set.
        _run.dlvl = static_cast<uint8>(std::clamp(dlvl, 0, 255));

        // Rooms only, in plan order. A corridor is 8.3 yd wide, so anything
        // standing in one would be shoulder to shoulder with the walls; the
        // entrance stays empty so an arriving player is not already in combat.
        std::vector<PlacedBlock const*> roomBlocks;
        SpawnSelectInputs inputs;
        // Round B / B3 and Round C / C5: the per-room facts a barrier's
        // arithmetic and the respawn checkpoint need, taken in the same pass
        // and in the same order, so `roomIndex` means one thing in every
        // vector keyed by it. `roomBlocks` is discarded at the end of this
        // function - these are what survives it.
        _roomSegment.clear();
        _roomIsBoss.clear();
        _roomSpot.clear();
        _roomBX.clear();
        _roomBY.clear();
        _roomChain.clear();
        _checkpointChain = -1;
        _checkpointRoom = -1;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId < 0 || b.role == BlockRole::RoomEntrance)
            {
                continue;
            }

            // Round E / WP5. An event pocket is a Room with a roomId and
            // would otherwise be filled like any other, which would be wrong
            // four times over: it would draw a pack, take a dense roomIndex,
            // count toward roomsTotal on the HUD and add its trash to its
            // segment's barrier denominator - so the party would be asked to
            // clear an optional side room before the gate to the boss opened.
            // The pocket's whole content is the pilgrim and the wave he
            // brings, both of which SpawnEventRooms puts there.
            if (b.isEvent)
            {
                continue;
            }

            RoomRequest room;
            room.roomIndex = static_cast<int>(roomBlocks.size());
            room.isBoss = b.role == BlockRole::RoomBoss;
            inputs.rooms.push_back(room);
            roomBlocks.push_back(&b);
            // SegmentOf answers for all three room kinds: a spine room's own
            // segment, a pocket's host segment, a loop room's run segment.
            _roomSegment.push_back(SegmentOf(plan, b));
            _roomIsBoss.push_back(b.role == BlockRole::RoomBoss);
            // Round C / C5. chainIndex, not SegmentOf: "furthest" is measured
            // along the spine, and a pocket carries -1 there on purpose - it
            // is off the chain and can never be the checkpoint.
            _roomChain.push_back(b.chainIndex);
            _roomBX.push_back(b.bx);
            _roomBY.push_back(b.by);
            RoomSpot spot;
            {
                double const mid = PD_BLOCK_SIZE_YD / 2.0;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, mid, mid, spot.x, spot.y, spot.z);
                // Grid-vetoed like a spawn point, and NOT as a formality.
                //
                // `mid` is half a block, i.e. exactly 4 * PD_CELL_SIZE_YD, so
                // this point is not the centre OF a cell - it is the corner
                // where (3,3), (3,4), (4,3) and (4,4) meet, and which of the
                // four WorldToCell names is decided by the float BlockToWorld
                // narrows to (SpawnPatrols carries the measurement, and the
                // same one-cell reading applies here).
                //
                // That distinction costs nothing on the masks the kit ships,
                // because all four answer the same way in every chunk that can
                // be a checkpoint. Measured over the shipped chunk-meta walk
                // masks: in all 60 room_boss chunks all four cells are floor -
                // so the CHECKPOINT itself always stands on the arena floor,
                // even the 33 yd platform's - while in 15 of the 90 room
                // chunks all four are VOID: theme 2's alt-1 room, 13001-13015,
                // carries a 2x2 hole in the middle of the block, and it is
                // exactly this quad. There the veto is load-bearing whichever
                // of the four the float names, and it finds floor on ring 1.
                if (WalkGrid const* grid = GetWalkGrid())
                {
                    int gcx = 0, gcy = 0;
                    WorldToCell(spot.x, spot.y, gcx, gcy);
                    GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                    // `snapped`, not the plan's `near`: <minwindef.h> defines
                    // `near` as an empty macro, so that name compiles to
                    // nothing on this platform.
                    GridPoint snapped;
                    if (!grid->At(cell.x, cell.y) &&
                        NearestWalkable(*grid, cell.x, cell.y, SPAWN_FALLBACK_SNAP_CELLS,
                                        snapped))
                    {
                        grid->GlobalFromLocalCell(snapped, gcx, gcy);
                        double wx = 0.0, wy = 0.0;
                        CellCentreToWorld(gcx, gcy, wx, wy);
                        spot.x = static_cast<float>(wx);
                        spot.y = static_cast<float>(wy);
                    }
                }
            }
            _roomSpot.push_back(spot);
        }

        inputs.spawnsPerRoom = cfg.spawnsPerRoom;
        inputs.bossRoomAdds = cfg.bossRoomAdds;
        inputs.affixPct = cfg.affixPct;
        inputs.casterPct = account.cfgCasterPct;
        inputs.bandMin = account.cfgBandMin;
        // No creature-type cap any more: every trash slot draws from the whole
        // unlocked pool (PDv2PackMgr.h says why). dlvl still decides which
        // packs are unlocked, which is the variety lever that remains.
        inputs.unlockedDlvl = dlvl;

        // The draw is seeded from the PLAN's seed, so the same dungeon holds
        // the same creatures every time it is regenerated. That is where the
        // determinism boundary runs: layout and spawn SELECTION are pinned,
        // loot rolls are not (see the bonus roll in OnMobDied).
        std::vector<RoomSpawns> spawns;
        if (!sPDv2PackMgr->SelectSpawns(plan.effectiveSeed, inputs, spawns))
        {
            // Exactly the degradation this had before packs existed: an
            // unapplied mod_pdungeon_packs.sql leaves a dungeon with
            // placeholder trash in it rather than an empty one, and says so.
            LOG_WARN(PD_LOG, "PDv2: no creature pack could be drawn for instance {} - "
                             "falling back to the placeholder creature",
                     instance->GetInstanceId());

            spawns.clear();
            spawns.reserve(inputs.rooms.size());
            for (RoomRequest const& room : inputs.rooms)
            {
                // Same room SHAPES as a real draw (boss room = 1 + adds,
                // normal room = spawnsPerRoom), so a degraded dungeon is the
                // real one with placeholder art rather than a different layout.
                int const count = room.isBoss
                                      ? 1 + (cfg.bossRoomAdds > 0 ? cfg.bossRoomAdds : 0)
                                      : (cfg.spawnsPerRoom > 0 ? cfg.spawnsPerRoom : 1);

                RoomSpawns fallback;
                fallback.roomIndex = room.roomIndex;
                // packId left at its default (0): there is no pack table to
                // theme against on this path, so every placeholder reads as
                // unthemed, the same way the boss slot does.
                fallback.picks.assign(static_cast<size_t>(count),
                                      SpawnPick{ PLACEHOLDER_CREATURE, PACK_ROLE_MELEE, 0 });
                spawns.push_back(fallback);
            }
        }

        _roomAlive.assign(roomBlocks.size(), 0);
        // Round E / R2 (spec D13). ORDINARY rooms, not every room the dungeon
        // built: the boss halls have their own counter on the HUD line
        // (bossKilled/bossTotal), and counting them twice is exactly what made
        // the panel say "14 rooms" while the HUD said "0/15 rooms". `rooms` on
        // the dial is now the ordinary count, the entrance and the boss halls
        // come on top of it, and this is the number that has to agree with it.
        // roomBlocks still holds every room - _roomAlive, the spawn slices and
        // every roomIndex are keyed by that list and must stay complete.
        _run.roomsTotal = static_cast<uint8>(
            std::count(_roomIsBoss.begin(), _roomIsBoss.end(), false));

        // Round E / D8. The room factor, frozen beside the loot multiplier and
        // for the same reason - every currency roll of every kill is scaled by
        // it, and the dial must not move under a run that is being walked.
        //
        // ORDINARY rooms, read straight off _run.roomsTotal: since Round E /
        // R2 that field IS the ordinary count (assigned above from
        // _roomIsBoss), which is exactly what the factor asks for - how much
        // TRASH the run is worth walking, D8's point being that a one-room run
        // must not pay what a ten-room run pays. One source of truth: when the
        // two were counted separately they could drift apart silently.
        // The factor is stored UNSIGNED, so the one thing that has to happen
        // on the way in is the floor GameRoomFactorX100 deliberately does not
        // apply (its header says why): a negative RoomsBonusPctPerRoom is not
        // a legal conf value, but a negative product cast into a uint16 would
        // come out enormous and GameChanceBp would then clamp every tier to a
        // certainty - a typo that pays MORE is the wrong way to fail.
        _run.roomFactorX100 = static_cast<uint16>(
            std::max(0, GameRoomFactorX100(static_cast<int>(_run.roomsTotal),
                                           cfg.lootRoomsBaseline,
                                           cfg.lootRoomsBonusPctPerRoom)));

        // The affix set for THIS run's difficulty, resolved once: it is the
        // same list for every affixed mob in the dungeon (that is how
        // mod-dungeon-challenge assigns them - all of the unlocked ones, not
        // one drawn per mob), and the run's difficulty is frozen above, so
        // there is nothing to recompute per creature.
        //
        // Kept on the instance because a Lil' Bro split has to hand the same
        // set to a child that is born minutes later, and re-reading it then
        // would read a dial the player may have moved since.
        _runAffixes = sPDv2PackMgr->AffixesForDifficulty(static_cast<int>(_run.difficulty));
        _runAffixMask = 0;
        for (AffixDef const& affix : _runAffixes)
        {
            _runAffixMask |= AffixBit(affix.id);
        }

        uint32 spawned = 0;
        uint32 affixedMobs = 0;
        double const mid = PD_BLOCK_SIZE_YD / 2.0;

        // The placement this used to do for EVERY pick, kept verbatim for the
        // chunk that publishes no typed anchors: a small fixed pattern around
        // the block centre. Deliberately NOT random - the same plan must
        // produce the same dungeon, and an unseeded draw here would break that
        // quietly. PlanSpawnPoints reproduces the identical ring for the picks
        // an anchored room runs out of anchors for.
        auto circlePoints = [mid](size_t count) -> std::vector<PDv2SpawnPoint>
        {
            std::vector<PDv2SpawnPoint> out;
            out.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                double const angle = 2.0 * 3.14159265358979 *
                                     static_cast<double>(i) / static_cast<double>(count);
                out.push_back({ mid + std::cos(angle) * SPAWN_SPREAD_YD,
                                mid + std::sin(angle) * SPAWN_SPREAD_YD });
            }
            return out;
        };

        // The instance's own walk grid vetoes a point that is not floor, the
        // same way SplitOnDeath vetoes a child's offset - and one warning per
        // CHUNK, not per creature, because a chunk whose anchors disagree with
        // its walk mask would otherwise write one line per mob per run.
        WalkGrid const* grid = GetWalkGrid();
        std::set<int> vetoedChunks;

        for (size_t r = 0; r < roomBlocks.size() && r < spawns.size(); ++r)
        {
            PlacedBlock const& b = *roomBlocks[r];
            bool const isBossRoom = b.role == BlockRole::RoomBoss;
            std::vector<SpawnPick> const& picks = spawns[r].picks;
            int const count = static_cast<int>(picks.size());

            // Round B / B2: WHERE this room's picks stand. The roles go in in
            // pick order (PACK_ROLE_* and SPAWN_ROLE_* are the same three
            // values), and one point comes back per pick, so `points[i]`
            // belongs to `picks[i]` and the boss - pick 0 of a boss room - gets
            // the kit's boss anchor, which is the arena centre. The draw above
            // is untouched: PlanSpawnPoints reads anchors and roles only, it
            // draws nothing and it cannot move a pick.
            //
            // The "same three values" above is the whole mapping, so it is
            // asserted rather than asserted-in-prose: SPAWN_ROLE_* are plain
            // ints in generator/PDv2SpawnAnchors.h, PACK_ROLE_* an enum in
            // generator/PDv2PackDraw.h, and neither header includes the other
            // (both are engine-free and must stay independent). This is the
            // translation unit that sees both, so this is where the mirror can
            // be made self-checking.
            static_assert(SPAWN_ROLE_MELEE == PACK_ROLE_MELEE, "spawn/pack melee role drifted");
            static_assert(SPAWN_ROLE_CASTER == PACK_ROLE_CASTER, "spawn/pack caster role drifted");
            static_assert(SPAWN_ROLE_BOSS == PACK_ROLE_BOSS, "spawn/pack boss role drifted");

            std::vector<int> roles;
            roles.reserve(picks.size());
            for (SpawnPick const& pick : picks)
            {
                roles.push_back(static_cast<int>(pick.role));
            }

            RoomAnchors const* anchors = sPDv2Mgr->RoomAnchorsFor(b.chunkId);
            std::vector<PDv2SpawnPoint> const points =
                anchors ? PlanSpawnPoints(*anchors, isBossRoom, roles)
                        : circlePoints(roles.size());

            for (int i = 0; i < count && i < static_cast<int>(points.size()); ++i)
            {
                PDv2SpawnPoint const& point = points[static_cast<size_t>(i)];

                float x = 0.0f, y = 0.0f, z = 0.0f;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, point.u, point.v, x, y, z);

                // An anchor is a kit constant and the walk grid is what this
                // instance actually composed, so the two can disagree - a kit
                // published against an older mask, or an overflow ring point
                // that falls outside a 33 yd room's platform. Gravity is off on
                // this map, so a mob seated off the floor hovers over the void
                // for ever: unreachable, unkillable, and holding the room's
                // counter open. The entry anchor is provably floor (it is the
                // cell every walk into the room arrives on), so that is where
                // a vetoed pick goes; a chunk without one falls back to the
                // block centre, which is walkable in every room variant the
                // kit ships.
                //
                // Why the circle path never gets here: LoadChunkMeta writes
                // _walkMasks[chunkId] and _chunkRoomAnchors[chunkId] from the
                // SAME row in the same iteration, so a chunk with no anchors
                // has no walk mask either, BuildWalkGrid fails on it and `grid`
                // is null - the veto and the overflow circle cannot meet.
                //
                // That same-row property is also the ONLY reason the entry
                // anchor is floor at all: the kit derives it as a walkable cell
                // centre of that very mask, i.e. the argument is about the
                // kit's mask, not about the grid this instance composed. A
                // hand-edited chunk_meta row breaks the tie, and then the
                // fallback would stack every vetoed pick of the room on a point
                // in the void the veto exists to prevent. So the fallback is
                // grid-checked too and snapped to the nearest walkable cell;
                // when the grid is null or nothing walkable is within reach,
                // the un-snapped point stands, exactly as before.
                if (grid)
                {
                    int gcx = 0, gcy = 0;
                    WorldToCell(x, y, gcx, gcy);
                    GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                    if (!grid->At(cell.x, cell.y))
                    {
                        bool const onEntry = anchors && anchors->hasEntry;
                        sPDv2Mgr->BlockToWorld(b.bx, b.by,
                                               onEntry ? anchors->entry.u : mid,
                                               onEntry ? anchors->entry.v : mid,
                                               x, y, z);

                        int fcx = 0, fcy = 0;
                        WorldToCell(x, y, fcx, fcy);
                        GridPoint const fallbackCell = grid->LocalFromGlobalCell(fcx, fcy);
                        GridPoint snapped;
                        if (!grid->At(fallbackCell.x, fallbackCell.y) &&
                            NearestWalkable(*grid, fallbackCell.x, fallbackCell.y,
                                            SPAWN_FALLBACK_SNAP_CELLS, snapped))
                        {
                            int scx = 0, scy = 0;
                            grid->GlobalFromLocalCell(snapped, scx, scy);
                            double wx = 0.0, wy = 0.0;
                            CellCentreToWorld(scx, scy, wx, wy);
                            x = static_cast<float>(wx);
                            y = static_cast<float>(wy);
                        }

                        if (vetoedChunks.insert(b.chunkId).second)
                        {
                            LOG_WARN(PD_LOG, "PDv2: instance {} chunk {} planned a spawn point "
                                             "the walk grid calls void - that chunk's vetoed "
                                             "picks stand on its {} instead",
                                     instance->GetInstanceId(), b.chunkId,
                                     onEntry ? "entry anchor" : "block centre");
                        }
                    }
                }

                PDv2MobData proto;
                proto.role = picks[i].role;
                proto.casterSpellId = picks[i].casterSpellId;
                proto.roomIndex = static_cast<uint32>(r);

                // A boss room's FIRST pick is its boss - PDv2PackMgr.h states
                // that contract ("every boss room gets exactly one role-2
                // entry, drawn fresh"), and it holds even when the packs have
                // no role-2 member and a trash stand-in takes the slot. Keying
                // completion on the slot rather than on the drawn role is what
                // keeps that data state finishable.
                proto.isRunBoss = isBossRoom && i == 0;

                // MEMBERSHIP FIRST, AURA SECOND. The mask is what every affix
                // hook in the module reads; the spells are only the look.
                // Writing it here - the one site that knows which mobs the
                // seeded draw picked - is what lets a hook gate on a bit test
                // instead of on HasAura, which a dispel or a purge could
                // quietly take away (PDv2Affixes.h).
                proto.affixMask = picks[i].affixed ? _runAffixMask : uint16(0);

                // Round E / WP8, operator finding 6: the BOSS looks at the door
                // his party will come through, and nobody else does. Trash is a
                // pack standing around a room and reads as one from any angle;
                // a boss with his back to the entrance reads as a bug, because
                // he is the one creature the room is arranged around. Measured
                // from his OWN anchor - the arena centre, after the veto has
                // had its say - so the angle describes where he actually ends
                // up rather than where the kit planned to put him.
                float orientation = 0.0f;
                if (proto.isRunBoss)
                {
                    orientation = FacingTowardDoorway(plan, b, x, y);
                    if (PDv2Debug())
                    {
                        LOG_INFO(PD_LOG, "PDv2: instance {} boss of room {} (block {}, {}, "
                                         "chain {}) faces {:.3f} rad toward its doorway",
                                 instance->GetInstanceId(), uint32(r), b.bx, b.by,
                                 b.chainIndex, orientation);
                    }
                }

                if (SpawnTaggedMob(picks[i].entry, proto, x, y, z, 0, orientation))
                {
                    if (proto.isRunBoss)
                    {
                        ++_run.bossTotal;
                    }
                    if (proto.affixMask)
                    {
                        ++affixedMobs;
                    }
                    ++_roomAlive[r];
                    ++spawned;
                }
            }
        }
        _run.total = static_cast<uint16>(spawned);

        // Round B / B3: the barrier's DENOMINATOR, frozen here. _roomAlive is
        // the live count and a Lil' Bro split inflates it mid-run, so the copy
        // - not the vector - is what a threshold is ever measured against.
        _roomPlanned = _roomAlive;
        size_t const segments = static_cast<size_t>(std::max(1, plan.config.bossRooms)) + 1;
        _segmentPlanned.assign(segments, 0);
        _segmentKilled.assign(segments, 0);
        for (size_t r = 0; r < _roomPlanned.size(); ++r)
        {
            // The boss room's own pack stands BEHIND its barrier and is left
            // out, or a segment whose only room is its boss could never open
            // (design 2026-09-03 §B3.1). Segment 0 is the entrance: no barrier.
            if (r >= _roomSegment.size() || _roomIsBoss[r] || _roomSegment[r] < 1)
            {
                continue;
            }
            size_t const seg = static_cast<size_t>(_roomSegment[r]);
            if (seg < _segmentPlanned.size())
            {
                _segmentPlanned[seg] += _roomPlanned[r];
            }
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} on map {} spawned {} creature(s) in {} room(s) "
                         "({} boss) from a {}-block plan, difficulty {} lootMult {}, "
                         "{} mob(s) wearing {} affix(es)",
                 instance->GetInstanceId(), instance->GetId(), spawned,
                 uint32(_run.roomsTotal), uint32(_run.bossTotal),
                 uint32(plan.blocks.size()), uint32(_run.difficulty),
                 uint32(_run.lootMultX100), affixedMobs, uint32(_runAffixes.size()));
    }

    void PDv2InstanceScript::EvaluateBarrier(int segment)
    {
        // Segment 0 is the entrance and has no barrier; anything below that is
        // a corridor's -1 and never reaches here from OnMobDied's guard.
        if (segment < 1 || _barriers.empty())
        {
            return;
        }

        size_t const seg = static_cast<size_t>(segment);
        uint32 const planned = seg < _segmentPlanned.size() ? _segmentPlanned[seg] : 0;
        uint32 const killed = seg < _segmentKilled.size() ? _segmentKilled[seg] : 0;
        uint32 const pct = static_cast<uint32>(sPDv2Mgr->GetConfig().barrierPct);

        for (Barrier& barrier : _barriers)
        {
            if (barrier.segment != segment || barrier.open)
            {
                continue;
            }
            if (planned == 0)
            {
                // The denominator excludes the boss room's own pack, so a
                // segment whose ONLY room is its boss plans nothing - the
                // single-boss-segment softlock (design §B3.1). It opens on
                // sight rather than never.
                OpenBarrier(barrier, "its segment plans no trash in front of the boss");
            }
            else if (killed * 100 >= planned * pct)
            {
                // Integers on purpose: the same comparison the hint's own
                // ceiling is derived from, so the two can never disagree about
                // whether one more kill is needed.
                OpenBarrier(barrier, "the segment's kill threshold was met");
            }
        }
    }

    void PDv2InstanceScript::OpenBarrier(Barrier& barrier, char const* why)
    {
        if (barrier.open)
        {
            return;
        }
        barrier.open = true;

        // Delete(), not a door state: type 5 GENERIC has no open state to set,
        // and the whole point of the choice is that its collision is the one
        // shape measured to stop a player on this map. The portcullis simply
        // stops existing.
        if (GameObject* go = instance->GetGameObject(barrier.guid))
        {
            go->Delete();
        }
        _decorGuids.erase(std::remove(_decorGuids.begin(), _decorGuids.end(), barrier.guid),
                          _decorGuids.end());
        barrier.guid.Clear();

        // ...and the creatures get their lane back. Nothing is re-pathed: the
        // AI re-decides inside 500 ms on its own.
        SetCellsWalkable(barrier.cells, true);

        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            sPDv2UILink->SendNotice(it->GetSource(), "The barrier to the boss falls.");
        }

        size_t const seg = static_cast<size_t>(barrier.segment);
        uint32 const planned = seg < _segmentPlanned.size() ? _segmentPlanned[seg] : 0;
        uint32 const killed = seg < _segmentKilled.size() ? _segmentKilled[seg] : 0;
        LOG_INFO(PD_LOG, "PDv2: instance {} opened the barrier of segment {} - {} "
                         "({}/{} planned kills, threshold {}%)",
                 instance->GetInstanceId(), barrier.segment, why, killed, planned,
                 sPDv2Mgr->GetConfig().barrierPct);
    }

    void PDv2InstanceScript::HintBarriers()
    {
        if (_barriers.empty())
        {
            return;
        }

        uint32 const pct = static_cast<uint32>(sPDv2Mgr->GetConfig().barrierPct);
        Map::PlayerList const& players = instance->GetPlayers();
        for (Barrier& barrier : _barriers)
        {
            if (barrier.open || barrier.hinted)
            {
                continue;
            }

            size_t const seg = static_cast<size_t>(barrier.segment);
            uint32 const planned = seg < _segmentPlanned.size() ? _segmentPlanned[seg] : 0;
            uint32 const killed = seg < _segmentKilled.size() ? _segmentKilled[seg] : 0;

            // The ceiling of planned x pct / 100 is the kill count that first
            // satisfies EvaluateBarrier's >=, so this number is what the
            // player actually still owes - never one less, never one more. It
            // is floored at 1: a closed barrier by definition still wants a
            // kill, and "0 more must fall" in front of a wall is a bug report.
            uint32 const needAll = (planned * pct + 99) / 100;
            uint32 const needed = needAll > killed ? needAll - killed : 1;

            for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
            {
                Player* player = it->GetSource();
                if (!player || !player->IsInWorld())
                {
                    continue;
                }
                // 2D: the dungeon is one floor plane, and a Z term would only
                // add the height of a jump.
                float const dx = player->GetPositionX() - barrier.x;
                float const dy = player->GetPositionY() - barrier.y;
                if (dx * dx + dy * dy > BARRIER_HINT_YD * BARRIER_HINT_YD)
                {
                    continue;
                }
                sPDv2UILink->SendNotice(player, Acore::StringFormat(
                    "The barrier holds - {} more of this segment's foes must fall.", needed));
                // Everyone standing there is told, and then never again for
                // this barrier: it is a signpost, not an alarm.
                barrier.hinted = true;
            }
        }
    }

    bool PDv2InstanceScript::NextClosedBarrier(uint32& planned, uint32& killed, uint32& pct) const
    {
        // Zeroed up front, so a caller that ignores the answer still puts the
        // wire's own "no gate" value on the wire (PDv2UILink::SendRunTick).
        planned = 0;
        killed = 0;
        pct = 0;

        // SpawnBarriers fills _barriers in segment order and an opened barrier
        // STAYS in it with `open` set, so the lowest sealed segment is a scan.
        // Done by comparison rather than by taking the first !open entry
        // because that ordering is a property of the spawner, not a contract
        // this getter is entitled to lean on.
        Barrier const* next = nullptr;
        for (Barrier const& barrier : _barriers)
        {
            if (barrier.open)
            {
                continue;
            }
            if (!next || barrier.segment < next->segment)
            {
                next = &barrier;
            }
        }

        if (!next)
        {
            return false;
        }

        size_t const seg = static_cast<size_t>(next->segment);
        planned = seg < _segmentPlanned.size() ? _segmentPlanned[seg] : 0;
        killed = seg < _segmentKilled.size() ? _segmentKilled[seg] : 0;

        // Clamped, and not defensively: _segmentPlanned counts _roomPlanned,
        // frozen at spawn, while _segmentKilled counts corpses - and a Lil'
        // Bro split makes more corpses than the draw planned. Without the
        // clamp that segment's gate line would read past 100 %.
        pct = planned ? std::min<uint32>(100, killed * 100 / planned) : 100;
        return true;
    }

    int PDv2InstanceScript::RoomIndexAt(float x, float y) const
    {
        // Non-negative first, exactly as TickAmbushes checks it: global cells
        // are non-negative inside the field and integer division truncates
        // TOWARDS ZERO, so a position outside it would divide to a block it is
        // not in - and here that would name somebody else's room.
        int gcx = 0, gcy = 0;
        WorldToCell(x, y, gcx, gcy);
        if (gcx < 0 || gcy < 0)
        {
            return -1;
        }

        int const bx = gcx / PD_CELLS_PER_BLOCK;
        int const by = gcy / PD_CELLS_PER_BLOCK;
        for (size_t r = 0; r < _roomBX.size() && r < _roomBY.size(); ++r)
        {
            if (_roomBX[r] == bx && _roomBY[r] == by)
            {
                return static_cast<int>(r);
            }
        }
        return -1;
    }

    bool PDv2InstanceScript::GateFieldsFor(Player const* player, uint32& planned,
                                           uint32& killed, uint32& pct, bool& open) const
    {
        // Zeroed up front the way NextClosedBarrier zeroes its three, and for
        // the same reason: a caller that ignores the answer still puts the
        // wire's own "no gate" on the wire.
        planned = 0;
        killed = 0;
        pct = 0;
        open = false;

        int const room = player ? RoomIndexAt(player->GetPositionX(),
                                              player->GetPositionY())
                                : -1;

        // A boss hall is skipped ON PURPOSE and not as a bound check: its pack
        // stands BEHIND the barrier and is deliberately out of the segment's
        // denominator (design 2026-09-03 §B3.1, the single-boss-segment
        // softlock), so the honest answer for a player standing in one is the
        // run's next gate rather than a number that pretends their kills there
        // count towards anything.
        if (room >= 0 && static_cast<size_t>(room) < _roomSegment.size() &&
            static_cast<size_t>(room) < _roomIsBoss.size() && !_roomIsBoss[room])
        {
            int const segment = _roomSegment[room];
            if (segment >= 1 && static_cast<size_t>(segment) < _segmentPlanned.size())
            {
                size_t const seg = static_cast<size_t>(segment);
                planned = _segmentPlanned[seg];
                killed = seg < _segmentKilled.size() ? _segmentKilled[seg] : 0;
                // The same clamp NextClosedBarrier carries, and the same
                // argument: a Lil' Bro split makes more corpses than the frozen
                // denominator planned, so the raw ratio can run past 100 %.
                pct = planned ? std::min<uint32>(100, killed * 100 / planned) : 100;

                // "Open" includes "never had one". SpawnBarriers skips a
                // segment whose entry run it cannot name (a boss on the
                // entrance, a fork), and a segment nothing seals is a segment
                // the party may walk out of - which is what the HUD has to say.
                open = true;
                for (Barrier const& barrier : _barriers)
                {
                    if (barrier.segment == segment)
                    {
                        open = barrier.open;
                        break;
                    }
                }
                return true;
            }
        }

        // Not in a room this can answer for: a corridor, a boss hall, an event
        // pocket, the entrance's segment 0. The run's next sealed gate is the
        // useful thing to show there, and it is sealed by definition - `open`
        // stays false.
        return NextClosedBarrier(planned, killed, pct);
    }

    void PDv2InstanceScript::ClearedRoomBlocks(std::vector<std::pair<int, int>>& out) const
    {
        out.clear();
        for (size_t r = 0; r < _roomAlive.size(); ++r)
        {
            // _roomBX/_roomBY are filled in the same pass and the same order
            // as _roomAlive, so the bound can only bite on a half-built
            // instance - which is exactly when a caller must get nothing back
            // rather than a block coordinate that means something else.
            if (_roomAlive[r] != 0 || r >= _roomBX.size() || r >= _roomBY.size())
            {
                continue;
            }
            out.push_back(std::make_pair(_roomBX[r], _roomBY[r]));
        }
    }

    void PDv2InstanceScript::SetCellsWalkable(std::vector<GridPoint> const& cells, bool walkable)
    {
        if (!_gridReady)
        {
            return;
        }
        for (GridPoint const& p : cells)
        {
            if (!_grid.InBounds(p.x, p.y))
            {
                continue;
            }
            _grid.cells[static_cast<size_t>(p.y) * _grid.width + p.x] = walkable ? 1 : 0;
        }
    }

    void PDv2InstanceScript::SpawnDecor(BlockPlan const& plan, std::vector<Position>& outPositions)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        if (!cfg.decorEnable)
        {
            return;
        }

        std::vector<DecorRule> const& rules = sPDv2Mgr->DecorRules();
        if (rules.empty())
        {
            // Not an error: mod_pdungeon_decor.sql is simply not applied yet,
            // and an unlit dungeon is still a dungeon. LoadDecorRules said so
            // once at startup and there is no reason to say it per instance.
            return;
        }

        // The PLAN's seed, the same number the terrain and the spawn draw came
        // from. BuildDecorPlan derives its own stream from it, so the props
        // follow a re-roll exactly as the walls do and re-entering the same
        // dungeon finds them where they were.
        std::vector<DecorSpot> const spots = BuildDecorPlan(
            plan,
            [](int chunkId) { return sPDv2Mgr->WalkMaskFor(chunkId); },
            [](int chunkId) { return sPDv2Mgr->AnchorsFor(chunkId); },
            rules, plan.effectiveSeed);

        uint32 placed = 0;
        for (DecorSpot const& spot : spots)
        {
            // Z is the kit's floor plane, which BlockToWorld already supplies -
            // the same plane the creatures stand on, and the only floor the
            // server knows about on this map.
            float x = 0.0f, y = 0.0f, z = 0.0f;
            sPDv2Mgr->BlockToWorld(spot.bx, spot.by, spot.u, spot.v, x, y, z);

            // respawnTime 0: no despawn timer. A prop is furniture and lives
            // exactly as long as the instance does; DespawnAll owns its end.
            GameObject* go = instance->SummonGameObject(
                static_cast<uint32>(spot.goEntry), x, y, z,
                static_cast<float>(spot.orientation), 0.0f, 0.0f, 0.0f, 0.0f, 0);
            if (!go)
            {
                LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon decor {} "
                                  "(missing gameobject_template?)",
                          instance->GetInstanceId(), spot.goEntry);
                continue;
            }

            _decorGuids.push_back(go->GetGUID());
            outPositions.emplace_back(x, y, z);
            ++placed;
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} placed {} prop(s) from {} decor rule(s) "
                         "over a {}-block plan",
                 instance->GetInstanceId(), placed, uint32(rules.size()),
                 uint32(plan.blocks.size()));
    }

    void PDv2InstanceScript::SpawnKitProps(BlockPlan const& plan)
    {
        // Gated like SpawnDecor: props are LOOK, and V2.Decor.Enable is the
        // one switch for everything optical. (The dead-end chest below stays
        // ungated - a reward, not a look.)
        if (!sPDv2Mgr->GetConfig().decorEnable)
        {
            return;
        }

        uint32 placed = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            std::vector<KitProp> const* props = sPDv2Mgr->PropsFor(b.chunkId);
            if (!props)
            {
                continue;
            }
            for (KitProp const& prop : *props)
            {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, prop.u, prop.v, x, y, z);
                // The kit measured the terrain under this prop; the floor
                // plane BlockToWorld answers is only right on WALK cells.
                z += static_cast<float>(prop.z);
                GameObject* go = instance->SummonGameObject(
                    static_cast<uint32>(prop.goEntry), x, y, z,
                    static_cast<float>(prop.o), 0.0f, 0.0f, 0.0f, 0.0f, 0);
                if (!go)
                {
                    LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon kit prop {} "
                                      "(mod_pdungeon_prop_displays.sql not applied?)",
                              instance->GetInstanceId(), prop.goEntry);
                    continue;
                }
                _decorGuids.push_back(go->GetGUID());
                ++placed;
            }
        }
        if (placed)
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} placed {} kit prop(s) from the "
                             "chunk-meta anchors", instance->GetInstanceId(), placed);
        }
    }

    void PDv2InstanceScript::BuildPropCells()
    {
        _propCells.clear();
        if (!_gridReady || _decorGuids.empty())
        {
            // No grid means no indexing scheme to hand the planner, and no
            // props means an all-zero vector the planner would pay for on
            // every step. PropCells() answers nullptr for both, which
            // FindPatrolPath reads as "no prop costs anywhere".
            return;
        }

        // ONE BYTE PER GRID CELL, the walk grid's own indexing. The planner
        // reads it with the same (y * width + x) it reads `cells` with, which
        // is why it is sized from the grid rather than from the plan.
        _propCells.assign(_grid.cells.size(), 0);

        uint32 marked = 0;
        for (ObjectGuid const& guid : _decorGuids)
        {
            GameObject* go = instance->GetGameObject(guid);
            if (!go)
            {
                // Summoned and already gone (a dead-end chest that was looted
                // and deleted, a barrier this run opened on the spot). Not a
                // finding: the object is not standing in the corridor any
                // more, so the cell it used to hold is free.
                continue;
            }
            int gcx = 0, gcy = 0;
            WorldToCell(go->GetPositionX(), go->GetPositionY(), gcx, gcy);
            GridPoint const cell = _grid.LocalFromGlobalCell(gcx, gcy);
            if (!_grid.InBounds(cell.x, cell.y))
            {
                // A prop outside the grid's bounding box cannot be in anyone's
                // way, and writing it would be an out-of-range store.
                continue;
            }
            size_t const idx = static_cast<size_t>(cell.y) * _grid.width + cell.x;
            if (!_propCells[idx])
            {
                ++marked;
            }
            // Not a counter: several props share a cell often enough (a torch
            // pair, a prop on a decor spot), and the planner asks a yes/no
            // question. One flag per cell, however many objects stand on it.
            _propCells[idx] = 1;
        }

        LOG_DEBUG(PD_LOG, "PDv2: instance {} marked {} prop cell(s) of {} for the "
                          "patrol planner", instance->GetInstanceId(), marked,
                  uint32(_propCells.size()));
    }

    void PDv2InstanceScript::SpawnCritters(BlockPlan const& plan,
                                            std::vector<Position> const& decorPositions)
    {
        // Gated like SpawnDecor and SpawnKitProps: a critter is LOOK, not
        // content (it fails both the AI-binder and the scaling gate above),
        // and V2.Decor.Enable is the one switch every optical thing answers to.
        if (!sPDv2Mgr->GetConfig().decorEnable)
        {
            return;
        }

        std::vector<CritterRule> const& rules = sPDv2Mgr->CritterRules();
        if (rules.empty())
        {
            // Not an error, same reasoning as SpawnDecor's empty-rules return:
            // mod_pdungeon_critter_rules.sql simply is not applied yet.
            return;
        }

        // The PLAN's seed, exactly like SpawnDecor: BuildCritterPlan derives
        // its own stream from it (PD_CRITTER_SEED_MIX), so critters follow a
        // re-roll the way the props and the terrain do.
        std::vector<CritterSpot> const spots = BuildCritterPlan(
            plan,
            [](int chunkId) { return sPDv2Mgr->WalkMaskFor(chunkId); },
            rules, plan.effectiveSeed);

        uint32 placed = 0;
        uint32 skippedForClearance = 0;
        for (CritterSpot const& spot : spots)
        {
            // Z is the kit's floor plane, the same one every other spawn on
            // this map stands on - there is no other floor the server knows.
            float x = 0.0f, y = 0.0f, z = 0.0f;
            sPDv2Mgr->BlockToWorld(spot.bx, spot.by, spot.u, spot.v, x, y, z);

            // A scatter decor rule and this rule draw from the SAME candidate
            // cells, on two RNG streams that have never heard of each other,
            // and BuildCritterPlan carries no spacing gate of its own. Skip
            // rather than summon when a prop already claimed this ground -
            // both plans are deterministic, so which critters this drops is a
            // property of the seed, not a coin flip.
            bool blockedByProp = false;
            for (Position const& decorPos : decorPositions)
            {
                if (decorPos.GetExactDist2d(x, y) < CRITTER_DECOR_CLEAR_YD)
                {
                    blockedByProp = true;
                    break;
                }
            }
            if (blockedByProp)
            {
                ++skippedForClearance;
                continue;
            }

            Creature* c = instance->SummonCreature(
                static_cast<uint32>(spot.creatureEntry),
                Position(x, y, z, static_cast<float>(spot.orientation)));
            if (!c)
            {
                continue;
            }

            // The home position every dungeon spawn gets, and - since Round D
            // / D3 - no gravity flag beside it: the core strips that on the
            // first movement update anyway, until then it shows as a hover,
            // and a critter stands at floorZ without it (SpawnTaggedMob cites
            // the core lines).
            c->SetHomePosition(x, y, z, static_cast<float>(spot.orientation));

            // A critter IS killable - all four shipped rules point at unit_flags
            // 0 templates (32428, 23086, 2110, 26525: selectable, attackable,
            // not IMMUNE_TO_PC), and an AoE that clips one kills it - so it
            // takes the module's no-kill-reputation policy too. SpawnTaggedMob
            // carries the reasoning and the core cites. None of the four has a
            // creature_onkill_reputation row today (measured 2026-09-09); the
            // switch is set regardless, because pdungeon_critter_rules is
            // OPERATOR data and the policy has to hold for a row this module
            // has never seen.
            c->SetReputationRewardDisabled(true);

            // NO PDv2MobData tag, deliberately. The tag is the module's own
            // definition of "this is a dungeon mob": without it, OnMobDied
            // refuses this creature, no room counter moves, no affix touches
            // it, and the damage hooks leave it alone. IsCritter() above is
            // the gate that keeps it off the AI binder and the level/HP
            // scaling in the first place, both of which run before any tag
            // could exist.
            _critterGuids.push_back(c->GetGUID());
            ++placed;
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} placed {} critter(s) of {} planned from "
                         "{} critter rule(s) ({} skipped for prop clearance)",
                 instance->GetInstanceId(), placed, uint32(spots.size()),
                 uint32(rules.size()), skippedForClearance);
    }

    void PDv2InstanceScript::SpawnDeadEndChests(BlockPlan const& plan)
    {
        // One Shifting Cache (GO_CHEST, native loot table) per dead-end stub,
        // on its junction square - the stub's whole reason to exist - and per
        // loop room (B0b), on the kit's chest anchor: a loop is a detour off
        // the straight run, so it has to pay for the walk the same way a stub
        // does. NOT gated on Decor.Enable: the chest is a reward, not a look,
        // and the dungeon must not lose loot to a cosmetics switch. Torn down
        // by the same DespawnAll as everything else this instance stands up.
        uint32 stubs = 0;
        uint32 loops = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            // Round E / WP5. An event pocket is a dead-END room but not a
            // dead-end STUB and not a loop, so the two tests below already
            // miss it (role Room, detourOf -1 - both proved by
            // ValidateBlockPlan's "an event pocket carries the wrong role or
            // fields"). Stated as its own line anyway, because the free cache
            // and the event's own won-cache would then stand in one small
            // room and the reward for holding the line would be the reward
            // for walking in - and a later planner change that gave an event
            // pocket either field would open exactly that hole silently.
            if (b.isEvent)
            {
                continue;
            }

            // Exclusive by construction: the planner validates that a loop
            // room is a Room block (PDBlockPlan.cpp, "a loop room carries the
            // wrong role or fields"), never a corridor.
            bool const isStub = b.role == BlockRole::CorridorDeadEnd;
            bool const isLoopRoom = b.detourOf >= 0;
            if (!isStub && !isLoopRoom)
            {
                continue;
            }

            // The kit pins the stub's chest anchor to the block centre (the
            // junction square), so the position is a constant of the format
            // rather than a lookup that could go stale. A loop room is a room
            // chunk and publishes a real chest anchor instead - one the kit
            // put clear of the walls and of the socket track.
            double u = PD_BLOCK_SIZE_YD / 2.0;
            double v = PD_BLOCK_SIZE_YD / 2.0;
            if (isLoopRoom)
            {
                RoomAnchors const* anchors = sPDv2Mgr->RoomAnchorsFor(b.chunkId);
                if (anchors && anchors->hasChest)
                {
                    u = anchors->chest.u;
                    v = anchors->chest.v;
                }
                else
                {
                    // The block centre is walkable in every room variant, so
                    // the reward is still reachable - it just stands on the
                    // track instead of beside it.
                    LOG_WARN(PD_LOG, "PDv2: instance {} chunk {} publishes no chest anchor - "
                                     "the loop room's cache stands on the block centre",
                             instance->GetInstanceId(), b.chunkId);
                }
            }

            float x = 0.0f, y = 0.0f, z = 0.0f;
            sPDv2Mgr->BlockToWorld(b.bx, b.by, u, v, x, y, z);
            // -pi/2: "90 Grad nach rechts" (T2 2026-09-08); WoW orientation is
            // counter-clockwise. The four zeros after it are the quaternion, and
            // an all-zero quaternion is not a facing: Map::SummonGameObject hands
            // this angle and that quat to GameObject::Create, which relocates the
            // object with the angle and then calls SetWorldRotation, which rebuilds
            // the rotation from the orientation about +Z whenever the quat's
            // magnitude is zero. So this literal alone decides the facing.
            GameObject* go = instance->SummonGameObject(
                GO_CHEST, x, y, z, 4.712389f, 0.0f, 0.0f, 0.0f, 0.0f, 0);
            if (!go)
            {
                LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon a cache "
                                  "(missing gameobject_template {}?)",
                          instance->GetInstanceId(), uint32(GO_CHEST));
                continue;
            }
            _decorGuids.push_back(go->GetGUID());
            if (isStub)
            {
                ++stubs;
            }
            else
            {
                ++loops;
            }
        }
        if (stubs || loops)
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} placed {} dead-end chest(s) and "
                             "{} loop-room chest(s)",
                     instance->GetInstanceId(), stubs, loops);
        }
    }

    void PDv2InstanceScript::SpawnBarriers(BlockPlan const& plan)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        if (!_gridReady)
        {
            // Not a reason to skip the portcullis: the GameObject still stops
            // the PLAYER, which is the mechanic the run is measured on. Only
            // the creature half is lost here - and a dungeon whose walk grid
            // failed to build has no creature pathing to lose in the first
            // place, because that grid IS the navigation on this map.
            LOG_WARN(PD_LOG, "PDv2: instance {} has no walk grid - its barriers will hold "
                             "players back but not creatures",
                     instance->GetInstanceId());
        }

        // The two cells a block's doorway occupies on the edge a socket bit
        // names, appended as walk-grid cells. The TABLE itself lives in the
        // engine-free planner (LaneCellsForSocket) so `pdblock` can pin it -
        // B3-B5 Task 2 review, Important 1; all that is left here is the one
        // translation an engine has to do, (row, col) -> LocalFromGlobalCell(
        // x = col, y = row).
        auto laneCells = [this](PlacedBlock const& block, unsigned edge,
                                std::vector<GridPoint>& out)
        {
            int cells[2][2] = { { 0, 0 }, { 0, 0 } };
            LaneCellsForSocket(edge, cells);
            for (int i = 0; i < 2; ++i)
            {
                out.push_back(_grid.LocalFromGlobalCell(
                    block.bx * PD_CELLS_PER_BLOCK + cells[i][1],
                    block.by * PD_CELLS_PER_BLOCK + cells[i][0]));
            }
        };

        int const chainLen = ChainLength(plan);
        int const bossRooms = std::max(1, plan.config.bossRooms);
        uint32 placed = 0;
        for (int k = 1; k <= bossRooms; ++k)
        {
            // The same walk the validator proved the spine with, so the run a
            // barrier seals and the run the plan is valid for are one run.
            // `run` comes back in walking order, so its LAST block is the
            // corridor that touches the boss room's doorway.
            int const bossChain = BossChainIndex(chainLen, plan.config.bossRooms, k);
            std::vector<size_t> run;
            unsigned const bit = SpineRunInto(plan, bossChain, &run);
            if (!bit || run.empty())
            {
                // A boss sitting on the entrance itself (chain 0), or a join
                // that is not one straight run. Neither has a single doorway
                // to seal, so that segment stays open - a missing barrier is a
                // shortcut, never a softlock.
                LOG_WARN(PD_LOG, "PDv2: instance {} found no single entry run into boss {} "
                                 "(chain room {}) - segment {} gets no barrier",
                         instance->GetInstanceId(), k, bossChain, k);
                continue;
            }
            if (bit != SOCKET_N && bit != SOCKET_E && bit != SOCKET_S && bit != SOCKET_W)
            {
                // SpineRunInto only ever answers with one of the four bits, so
                // this is a contract check rather than a branch a plan can
                // reach - but it has to be made HERE: LaneCellsForSocket and
                // OppositeSocket read anything else as SOCKET_E, and a
                // portcullis on the wrong edge is worse than none.
                LOG_WARN(PD_LOG, "PDv2: instance {} could not name the lane cells of "
                                 "socket {} into chain room {} - segment {} gets no barrier",
                         instance->GetInstanceId(), bit, bossChain, k);
                continue;
            }

            PlacedBlock const* boss = nullptr;
            for (PlacedBlock const& b : plan.blocks)
            {
                // Last match, the way SpineRunInto picks it. chainIndex is set
                // on spine rooms only (pockets carry branchOf, loop rooms
                // detourOf), so there is exactly one of these anyway.
                if (b.chainIndex == bossChain)
                {
                    boss = &b;
                }
            }
            if (!boss)
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} has no chain room {} to bar - "
                                 "segment {} gets no barrier",
                         instance->GetInstanceId(), bossChain, k);
                continue;
            }
            PlacedBlock const& neighbour = plan.blocks[run.back()];

            // BOTH sides of the edge. Creatures snap to a cell within two of
            // their own, so sealing only the boss block's half would leave the
            // corridor cell next to it as a legal step across the doorway.
            std::vector<GridPoint> cells;
            laneCells(*boss, bit, cells);
            laneCells(neighbour, OppositeSocket(bit), cells);

            // One cell INSIDE the boss block, on that edge, at the lane
            // centre - the doorway's own square. u runs along the row axis and
            // v along the column axis, the same reading the kit's typed
            // anchors are decoded with.
            double const nearEdge = PD_CELL_SIZE_YD / 2.0;                      // 4.1667
            double const farEdge = PD_BLOCK_SIZE_YD - PD_CELL_SIZE_YD / 2.0;    // 62.5
            double const lane = PD_BLOCK_SIZE_YD / 2.0;                         // 33.3333
            double u = lane;
            double v = lane;
            // Two conf keys, not two constants: which radian value stands the
            // model across the lane depends on how the m2 is authored, and the
            // operator calibrates it in game with `.reload config`.
            float orientation = cfg.barrierOrientNS;
            switch (bit)
            {
                case SOCKET_N:  u = nearEdge;   orientation = cfg.barrierOrientNS;  break;
                case SOCKET_S:  u = farEdge;    orientation = cfg.barrierOrientNS;  break;
                case SOCKET_W:  v = nearEdge;   orientation = cfg.barrierOrientEW;  break;
                case SOCKET_E:  v = farEdge;    orientation = cfg.barrierOrientEW;  break;
                default:        break;      // unreachable: `bit` was checked against the four sockets above
            }

            float x = 0.0f, y = 0.0f, z = 0.0f;
            sPDv2Mgr->BlockToWorld(boss->bx, boss->by, u, v, x, y, z);
            GameObject* go = instance->SummonGameObject(GO_BARRIER, x, y, z, orientation,
                                                        0.0f, 0.0f, 0.0f, 0.0f, 0);
            if (!go)
            {
                LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon the barrier "
                                  "(missing gameobject_template {}?)",
                          instance->GetInstanceId(), uint32(GO_BARRIER));
                continue;
            }
            // The decor list owns the object, so one teardown deletes
            // everything this instance stood up; _barriers only remembers what
            // the object MEANS.
            _decorGuids.push_back(go->GetGUID());

            Barrier barrier;
            barrier.segment = k;
            barrier.guid = go->GetGUID();
            barrier.cells = cells;
            barrier.x = x;
            barrier.y = y;
            _barriers.push_back(barrier);
            SetCellsWalkable(cells, false);
            ++placed;

            // Asked once, right here. A segment whose rooms in front of the
            // boss plan no trash at all (design 2026-09-03 §B3.1) has to open
            // before anyone walks up to it: no kill will ever come to ask
            // again, and a sealed lane with nothing behind it to clear is the
            // softlock this whole denominator is shaped to avoid.
            EvaluateBarrier(k);
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} placed {} barrier(s) for {} boss segment(s)",
                 instance->GetInstanceId(), placed, uint32(bossRooms));
    }

    void PDv2InstanceScript::SpawnPatrols(BlockPlan const& plan)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2AccountState const account = sPDv2Mgr->GetAccountState(_accountId);

        // EnsureWalkGrid ran before this (see the run set-up), so this is the
        // same grid the patrol's own AI will walk on - which is the point: the
        // beat is planned, and every spawn point vetoed, by exactly the thing
        // that has to accept them.
        WalkGrid const* grid = GetWalkGrid();

        // HOW LONG THE FILE IS, one number for the whole dungeon. The dial is
        // frozen per run (SpawnFromPlan wrote _run.difficulty long before
        // this), so every corridor of a run gets the same size and an operator
        // can read the dial off any one patrol. Two live keys, two steps: 1
        // below Size2Diff, 2 from it, 3 from Size3Diff. Size3Diff <= Size2Diff
        // is not refused - it only makes the middle band empty, which is a
        // legitimate thing for an operator to type.
        int const diff = static_cast<int>(_run.difficulty);
        int const size = 1 + (diff >= cfg.patrolSize2Diff ? 1 : 0) +
                             (diff >= cfg.patrolSize3Diff ? 1 : 0);

        // The FIRST of the two doorway cells a socket names, as a GLOBAL cell.
        // The same translation SpawnBarriers makes - LaneCellsForSocket answers
        // (row, col) and the cell frame's x axis is the COLUMN one - but global
        // rather than grid-local, because that is the form the spawn tag
        // carries and the AI reads back through LocalFromGlobalCell.
        auto laneCell = [](PlacedBlock const& block, unsigned edge, int& gcx, int& gcy)
        {
            int cells[2][2] = { { 0, 0 }, { 0, 0 } };
            LaneCellsForSocket(edge, cells);
            gcx = block.bx * PD_CELLS_PER_BLOCK + cells[0][1];
            gcy = block.by * PD_CELLS_PER_BLOCK + cells[0][0];
        };

        int const chainLen = ChainLength(plan);
        uint32 patrols = 0;
        uint32 members = 0;
        uint32 runs = 0;
        // EVERY corridor between two rooms, not one per boss segment (Round D /
        // D2). A dungeon with four chain rooms has three corridors and gets
        // three patrols, whatever its boss count is - the operator asked for
        // "ein pat zwischen jedem raum", and a segment boundary is not a
        // corridor.
        for (int i = 1; i < chainLen; ++i)
        {
            // THE BEAT, DERIVED FROM THE RUN AND NOTHING ELSE
            //
            //      room i-1 |###|###|###| room i
            //               ^A            ^B
            //               front     back
            //
            // SpineRunInto answers `run` in WALKING order (room i-1 -> room i)
            // and `bit`, the socket on ROOM i's OWN edge that the run arrives
            // through. So:
            //
            //   B (goal)  = the corridor's half of that same doorway, i.e.
            //               LaneCellsForSocket(OppositeSocket(bit)) on
            //               run.back() - exactly the pair SpawnBarriers seals
            //               alongside room i's half.
            //   A (start) = the doorway of run.front() that faces room i-1.
            //               SpineRunInto names no socket for that end, so it is
            //               read off the STEP between those two blocks: the run
            //               walked from room i-1 into run.front(), so the two
            //               are neighbours, and bx grows EAST while by grows
            //               SOUTH (PDBlockPlan.cpp's StepFor is the same table).
            //
            // A one-block run has run.front() == run.back(), so A and B are
            // that single block's two doorways - which is what design §D2.1
            // asks for, without a special case.
            std::vector<size_t> run;
            unsigned const bit = SpineRunInto(plan, i, &run);
            if (!bit || run.empty())
            {
                // Two rooms joined directly, or a join that is not one straight
                // run. Neither is a corridor to patrol, and neither is an
                // error: it is the same refusal SpawnBarriers makes on the same
                // walk, for the same reason.
                LOG_WARN(PD_LOG, "PDv2: instance {} found no single corridor run into chain "
                                 "room {} - that corridor gets no patrol",
                         instance->GetInstanceId(), i);
                continue;
            }
            if (bit != SOCKET_N && bit != SOCKET_E && bit != SOCKET_S && bit != SOCKET_W)
            {
                // A contract check rather than a branch a plan can reach, made
                // HERE for the reason SpawnBarriers makes it: LaneCellsForSocket
                // and OppositeSocket read anything else as SOCKET_E, and a beat
                // that ends on the wrong edge is a patrol walking into a wall.
                LOG_WARN(PD_LOG, "PDv2: instance {} could not name the lane cells of socket {} "
                                 "into chain room {} - that corridor gets no patrol",
                         instance->GetInstanceId(), bit, i);
                continue;
            }
            ++runs;

            PlacedBlock const* before = nullptr;
            for (PlacedBlock const& b : plan.blocks)
            {
                // chainIndex is set on spine rooms only (pockets carry
                // branchOf, loop rooms detourOf), so this can never catch a
                // corridor; last match, the way SpineRunInto picks it.
                if (b.chainIndex == i - 1)
                {
                    before = &b;
                }
            }
            if (!before)
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} has no chain room {} to start the beat at - "
                                 "the corridor into chain room {} gets no patrol",
                         instance->GetInstanceId(), i - 1, i);
                continue;
            }

            PlacedBlock const& firstBlock = plan.blocks[run.front()];
            PlacedBlock const& lastBlock = plan.blocks[run.back()];

            // The step from run.front() to room i-1, as a socket bit. One of
            // the four by construction (RunFromSocket walks block by block, so
            // the two are neighbours), and the else is the contract check.
            int const dbx = before->bx - firstBlock.bx;
            int const dby = before->by - firstBlock.by;
            unsigned startBit = 0;
            if (dbx == 0 && dby == -1)
            {
                startBit = SOCKET_N;
            }
            else if (dbx == 0 && dby == 1)
            {
                startBit = SOCKET_S;
            }
            else if (dbx == -1 && dby == 0)
            {
                startBit = SOCKET_W;
            }
            else if (dbx == 1 && dby == 0)
            {
                startBit = SOCKET_E;
            }
            if (!startBit)
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} found chain room {} at ({},{}) not adjacent "
                                 "to its run's first block ({},{}) - the corridor into chain "
                                 "room {} gets no patrol",
                         instance->GetInstanceId(), i - 1, before->bx, before->by,
                         firstBlock.bx, firstBlock.by, i);
                continue;
            }

            int startCellX = 0, startCellY = 0;
            int goalCellX = 0, goalCellY = 0;
            laneCell(firstBlock, startBit, startCellX, startCellY);
            laneCell(lastBlock, OppositeSocket(bit), goalCellX, goalCellY);

            // THE BEAT, PLANNED ONCE HERE - and only for the file's spawn
            // POSITIONS. The leader plans its own on its first idle tick, with
            // the same planner over the same grid and the same prop map, so the
            // two agree by construction instead of by carrying a route through
            // the spawn tag; and it has to, because the veto below may have
            // moved it off this cell.
            //
            // BOTH ends are snapped, and the goal end is the one that needs it:
            // SpawnBarriers ran before this, and a boss corridor's portcullis
            // has already taken the goal's own lane cell OUT of the grid
            // (SetCellsWalkable(false) on both halves of that doorway). The
            // snap answers the first walkable cell of the ring around it, which
            // inside a sealed corridor is the lane one step back - so the beat
            // ends AT the closed gate instead of failing to plan at all. The
            // TAG still carries the true doorway cell, which is what lets the
            // AI's own plan reach the doorway once the barrier falls.
            std::vector<GridPoint> beat;
            int spawnCellX = startCellX;
            int spawnCellY = startCellY;
            if (grid)
            {
                GridPoint const rawStart = grid->LocalFromGlobalCell(startCellX, startCellY);
                GridPoint const rawGoal = grid->LocalFromGlobalCell(goalCellX, goalCellY);
                GridPoint from{ 0, 0 };
                GridPoint to{ 0, 0 };
                if (NearestWalkable(*grid, rawStart.x, rawStart.y,
                                    SPAWN_FALLBACK_SNAP_CELLS, from) &&
                    NearestWalkable(*grid, rawGoal.x, rawGoal.y,
                                    SPAWN_FALLBACK_SNAP_CELLS, to))
                {
                    grid->GlobalFromLocalCell(from, spawnCellX, spawnCellY);
                    // NOT merged, and it must not be. A merge (MergeClearPoints
                    // since the D2 follow-up) is for the AI, which walks legs;
                    // this wants the CELL CHAIN, because "follower k stands k
                    // cells behind the leader" is the formation and a merged
                    // list has no cell k. The waypoints the leader will walk
                    // are a SUBSET of these cells and every one of them is
                    // placed on its own clear point below, so the file still
                    // stands where the beat will run.
                    //
                    // Round D / D2: the SAME two passes the leader's own plan
                    // makes (PDv2CreatureAI.cpp, UpdatePatrol) - the shipped
                    // cost first, then minClearQ 0 - because the whole point of
                    // planning here is that the file is stood up on the cells
                    // the AI will later walk. A fallback on one side only would
                    // put the file on a beat the leader never plans.
                    if (!FindPatrolPath(*grid, from, to, PropCells(), beat))
                    {
                        PatrolCost loose;
                        loose.minClearQ = 0;
                        if (!FindPatrolPath(*grid, from, to, PropCells(), beat, loose))
                        {
                            beat.clear();
                            // Named rather than silent (Task 3 review M7): a
                            // file stacked on one square with nothing in the
                            // log is the evidence gap the debug key exists to
                            // close. The leader recovers on its first idle tick
                            // and MoveFollow unpiles the tail, so this is a
                            // cosmetic degradation and not a broken run.
                            LOG_WARN(PD_LOG, "PDv2: instance {} could not plan the beat of the "
                                             "corridor into chain room {} - its file spawns "
                                             "stacked on the doorway cell",
                                     instance->GetInstanceId(), i);
                        }
                    }
                }
                else
                {
                    LOG_WARN(PD_LOG, "PDv2: instance {} found no floor within {} cells of the "
                                     "beat's ends for the corridor into chain room {} - its "
                                     "patrol stands on the doorway cell unvetoed",
                             instance->GetInstanceId(), SPAWN_FALLBACK_SNAP_CELLS, i);
                }
            }

            // ONE DRAW PER CORRIDOR, on the patrol's OWN stream, shaped like a
            // single room with no boss: the picks come off the same pools, the
            // same band and the same unlock as the dungeon's trash, because in
            // a module with no rank and no elite pool an "elite" IS a trash mob
            // with a bigger bar (design 2026-09-03 §B4.2). `size` picks in one
            // call rather than `size` calls, so the file's members are drawn
            // from one stream and the seed says what the whole patrol is.
            SpawnSelectInputs in;
            RoomRequest room;
            room.roomIndex = 0;
            room.isBoss = false;
            in.rooms.push_back(room);
            in.spawnsPerRoom = size;
            in.bossRoomAdds = 0;
            // Melee only. A caster plants itself at range the moment it pulls,
            // and a corridor sentry that never closes is not a patrol.
            in.casterPct = 0;
            // Copied from the room draw for the SHAPE of the stream, not for
            // its result: the draw rolls `affixed` per trash pick either way,
            // and the flag is deliberately dropped below - §B4.2 gives the
            // patrol no affix, and proto.affixMask staying 0 is what says so.
            in.affixPct = cfg.affixPct;
            in.bandMin = account.cfgBandMin;
            in.unlockedDlvl = static_cast<int>(account.dlvl);

            std::vector<RoomSpawns> out;
            uint32 const seed = plan.effectiveSeed ^ PD_PATROL_SEED_MIX ^
                                (static_cast<uint32>(i) * PD_SEGMENT_SEED_STEP);
            std::vector<uint32> entries;
            if (sPDv2PackMgr->SelectSpawns(seed, in, out) && !out.empty())
            {
                for (SpawnPick const& pick : out[0].picks)
                {
                    entries.push_back(pick.entry);
                }
            }
            // The same degradation the room draw and the ambush take when the
            // pack SQL was never applied: placeholder mammoths rather than an
            // empty corridor. It also covers a draw that came back short.
            while (entries.size() < static_cast<size_t>(size))
            {
                entries.push_back(PLACEHOLDER_CREATURE);
            }

            ObjectGuid leaderGuid;
            for (int k = 0; k < size; ++k)
            {
                // WHERE this member stands. The leader takes the beat's first
                // cell - the vetoed doorway lane cell - and follower k the k-th
                // cell along the beat, so the file is already strung out down
                // the lane on the first frame of the run instead of piling up
                // on one square and sorting itself out afterwards (design
                // §D2.4). A beat shorter than the file - a one-block corridor,
                // or a sealed one - clamps to its last cell, which stacks the
                // tail for a second; MoveFollow unpicks that on the first tick.
                //
                // Round D / D2: on the cell's CLEAR POINT, not its centre. The
                // file is meant to be standing in the middle of the visible
                // passage on the very first frame a player sees it, and on a
                // city straight the two are up to 4 yd apart - a member seated
                // on the centre would spawn inside a house and then walk out of
                // it on its first leg, which is exactly the sight this round
                // exists to remove. The no-grid and empty-beat fallbacks keep
                // the cell centre: with no grid there is no layer to read.
                double wx = 0.0, wy = 0.0;
                if (grid && !beat.empty())
                {
                    size_t const idx = std::min(static_cast<size_t>(k), beat.size() - 1);
                    PatrolPointToWorld(*grid, beat[idx], wx, wy);
                }
                else
                {
                    CellCentreToWorld(spawnCellX, spawnCellY, wx, wy);
                }

                PDv2MobData proto;
                proto.role = PACK_ROLE_MELEE;
                // In no room and in no counter: a patrol is risk on the road,
                // not progress (design §B4.3). PD_ROOM_NONE rather than the
                // tag's 0 default is what keeps OnMobDied from decrementing
                // room 0.
                proto.roomIndex = PD_ROOM_NONE;
                proto.countsForRun = false;
                proto.isPatrol = true;
                if (k == 0)
                {
                    // GLOBAL cells, never this grid's local ones: the AI reads
                    // the tag through LocalFromGlobalCell, and the grid origin
                    // is a property of the layout rather than of the creature
                    // that walks over it.
                    proto.patrolStartCellX = startCellX;
                    proto.patrolStartCellY = startCellY;
                    proto.patrolGoalCellX = goalCellX;
                    proto.patrolGoalCellY = goalCellY;
                }
                else
                {
                    proto.patrolLeader = leaderGuid;
                    proto.patrolRank = static_cast<uint8>(k);
                }

                Creature* c = SpawnTaggedMob(entries[static_cast<size_t>(k)], proto,
                                             static_cast<float>(wx), static_cast<float>(wy),
                                             cfg.floorZ);
                if (!c)
                {
                    LOG_WARN(PD_LOG, "PDv2: instance {} could not summon creature {} as "
                                     "member {} of the patrol in the corridor into chain "
                                     "room {}",
                             instance->GetInstanceId(), entries[static_cast<size_t>(k)], k, i);
                    if (k == 0)
                    {
                        // No leader, no file. A follower whose tag names an
                        // empty leader would hold its ground for the rest of
                        // the run, which is a mob standing in a corridor for no
                        // reason - so the corridor gets nothing instead.
                        break;
                    }
                    continue;
                }
                if (k == 0)
                {
                    leaderGuid = c->GetGUID();
                    ++patrols;
                }

                // A MULTIPLIER, which is exactly why it cannot go through
                // baseHealthOverride: that argument is an ABSOLUTE number the
                // caller has to know in advance (it is how a Lil' Bro child
                // gets its tenth), and the number this one multiplies - what
                // this run's difficulty scale already made of the template -
                // does not exist until SummonCreature has run PDv2Scaling's
                // OnCreatureSelectLevel (PDv2Scaling.cpp:232-263; gated on the
                // MAP, not on the tag, so it has fired by the time
                // SpawnTaggedMob returns). Every member of the file is
                // therefore a multiple of what this run's trash actually is,
                // never of the row.
                //
                // 64-bit product on purpose. The multiplier is clamped from
                // below (>= 100) and not from above, so a conf typo of 100000
                // would wrap a big bar into a small one in 32 bits - the
                // opposite of what the key is for.
                uint64 const scaled = static_cast<uint64>(c->GetMaxHealth()) *
                                      static_cast<uint64>(cfg.patrolHealthMultPct) / 100;
                uint64 const capped = std::min<uint64>(
                    scaled, static_cast<uint64>(std::numeric_limits<uint32>::max()));
                SetDungeonHealth(c, static_cast<uint32>(capped));
                c->SetFullHealth();
                // Out of combat it walks; JustEngagedWith puts it back on run
                // speed the moment it pulls. A follower inherits this from its
                // leader anyway (MoveFollow's inheritWalkState, default true),
                // but it also has to be true before the first follow tick.
                c->SetWalk(true);
                ++members;
            }
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} placed {} patrol(s), {} creature(s) for {} "
                         "corridor run(s)",
                 instance->GetInstanceId(), patrols, members, runs);
    }

    void PDv2InstanceScript::SpawnAmbushPlan(BlockPlan const& plan)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2AccountState const account = sPDv2Mgr->GetAccountState(_accountId);

        // WHICH corridors, decided entirely in the engine-free planner. The
        // chance is read LIVE (design 2026-09-03 §B5.1): an operator turning
        // V2.Ambush.Chance re-arms the next run rather than re-rolling a layout
        // an account already owns, which is why it is not a plan input.
        std::vector<AmbushSpot> const spots =
            BuildAmbushPlan(plan, cfg.ambushChancePct, plan.effectiveSeed);

        double const mid = PD_BLOCK_SIZE_YD / 2.0;
        for (AmbushSpot const& spot : spots)
        {
            if (spot.blockIndex >= plan.blocks.size())
            {
                // BuildAmbushPlan indexes the plan it was handed, so this is a
                // contract check rather than a branch a plan can reach - but an
                // out-of-range read here would be a crash, not a missing trap.
                LOG_WARN(PD_LOG, "PDv2: instance {} got an ambush spot outside the plan "
                                 "(block {} of {}) - segment {} gets no ambush",
                         instance->GetInstanceId(), uint32(spot.blockIndex),
                         uint32(plan.blocks.size()), spot.segment);
                continue;
            }

            Ambush ambush;
            ambush.spot = spot;
            // The corridor's OWN sockets are its axis: a straight N|S piece
            // runs north-south whatever else is around it, and FireAmbush reads
            // nothing else to decide which way to place the mobs.
            ambush.socketMask = plan.blocks[spot.blockIndex].socketMask;
            sPDv2Mgr->BlockToWorld(spot.bx, spot.by, mid, mid, ambush.x, ambush.y, ambush.z);

            // ONE synthetic room's worth of trash, drawn here and stored - not
            // at the moment the trap fires. Two reasons, and the second is the
            // load-bearing one: the draw is seeded, so a spot that is rolled at
            // build time springs the same creatures every time this seed is
            // entered; and the firing tick is the one place in the run where
            // the player is already stunned and the server has no business
            // doing anything it could have done minutes earlier.
            SpawnSelectInputs in;
            RoomRequest room;
            room.roomIndex = 0;
            room.isBoss = false;
            in.rooms.push_back(room);
            in.spawnsPerRoom = cfg.ambushMobs;
            in.bossRoomAdds = 0;
            // Everything else exactly as SpawnFromPlan fills it, so an ambush
            // draws from the same pools, the same band and the same unlock as
            // the dungeon's own trash. affixPct is copied for the SHAPE of the
            // stream only: the draw rolls `affixed` per pick either way, and
            // §B5.3 gives the ambush no affix - proto.affixMask staying 0 below
            // is what says so.
            in.casterPct = account.cfgCasterPct;
            in.affixPct = cfg.affixPct;
            in.bandMin = account.cfgBandMin;
            in.unlockedDlvl = static_cast<int>(account.dlvl);

            std::vector<RoomSpawns> out;
            uint32 const seed = plan.effectiveSeed ^ PD_AMBUSH_SEED_MIX ^
                                (static_cast<uint32>(spot.segment) * PD_SEGMENT_SEED_STEP);
            if (sPDv2PackMgr->SelectSpawns(seed, in, out) && !out.empty() &&
                !out[0].picks.empty())
            {
                ambush.picks = out[0].picks;
            }
            else if (cfg.ambushMobs > 0)
            {
                // The same degradation the room draw takes when the pack SQL
                // was never applied: placeholder mammoths rather than a trap
                // that stuns and then does nothing.
                ambush.picks.assign(static_cast<size_t>(cfg.ambushMobs),
                                    SpawnPick{ PLACEHOLDER_CREATURE, PACK_ROLE_MELEE, 0 });
            }

            _ambushes.push_back(ambush);

            // Per spot, not just a count. The Round B log printed only how
            // many corridors were armed, and when the operator reported "no
            // ambush" that line could not say whether the trap was in the
            // corridor he walked or two segments away - the plan had to be
            // regenerated offline to find out (research
            // c-research-ambush-trigger.md, closing note). The block is what
            // the trigger now tests, so the block is what this prints.
            LOG_INFO(PD_LOG, "PDv2: instance {} armed segment {} at block ({},{}) "
                             "chunk {} centre ({:.1f},{:.1f})",
                     instance->GetInstanceId(), ambush.spot.segment,
                     ambush.spot.bx, ambush.spot.by,
                     plan.blocks[ambush.spot.blockIndex].chunkId, ambush.x, ambush.y);
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} armed {} corridor(s) with an ambush "
                         "(chance {}%, {} mob(s) each)",
                 instance->GetInstanceId(), uint32(_ambushes.size()),
                 cfg.ambushChancePct, cfg.ambushMobs);
    }

    void PDv2InstanceScript::TickAmbushes()
    {
        if (_ambushes.empty())
        {
            return;
        }

        // Null until the layout's walk grid is built, which is only ever the
        // case for a run whose kit metadata never loaded. The block test below
        // stands on its own without it; the grid only refines it.
        WalkGrid const* grid = GetWalkGrid();
        Map::PlayerList const& players = instance->GetPlayers();
        for (Ambush& ambush : _ambushes)
        {
            if (!ambush.armed)
            {
                continue;
            }
            for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
            {
                Player* player = it->GetSource();
                // A dead player does not spring a trap: the corpse run back
                // through the corridor is not the moment to spend the one
                // ambush this segment gets. A game master springs nothing at
                // all - TickVoidZones filters IsGameMaster for the same reason
                // and it matters more here, because a sprung spot stays spent
                // until the run is rebuilt: a GM flying the layout to look at
                // it would otherwise disarm every ambush in the dungeon.
                if (!player || !player->IsInWorld() || !player->IsAlive() ||
                    player->IsGameMaster())
                {
                    continue;
                }
                // The trigger is the BLOCK, not a disc (Round C / C2). A
                // player cannot cross a corridor block without standing on one
                // of its cells, whatever the corridor's kind and whichever
                // pair of sockets they walk between - which is exactly what
                // the 9 yd disc could not say (header comment on the struct).
                // Non-negative is part of the test: global cells are
                // non-negative inside the field, and integer division
                // truncates towards zero, so a position outside it would
                // divide to a block it is not in.
                int gcx = 0, gcy = 0;
                WorldToCell(player->GetPositionX(), player->GetPositionY(), gcx, gcy);
                if (gcx < 0 || gcy < 0 ||
                    gcx / PD_CELLS_PER_BLOCK != ambush.spot.bx ||
                    gcy / PD_CELLS_PER_BLOCK != ambush.spot.by)
                {
                    continue;
                }
                if (grid)
                {
                    // In the block but off its lane: a corridor block is
                    // 66.67 yd across and only its lane is floor, so a player
                    // who got onto the wall band (a jump, a knockback, a GM
                    // drop) is over the corridor rather than in it. 2D
                    // throughout, like the barrier hint and for the same
                    // reason - the dungeon is one floor plane and a Z term
                    // would only measure the height of a jump.
                    //
                    // Known and bounded: a closed barrier takes its four lane
                    // cells OUT of this grid (SetCellsWalkable), so standing
                    // exactly in a sealed doorway of an armed corridor does
                    // not fire the spot. It costs nothing - those four cells
                    // are the last of the lane, so the player crossed the rest
                    // of the corridor first and the ambush already had every
                    // other cell of the block to fire on - and the moment
                    // OpenBarrier hands the cells back the spot covers them
                    // again.
                    GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                    if (!grid->At(cell.x, cell.y))
                    {
                        continue;
                    }
                }
                FireAmbush(ambush, player);
                // Spent. Whoever walked in first is the one it fires on, and
                // the rest of this player list has nothing left to trigger.
                break;
            }
        }
    }

    void PDv2InstanceScript::FireAmbush(Ambush& ambush, Player* player)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();

        // DISARMED FIRST, before anything below can fail. The scan runs four
        // times a second and a player crosses the block over several seconds:
        // a spot left armed through a failed summon would stun them again on
        // the next tick, and again after that. Round C / C2 changed WHAT is
        // stood in (a corridor block, no longer a disc); it did not change
        // that a player stands in it for many ticks, which is the reason.
        ambush.armed = false;

        if (cfg.ambushStunSpell)
        {
            // The PLAYER is the caster (design §B5.3): AddAura with a matching
            // caster and target is the shape that gives the client a real stun
            // with a debuff icon, and the default 20170 is aura-only with a
            // flat 2 s duration, so it releases itself and no code here has to
            // remember to take it off.
            player->AddAura(cfg.ambushStunSpell, player);
        }
        sPDv2UILink->SendNotice(player, "Ambush!");

        // Along the corridor and across it, in WORLD coordinates. The block
        // frame's u runs south and v runs east (PDv2WorldMath.h: x = MAX -
        // (by*BLOCK + u), y = MAX - (bx*BLOCK + v)), so u is the world X axis
        // and v the world Y axis, up to a sign that a symmetric ± pattern does
        // not care about. A corridor with a north or south socket therefore
        // runs along world X, and anything else along world Y.
        //
        // A corner piece answers both readings; this takes the N|S one and lets
        // the grid veto sort out the offsets that land in the wall, because
        // "which of the two halves of an L is the player in" is a question the
        // block plan cannot answer and the walk grid can.
        bool const alongWorldX = (ambush.socketMask & (SOCKET_N | SOCKET_S)) != 0;

        WalkGrid const* grid = GetWalkGrid();
        uint32 born = 0;
        for (size_t i = 0; i < ambush.picks.size(); ++i)
        {
            AmbushOffset const& offset = AMBUSH_OFFSETS[i % AMBUSH_OFFSET_COUNT];
            float cx = player->GetPositionX() +
                       (alongWorldX ? offset.along : offset.across);
            float cy = player->GetPositionY() +
                       (alongWorldX ? offset.across : offset.along);

            // The same veto SplitOnDeath uses on a child's offset, for the same
            // reason: gravity is off, so a mob placed past the platform edge
            // hovers over the void for ever - unreachable, unkillable and
            // permanently in combat with the player who sprang the trap. The
            // walk grid is the only thing on this server that knows where floor
            // is, so it decides; the player's own feet are the fallback,
            // because the player is provably standing on floor.
            if (grid)
            {
                int gcx = 0, gcy = 0;
                WorldToCell(cx, cy, gcx, gcy);
                GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                if (!grid->At(cell.x, cell.y))
                {
                    cx = player->GetPositionX();
                    cy = player->GetPositionY();
                }
            }

            PDv2MobData proto;
            proto.role = ambush.picks[i].role;
            proto.casterSpellId = ambush.picks[i].casterSpellId;
            // In no room and in no counter: an ambush is risk on the road, not
            // progress (design §B5.3, the same exemption the patrol carries).
            // affixMask is left 0 - "no affix" - even though the draw rolled
            // the flag.
            proto.roomIndex = PD_ROOM_NONE;
            proto.countsForRun = false;

            // Z from the SPOT, never from the player: ambush.z is the floor
            // plane this corridor was placed at, and a player caught mid-jump
            // must not hand four gravity-less mobs a permanent hover.
            Creature* c = SpawnTaggedMob(ambush.picks[i].entry, proto, cx, cy, ambush.z);
            if (!c)
            {
                continue;
            }
            ++born;

            // The whole point of the trap: they are already on the player when
            // the stun ends, rather than waiting to be noticed.
            if (CreatureAI* ai = c->AI())
            {
                ai->AttackStart(player);
            }
        }

        LOG_INFO(PD_LOG, "PDv2: instance {} sprang the ambush of segment {} in block "
                         "({}, {}) on {} - {} mob(s), stun spell {}",
                 instance->GetInstanceId(), ambush.spot.segment, ambush.spot.bx,
                 ambush.spot.by, player->GetName(), born, cfg.ambushStunSpell);
    }

    bool PDv2InstanceScript::CheckpointSpot(float& x, float& y, float& z) const
    {
        if (_checkpointRoom < 0 || static_cast<size_t>(_checkpointRoom) >= _roomSpot.size())
        {
            return false;
        }
        RoomSpot const& s = _roomSpot[static_cast<size_t>(_checkpointRoom)];
        x = s.x;
        y = s.y;
        z = s.z;
        return true;
    }

    void PDv2InstanceScript::OnUnitDeath(Unit* unit)
    {
        if (!unit || !unit->IsPlayer())
        {
            return;
        }

        // RECORDED ONLY. This hook fires from setDeathState(JustDied)
        // (Unit.cpp:11439-11440) - the core is still inside Unit::Kill, and
        // KillPlayer has not yet set the corpse state or the death timer, so
        // anything resurrected from in here would be killed again on the way
        // out. RespawnPending does it on the next 1 Hz tick instead, and that
        // one second is the whole window the release veto in PDClientLink
        // exists to cover.
        _pendingRespawn[unit->GetGUID()] = getMSTime();

        // Round E / R1. The cap's only input from the run floor, counted HERE
        // rather than in RespawnPending, because this hook is the one place a
        // death is certain. The next tick's respawn can be pre-empted by a
        // logout, a GM resurrection or the player leaving the map, and none of
        // those un-kills anybody. Every death of every player counts - the
        // party shares one run, and it is the run that is being graded.
        //
        // Scoped to the run by SpawnFromPlan's `_run = PDv2RunState()`, which
        // is what keeps a fall into the void on a not-yet-built map out of the
        // next run's tally.
        //
        // Saturating, never wrapping (PDv2RunState::deaths says why 256 deaths
        // reading as a clean clear would be the expensive kind of bug).
        if (_run.deaths < 255)
        {
            ++_run.deaths;
        }
        MarkRunDirty();

        // One line per player DEATH, and a wipe-heavy run has plenty (Round E
        // / R3). The tally itself is not diagnostics - it is graded by R1 and
        // reported by FinishRun's INFO, which stays ungated.
        if (PDv2Debug())
        {
            LOG_INFO(PD_LOG, "PDv2: {} died in instance {} ({} death(s) this run) - "
                             "respawn on the next tick",
                     unit->GetName(), instance->GetInstanceId(), uint32(_run.deaths));
        }
    }

    void PDv2InstanceScript::RespawnPending()
    {
        if (_pendingRespawn.empty())
        {
            return;
        }

        // Taken out and the map emptied BEFORE anything below runs. Every
        // branch drops its entry anyway, so this changes no outcome - but
        // ResurrectPlayer walks UpdateZone and the aura machinery, and an
        // iterator into _pendingRespawn held across that would dangle the
        // moment any of it reached back into OnUnitDeath and rehashed the
        // map. A death recorded while this loop runs is a NEW death and
        // belongs to the next tick, which is exactly what it gets.
        std::vector<std::pair<ObjectGuid, uint32>> due(_pendingRespawn.begin(),
                                                       _pendingRespawn.end());
        _pendingRespawn.clear();

        for (auto const& entry : due)
        {
            // GetPlayer(Map const*, guid) already answers nullptr for anyone
            // who is not in world on THIS map, so a player who left, logged
            // out or was evicted simply drops out here: the core owns them.
            Player* player = ObjectAccessor::GetPlayer(instance, entry.first);
            if (!player || player->IsAlive())
            {
                continue;               // gone, or someone else resurrected them
            }

            // Alive, full health and NO resurrection sickness: the second
            // parameter of Player::ResurrectPlayer(float restore_percent,
            // bool applySickness) IS the sickness switch - false returns
            // early at src/server/game/Entities/Player/Player.cpp:4446,
            // before the CastSpell(this, 15007) at :4460 that would apply
            // it. Dying in a run costs neither the sickness (operator, T2
            // 2026-09-08) nor durability.
            // SpawnCorpseBones is a no-op when the player never released, and
            // when they did it only clears the ghost flag and re-saves the
            // auras - it never writes the position, so dying in here can
            // never be what stores a character on this map.
            uint32 const waitedMs = GetMSTimeDiffToNow(entry.second);
            player->ResurrectPlayer(1.0f, false);
            player->SpawnCorpseBones();

            // Where to (Round C / C5): the furthest cleared boss hall, else
            // the entrance. Both are COMPUTED from the run's own state - the
            // player never chose either, which is the whole point of dropping
            // B1's clickable altars. Returning to the entrance is the normal
            // case for the first half of a run, not a fallback, so it is not
            // logged as a warning any more. If there is not even an entrance,
            // nowhere - they rise where they fell, because (0, 0, 0) on a
            // composed map is the void and the fall catcher that would rescue
            // them from it is switched off by the same missing entrance that
            // got us here.
            float cx = 0.0f, cy = 0.0f, cz = 0.0f;
            if (CheckpointSpot(cx, cy, cz))
            {
                player->TeleportTo(instance->GetId(), cx, cy, cz + 2.0f, 0.0f);
                sPDv2UILink->SendNotice(player, "You return to the last boss's hall.");
                LOG_INFO(PD_LOG, "PDv2: {} returned alive to the hall of chain room {} in "
                                 "instance {} after {} ms",
                         player->GetName(), _checkpointChain, instance->GetInstanceId(), waitedMs);
            }
            else if (_haveEntrance)
            {
                player->TeleportTo(instance->GetId(), _entranceX, _entranceY, _entranceZ + 2.0f, 0.0f);
                sPDv2UILink->SendNotice(player, "You return to the entrance.");
                LOG_INFO(PD_LOG, "PDv2: {} returned alive to the entrance of instance {} "
                                 "after {} ms",
                         player->GetName(), instance->GetInstanceId(), waitedMs);
            }
            else
            {
                sPDv2UILink->SendNotice(player, "You rise again where you fell.");
                LOG_WARN(PD_LOG, "PDv2: instance {} has no entrance - {} was resurrected "
                                 "in place after {} ms",
                         instance->GetInstanceId(), player->GetName(), waitedMs);
            }
        }
    }

    void PDv2InstanceScript::CatchFallers()
    {
        if (!_haveEntrance)
        {
            return;
        }

        float const floor = sPDv2Mgr->GetConfig().floorZ;
        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsInWorld())
            {
                continue;
            }
            if (player->GetPositionZ() > floor - FALL_MARGIN_YD)
            {
                continue;
            }

            // Put them back at the entrance rather than killing them. There is
            // no terrain to land on anywhere else, so a corpse run would strand
            // them, and dying to the architecture is not a mechanic.
            player->TeleportTo(instance->GetId(), _entranceX, _entranceY, _entranceZ + 2.0f, 0.0f);
            ChatHandler(player->GetSession()).SendSysMessage(
                "You fell off the dungeon and were returned to the entrance.");
            // Per TICK per faller, in the worst case: a player who keeps
            // sliding off the same ledge is caught again every pass of
            // CatchFallers, so this one is gated by V2.Debug (Round E / R3)
            // and the player's own chat line is the ungated evidence.
            if (PDv2Debug())
            {
                LOG_INFO(PD_LOG, "PDv2: returned {} to the entrance from Z {}",
                         player->GetName(), player->GetPositionZ());
            }
        }
    }

    void PDv2InstanceScript::EvictDisconnected()
    {
        // A client that crashes or is killed leaves its player standing here,
        // alive in memory, for the session's grace period - and a reconnect
        // during that window attaches to THAT player, so the server sends the
        // returning client straight back to map 760 while its fresh DLL has
        // composed nothing yet. The client dies on CMap::LoadWdt, relogs into
        // the same trap, and the DB says nothing about any of it because the
        // live player was never saved (measured 2026-08-06: crash 6 s after
        // injection, no intercept in the DLL log, characters.map already 0).
        //
        // So the eviction has to happen on the LIVE player, and this is the
        // only place that sees it: send anyone whose socket is gone home at
        // once, before a reconnect can find them in here.
        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsInWorld() || !player->GetSession())
            {
                continue;
            }
            if (!player->GetSession()->IsSocketClosed() && !player->GetSession()->IsKicked())
            {
                continue;
            }

            player->TeleportTo(player->m_homebindMapId, player->m_homebindX,
                               player->m_homebindY, player->m_homebindZ,
                               player->GetOrientation());
            LOG_INFO(PD_LOG, "PDv2: evicted {} from the dungeon - the client is gone "
                             "and a reconnect must not land back on this map",
                     player->GetName());
        }
    }

    void PDv2InstanceScript::Update(uint32 diff)
    {
        if (_fallCheckTimer <= diff)
        {
            _fallCheckTimer = FALL_CHECK_INTERVAL_MS;
            CatchFallers();
            // After the fall catcher on purpose: a player who died BELOW the
            // floor is first pulled back onto the map by CatchFallers and
            // then sent on to their checkpoint, so the checkpoint is the
            // teleport that lands last and the ordering never leaves a corpse
            // in the void.
            RespawnPending();
            // Round B / B3. After the respawn, so a player who just landed at
            // their checkpoint is measured where they actually are; a barrier
            // only ever talks, so its place in the tick is free.
            HintBarriers();
            // Round C / C8. After the barrier hint and before the eviction
            // sweep, because both of those talk to players and the finale's
            // own notice belongs in the same second as the line that earned
            // it. Inert on every tick of every run that has not finished.
            TickFinale();
            EvictDisconnected();
            TickVoidZones();

            // The clock rides the same one-second tick. It is not marked dirty:
            // the UI polls at 1 Hz anyway, and a flag that ticks on its own
            // would tell a reader "something happened" once a second forever.
            if (_run.started && !_run.complete)
            {
                _run.elapsedSec = GetMSTimeDiffToNow(_run.startedMs) / 1000;
            }

            // Round E / WP5, and BEFORE the push below rather than beside the
            // finale: the event's countdown and the host's health bar are HUD
            // fields, so the second in which the clock moves has to be the
            // second the frame carries. Ticking after the push would show
            // every player a value that was already one second stale, and the
            // last tick of a won event would push "1 second left" and only
            // then win it. Inert on every run whose layout has no event.
            TickEvents();

            // The HUD's whole pull side, after the clock so the frame carries
            // the second it was sent in. The link decides whether anything is
            // worth saying; a still dungeon costs nothing on the wire.
            sPDv2UILink->OnInstanceTick(this);
        }
        else
        {
            _fallCheckTimer -= diff;
        }

        // Round B / B5, on its own cadence and AFTER the 1 Hz branch. A player
        // the respawn just teleported to their checkpoint is measured where
        // they now are rather than where they died, the only ordering that
        // cannot spring a trap at a position the player no longer occupies.
        // Accumulating rather than counting down: the map ticks at 10 ms, so
        // the remainder above 250 is a fraction of one tick and dropping it
        // costs nothing.
        _ambushTimer += diff;
        if (_ambushTimer >= AMBUSH_SCAN_MS)
        {
            _ambushTimer = 0;
            TickAmbushes();
        }
    }

    // ----------------------------------------------------------------------
    // Round E / WP5: event rooms ("Hold the line").
    //
    // The optional dead-end pocket the planner seats off a boss segment holds
    // one creature, NPC_EVENT_HOST. A player talks to him, a clock starts,
    // one attacker walks in every few seconds, and the party wins by keeping
    // him alive until the clock runs out.
    //
    // Everything below splits along one line: SpawnEventRooms DECIDES (at
    // build time, on a seeded stream) and the tick EXECUTES. The wave, where
    // each of its members walks in and where the reward will stand are all
    // fixed the moment the dungeon is populated, so the same layout always
    // fights the same fight and the second a player is busy being attacked
    // costs the server nothing it could have paid minutes earlier. It is the
    // same division SpawnAmbushPlan/FireAmbush already draw.
    //
    // Nothing here rolls anything of its own - no urand, no PDRandom. The one
    // draw is SelectSpawns on the event stream, which is the layout seed
    // mixed with PD_EVENT_SEED_MIX and the segment.
    // ----------------------------------------------------------------------

    void PDv2InstanceScript::SpawnEventRooms(BlockPlan const& plan)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2AccountState const account = sPDv2Mgr->GetAccountState(_accountId);

        // How many attackers one defence sends in: the whole duration divided
        // by the gap between them, i.e. twelve on the defaults (60 / 5). The
        // last one therefore walks in at 55 s and the clock runs out five
        // seconds later, which is what makes the final stretch a fight rather
        // than a wait. Guarded against a zero gap because both numbers are
        // live conf values and a division is not a place to trust a clamp.
        // ...and never below one: SpawnEverySec >= DurationSec is legal on
        // both keys on its own, and the truncation would leave a defence
        // with no attackers at all - an unloseable free chest.
        int const waveSize = cfg.eventSpawnEverySec > 0
                             ? std::max(1, cfg.eventDurationSec / cfg.eventSpawnEverySec)
                             : 0;

        double const mid = PD_BLOCK_SIZE_YD / 2.0;
        WalkGrid const* grid = GetWalkGrid();

        // The veto the room pass, the patrol spawn and the finale spots all
        // take, for the reason they all take it: an anchor is a kit constant
        // while the walk grid is what this instance actually composed, and
        // gravity is off on this map - so anything seated off the floor
        // hovers over the void for ever. Here it matters most for the HOST:
        // a pilgrim in the void could not be reached by the wave that is
        // supposed to kill him, and the event would be unlosable.
        auto vetoSpot = [this, grid](float& x, float& y, char const* what)
        {
            if (!grid)
            {
                return;
            }

            int gcx = 0, gcy = 0;
            WorldToCell(x, y, gcx, gcy);
            GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
            if (grid->At(cell.x, cell.y))
            {
                return;
            }

            GridPoint snapped;
            if (!NearestWalkable(*grid, cell.x, cell.y, SPAWN_FALLBACK_SNAP_CELLS,
                                 snapped))
            {
                LOG_WARN(PD_LOG, "PDv2: instance {} event {} stands on a void cell and "
                                 "found no floor within {} cell(s) - it stays where it was",
                         instance->GetInstanceId(), what, SPAWN_FALLBACK_SNAP_CELLS);
                return;
            }

            int scx = 0, scy = 0;
            grid->GlobalFromLocalCell(snapped, scx, scy);
            double wx = 0.0, wy = 0.0;
            CellCentreToWorld(scx, scy, wx, wy);
            x = static_cast<float>(wx);
            y = static_cast<float>(wy);
        };

        for (PlacedBlock const& b : plan.blocks)
        {
            if (!b.isEvent)
            {
                continue;
            }

            EventRoom event;
            event.bx = b.bx;
            event.by = b.by;
            // A pocket's segment is its HOST's segment (SegmentOf reads
            // branchOf for exactly this case), which is what puts one event
            // per boss segment on its own draw.
            event.segment = SegmentOf(plan, b);

            float centreX = 0.0f, centreY = 0.0f, centreZ = 0.0f;
            sPDv2Mgr->BlockToWorld(b.bx, b.by, mid, mid, centreX, centreY, centreZ);
            // One block, one floor plane: BlockToWorld's Z is a property of
            // the block, so the host, the wave and the cache all stand on it.
            event.z = centreZ;

            RoomAnchors const* anchors = sPDv2Mgr->RoomAnchorsFor(b.chunkId);

            // The host on the ENTRY anchor - the cell every walk into the
            // room arrives on, so a player who enters is already looking at
            // him and the wave that follows them in has the whole room to
            // cross. The block centre is the fallback for a chunk that
            // publishes no anchors; it is walkable in every room variant the
            // kit ships.
            float hostX = centreX;
            float hostY = centreY;
            if (anchors && anchors->hasEntry)
            {
                float unusedZ = 0.0f;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, anchors->entry.u,
                                       anchors->entry.v, hostX, hostY, unusedZ);
            }
            vetoSpot(hostX, hostY, "host");

            // Round E / WP8, operator finding 6: he FACES the doorway, i.e. the
            // party he is waiting for, and computed here because this is the
            // one pass that holds the PlacedBlock the doorway belongs to. An
            // event pocket is a dead end, so its single socket is that doorway;
            // FacingTowardDoorway answers 0 - the old due-north summon - for
            // anything it cannot read, which is a plain look and never a wrong
            // one.
            //
            // Stored on the EventRoom because the CHEST inherits it: WP8's won
            // event leaves the reward on his square, turned the way he was, and
            // deriving the same angle a second time in CloseEvent is how the
            // two would eventually disagree. The kit's chest anchor that used
            // to be decided here is gone with the same change - the reward has
            // no spot of its own to choose any more.
            event.hostX = hostX;
            event.hostY = hostY;
            event.hostO = FacingTowardDoorway(plan, b, hostX, hostY);
            if (PDv2Debug())
            {
                LOG_INFO(PD_LOG, "PDv2: instance {} event host in block ({}, {}) faces "
                                 "{:.3f} rad toward its doorway",
                         instance->GetInstanceId(), b.bx, b.by, event.hostO);
            }

            // The rim: the room's own six spawn anchors, FARTHEST FROM THE
            // BLOCK CENTRE FIRST. The distance is taken in the block's own
            // (u, v) frame, before the veto can nudge a point by a cell or
            // two, so the order is a property of the kit rather than of this
            // instance's walk grid - the same chunk always sends its wave in
            // through the same door.
            //
            // Farthest first is the whole point: the outermost pair are the
            // caster anchors (12.0 yd against the melee ring's 11.31), so the
            // first attackers walk the longest way in and the party standing
            // at the host has the most time to meet them.
            //
            // stable_sort and not sort: two anchors at the same radius keep
            // the kit's own order, and "the same seed fights the same fight"
            // must not turn on an implementation's choice of pivot.
            struct RimAnchor
            {
                double d2 = 0.0;
                float  x = 0.0f;
                float  y = 0.0f;
            };
            std::vector<RimAnchor> ring;
            if (anchors)
            {
                for (SpawnAnchor const& anchor : anchors->spawns)
                {
                    RimAnchor point;
                    double const du = anchor.u - mid;
                    double const dv = anchor.v - mid;
                    point.d2 = du * du + dv * dv;
                    float unusedZ = 0.0f;
                    sPDv2Mgr->BlockToWorld(b.bx, b.by, anchor.u, anchor.v,
                                           point.x, point.y, unusedZ);
                    vetoSpot(point.x, point.y, "rim");
                    ring.push_back(point);
                }
            }
            std::stable_sort(ring.begin(), ring.end(),
                             [](RimAnchor const& lhs, RimAnchor const& rhs)
                             {
                                 return lhs.d2 > rhs.d2;
                             });
            for (RimAnchor const& point : ring)
            {
                event.rim.emplace_back(point.x, point.y);
            }
            if (event.rim.empty())
            {
                // A chunk with no anchors at all. The wave then walks in on
                // the host's own square, which is a worse fight and not a
                // broken one - and TickEvents' modulo needs a non-empty rim.
                event.rim.emplace_back(centreX, centreY);
                LOG_WARN(PD_LOG, "PDv2: instance {} chunk {} publishes no spawn anchors - "
                                 "its event wave arrives on the block centre",
                         instance->GetInstanceId(), b.chunkId);
            }

            // ONE synthetic room's worth of trash on the EVENT stream, drawn
            // here and stored. Same inputs the ambush uses, with two on
            // purpose: casterPct is the event's own key (a wave has to CLOSE
            // on the host, not shoot him from the doorway) and affixPct is
            // copied for the SHAPE of the stream only - the draw rolls
            // `affixed` per pick either way, and proto.affixMask staying 0 in
            // the tick is what says an event mob wears no affix.
            SpawnSelectInputs in;
            RoomRequest room;
            room.roomIndex = 0;
            room.isBoss = false;
            in.rooms.push_back(room);
            in.spawnsPerRoom = waveSize;
            in.bossRoomAdds = 0;
            in.casterPct = cfg.eventCasterPct;
            in.affixPct = cfg.affixPct;
            in.bandMin = account.cfgBandMin;
            in.unlockedDlvl = static_cast<int>(account.dlvl);

            std::vector<RoomSpawns> out;
            uint32 const seed = plan.effectiveSeed ^ PD_EVENT_SEED_MIX ^
                                (static_cast<uint32>(event.segment) * PD_SEGMENT_SEED_STEP);
            if (sPDv2PackMgr->SelectSpawns(seed, in, out) && !out.empty() &&
                !out[0].picks.empty())
            {
                event.picks = out[0].picks;
            }
            else if (waveSize > 0)
            {
                // The same degradation the room draw and the ambush take when
                // the pack SQL was never applied: placeholder mammoths rather
                // than a defence with nothing to defend against, which would
                // be an unloseable free chest.
                event.picks.assign(static_cast<size_t>(waveSize),
                                   SpawnPick{ PLACEHOLDER_CREATURE, PACK_ROLE_MELEE, 0 });
            }

            Creature* host = instance->SummonCreature(
                NPC_EVENT_HOST, Position(hostX, hostY, event.z, event.hostO));
            if (!host)
            {
                // No host, no event - and deliberately no EventRoom either, so
                // every entry in _events has a live GUID and EventStateFor
                // cannot answer for a creature that was never born.
                LOG_ERROR(PD_LOG, "PDv2: instance {} failed to summon the event host "
                                  "(missing creature_template {}?) - block ({}, {}) "
                                  "stays empty",
                          instance->GetInstanceId(), uint32(NPC_EVENT_HOST), b.bx, b.by);
                continue;
            }

            // The facing goes into the home position too, the same rule
            // SpawnTaggedMob follows: he never moves, but a home orientation
            // that disagreed with the summon would turn him the moment
            // anything resets him.
            host->SetHomePosition(hostX, hostY, event.z, event.hostO);
            // The module's blanket policy, and it applies to him too: nothing
            // this dungeon summons pays kill reputation (SpawnTaggedMob states
            // the case in full). He is meant to be killable, so unlike Chromie
            // the flag is not academic here.
            host->SetReputationRewardDisabled(true);
            // _spawnedGuids, not _decorGuids: he is a creature, and that is
            // the list DespawnAll walks with DespawnOrUnsummon - so a rebuild
            // takes him down with the run he belongs to.
            _spawnedGuids.push_back(host->GetGUID());

            // NO SetDungeonHealth HERE, and the absence is deliberate.
            //
            // The design asks that the host scale like the mobs do, i.e. by
            // the run's difficulty factor 100 + difficulty *
            // V2.Diff.HealthPctPerLevel percent. He already does: the summon
            // above went through PDv2Scaling exactly like every trash mob -
            // IsDungeonCreature is true for him (map 760, no critter type, no
            // player owner), so OnBeforeCreatureSelectLevel set him to
            // PD_MOB_LEVEL and OnCreatureSelectLevel then applied that very
            // multiplier inside SummonCreature (PDv2Scaling.cpp:232-263, and
            // SpawnTaggedMob's affix comment says the same thing about the
            // ordering). Re-applying it here would SQUARE it and hand a
            // difficulty-10 party a host with several times the intended bar.
            //
            // What tunes the fight is therefore the template
            // (mod_pdungeon_event.sql: rank 1 elite, HealthModifier 50,
            // RegenHealth 0), which is where a retune belongs.
            //
            // Nothing touches his npcflag here either: the template already
            // carries UNIT_NPC_FLAG_GOSSIP, which is what makes him clickable
            // in the first place, and StartEvent is what takes it away.

            event.host = host->GetGUID();

            LOG_INFO(PD_LOG, "PDv2: instance {} staged an event in block ({}, {}) chunk {} "
                             "- segment {}, host at ({:.1f}, {:.1f}, {:.1f}) with {} hp, "
                             "{} attacker(s) on {} rim point(s)",
                     instance->GetInstanceId(), b.bx, b.by, b.chunkId, event.segment,
                     hostX, hostY, event.z, host->GetMaxHealth(),
                     uint32(event.picks.size()), uint32(event.rim.size()));

            _events.push_back(std::move(event));
        }
    }

    PDv2InstanceScript::EventRoom* PDv2InstanceScript::EventFor(ObjectGuid host)
    {
        if (host.IsEmpty())
        {
            return nullptr;
        }
        for (EventRoom& event : _events)
        {
            if (event.host == host)
            {
                return &event;
            }
        }
        return nullptr;
    }

    bool PDv2InstanceScript::StartEvent(Creature* host, Player* starter)
    {
        if (!host)
        {
            return false;
        }

        EventRoom* event = EventFor(host->GetGUID());
        // Idle and ONLY Idle. A running event must not have its clock reset
        // by a second click, and a finished one must never go back to
        // Running - the gossip already refuses both by offering the item only
        // on Idle, but the gossip is a menu a client sends and this is the
        // server's own answer to it.
        if (!event || event->state != EventState::Idle)
        {
            return false;
        }

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        uint32 const now = getMSTime();

        event->state = EventState::Running;
        event->deadlineMs = now + static_cast<uint32>(cfg.eventDurationSec) * 1000;
        // Now, not one interval from now: the first attacker walks in on the
        // very next tick, so the fight starts when the player says it does
        // rather than five silent seconds later.
        event->nextSpawnMs = now;
        event->nextPick = 0;

        // No second offer while he is busy being defended, and never put back
        // (Round E / WP8): a won event now despawns him and leaves his chest
        // instead, a lost one leaves him dead, so the flag has nothing left to
        // return to either way.
        host->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);

        std::string const notice = Acore::StringFormat(
            "Hold the line! {} seconds.", cfg.eventDurationSec);
        ForEachRunPlayer(instance, [&notice](Player* player)
        {
            sPDv2UILink->SendNotice(player, notice);
        });

        MarkRunDirty();

        LOG_INFO(PD_LOG, "PDv2: instance {} started the event in block ({}, {}) - "
                         "{} s, {} attacker(s) every {} s, started by {}",
                 instance->GetInstanceId(), event->bx, event->by,
                 cfg.eventDurationSec, uint32(event->picks.size()),
                 cfg.eventSpawnEverySec, starter ? starter->GetName() : "nobody");
        return true;
    }

    void PDv2InstanceScript::TickEvents()
    {
        if (_events.empty())
        {
            return;
        }

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        uint32 const now = getMSTime();

        for (EventRoom& event : _events)
        {
            // The steady state of every event on almost every tick: one that
            // nobody has started yet, and one whose closing beat already ran.
            if (event.state == EventState::Idle || event.closed)
            {
                continue;
            }

            // Not Running and not closed means OnEventHostDied moved it to
            // Lost from inside the host's death, where despawning anything is
            // forbidden, and left the closing to this pass.
            if (event.state != EventState::Running)
            {
                CloseEvent(event);
                continue;
            }

            Creature* host = instance->GetCreature(event.host);
            if (!host || !host->IsAlive())
            {
                // He died between two ticks, or something took him off the
                // map entirely. The same loss either way - "the pilgrim is no
                // longer standing" is the whole losing condition.
                event.state = EventState::Lost;
                CloseEvent(event);
                continue;
            }

            // Wrap-safe, exactly as TickFinale reads its own deadline:
            // getMSTime() is a uint32 of milliseconds that wraps every 49.7
            // days, and a plain `>=` across that wrap would park the event
            // for another 49 of them.
            if (static_cast<int32>(now - event.deadlineMs) >= 0)
            {
                event.state = EventState::Won;
                CloseEvent(event);
                continue;
            }

            if (static_cast<int32>(now - event.nextSpawnMs) < 0 ||
                event.nextPick >= event.picks.size())
            {
                continue;
            }

            SpawnPick const& pick = event.picks[event.nextPick];

            PDv2MobData proto;
            proto.role = pick.role;
            proto.casterSpellId = pick.casterSpellId;
            // In no room, in no counter and never in the layout at all. The
            // first two are what an ambush and a patrol already carry - an
            // event attacker must not move a room's kill count or a barrier's
            // numerator - and isExtra is the third, which is what keeps a
            // wave that can be farmed for ever off the currency faucet while
            // still dropping materials (PDv2MobData::isExtra says why).
            proto.roomIndex = PD_ROOM_NONE;
            proto.countsForRun = false;
            proto.isExtra = true;
            // affixMask stays 0: the draw rolled the flag, the design gives
            // an event wave no affix, and 0 is what says so.

            // Round-robin over the rim, so twelve attackers arriving through
            // six doors come in pairs from opposite sides rather than all
            // through one. The rim is never empty (SpawnEventRooms falls back
            // to the block centre), so the modulo is safe.
            std::pair<float, float> const& spot =
                event.rim[event.nextPick % event.rim.size()];

            if (Creature* mob = SpawnTaggedMob(pick.entry, proto, spot.first,
                                               spot.second, event.z))
            {
                event.wave.push_back(mob->GetGUID());

                // Straight at the pilgrim. His faction (1727, friendGroup 7 /
                // enemyGroup 8) already makes him a valid enemy of every pack
                // faction this dungeon spawns, so this only decides who the
                // mob walks to FIRST.
                if (CreatureAI* ai = mob->AI())
                {
                    ai->AttackStart(host);
                }
                // ...and the standing threat entry behind it, which is what
                // decides who the mob turns to when its current victim is
                // gone (EVENT_WAVE_HOST_THREAT says how much and why).
                mob->GetThreatMgr().AddThreat(host, EVENT_WAVE_HOST_THREAT);
            }

            // Advanced whether or not the summon worked: a failed spawn costs
            // the party one attacker, it must not stall the queue and hand
            // them a silent, empty minute instead.
            event.nextSpawnMs += static_cast<uint32>(cfg.eventSpawnEverySec) * 1000;
            ++event.nextPick;
        }
    }

    void PDv2InstanceScript::CloseEvent(EventRoom& event)
    {
        // EXACTLY ONCE. Two callers can reach a finished event - the tick
        // that ended it, and the tick after a death that ended it from
        // outside - and paying the reward twice is the failure this flag
        // exists to make impossible.
        if (event.closed)
        {
            return;
        }
        event.closed = true;

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        bool const won = event.state == EventState::Won;

        // The wave leaves with the fight. Only the LIVE ones: a corpse is
        // somebody's loot until they walk over to it, and mats drop from an
        // extra mob always (PDv2MobData::isExtra). They are all in
        // _spawnedGuids as well, so anything still standing at a rebuild goes
        // with the run regardless.
        uint32 dismissed = 0;
        for (ObjectGuid const& guid : event.wave)
        {
            Creature* mob = instance->GetCreature(guid);
            if (!mob || !mob->IsAlive())
            {
                continue;
            }
            mob->DespawnOrUnsummon();
            ++dismissed;
        }
        event.wave.clear();

        // A notice and not a chat line, for the reason the finale's portal is
        // one: the addon paints these as raid warnings, and the end of a
        // sixty-second defence is exactly the beat that must not scroll past.
        char const* const line = won
            ? "The pilgrim lives! Hold the line - won."
            : "The pilgrim has fallen. The line is lost.";
        ForEachRunPlayer(instance, [line](Player* player)
        {
            sPDv2UILink->SendNotice(player, line);
        });

        uint32 xp = 0;
        if (won)
        {
            // Round E / WP8, operator finding 8: "when the event is won a small
            // chest spawns where the NPC stood and the NPC despawns". His own
            // square, his own facing, and then he leaves - the pilgrim is
            // rescued, so a rescued pilgrim standing about for the rest of the
            // run with an empty menu was the thing that read wrong. The reward
            // is GO_EVENT_CHEST and not GO_CHEST: a small Chest01 rather than
            // the dungeon's big TreasureChest01, which is what makes it read as
            // his parting gift instead of as another dead-end cache. The LOOT
            // is a dead-end cache's, and PDv2ChestLoot says so at the dispatch.
            //
            // The angle is his, straight off the EventRoom - unlike the finale
            // cache, which turns display 259 by a measured quarter turn, this
            // model needs no offset, so nothing is added to it here.
            //
            // Order matters only in one direction: the chest is summoned FIRST
            // and the host dismissed after, so a failed summon still leaves
            // somebody standing rather than an empty room and no reward.
            if (GameObject* cache = instance->SummonGameObject(
                    GO_EVENT_CHEST, event.hostX, event.hostY, event.z,
                    event.hostO, 0.0f, 0.0f, 0.0f, 0.0f, 0))
            {
                _decorGuids.push_back(cache->GetGUID());
            }
            else
            {
                LOG_ERROR(PD_LOG, "PDv2: instance {} won an event but failed to summon its "
                                  "cache (missing gameobject_template {}?)",
                          instance->GetInstanceId(), uint32(GO_EVENT_CHEST));
            }

            // Scaled by the run's own loot multiplier, which is the number
            // every other reward in this dungeon is already measured in - so
            // a harder run pays more for the same minute of defence. uint64
            // through the multiply: 1000 * 255 fits an uint32 comfortably,
            // but the conf key is a uint32 and a wide product costs nothing.
            xp = static_cast<uint32>(static_cast<uint64>(cfg.eventParagonXp) *
                                     static_cast<uint64>(_run.lootMultX100) / 100);

            // Everybody who is standing in the dungeon, not only whoever
            // clicked: holding a line is what the whole party did.
#ifdef PDV2_HAS_PARAGON
            ForEachRunPlayer(instance, [xp](Player* player)
            {
                IncreaseParagonXP(player, xp);
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "[Hold the line] +{} Paragon XP.", xp);
            });
#else
            // No mod-paragon in this build, so nothing was paid - and the log
            // line below must not claim that anything was.
            xp = 0;
#endif

            // ...and he goes. His gossip flag used to be handed back here so a
            // won event still had a menu to open onto; WP8 took that away
            // instead, because the menu was empty (PDv2EventNPC's Idle-only
            // offer) and a rescued man who never leaves is the thing the
            // operator called out. DespawnOrUnsummon and not Delete: he is a
            // creature and that is this module's teardown for one, the same
            // call DespawnAll would make on his GUID at the rebuild - which is
            // why his entry may stay in _spawnedGuids, where a second despawn
            // of a creature that is already gone is a lookup that finds
            // nothing.
            if (Creature* host = instance->GetCreature(event.host))
            {
                host->DespawnOrUnsummon();
            }
        }

        // The HUD's countdown and the host's health bar both come off this
        // state, so the frame that follows has to be sent.
        MarkRunDirty();

        LOG_INFO(PD_LOG, "PDv2: instance {} {} the event in block ({}, {}) - "
                         "{} attacker(s) sent in, {} dismissed, {} paragon XP",
                 instance->GetInstanceId(), won ? "won" : "lost", event.bx,
                 event.by, uint32(event.nextPick), dismissed, xp);
    }

    void PDv2InstanceScript::OnEventHostDied(Creature* host)
    {
        if (!host)
        {
            return;
        }

        EventRoom* event = EventFor(host->GetGUID());
        if (!event || event->state != EventState::Running)
        {
            return;
        }

        // RECORDED, not acted on. This runs inside Unit::setDeathState, so it
        // may not despawn the wave that killed him - the next TickEvents pass
        // sees a state that is neither Running nor closed and does the whole
        // closing beat there, at most one second later.
        //
        // Doing it here rather than leaving the death entirely to the tick's
        // own !IsAlive() test is what makes the LOSS instant: the gossip and
        // the HUD read this state, and a pilgrim lying dead under a menu that
        // still says "Hold the line with me!" is a second of nonsense.
        event->state = EventState::Lost;
        MarkRunDirty();
    }

    PDv2InstanceScript::EventState
    PDv2InstanceScript::EventStateFor(ObjectGuid host) const
    {
        if (host.IsEmpty())
        {
            return EventState::Idle;
        }
        for (EventRoom const& event : _events)
        {
            if (event.host == host)
            {
                return event.state;
            }
        }
        // Total by design: any creature, at any time, in any run - including
        // one whose layout has no event at all - answers Idle. That is what
        // makes the gossip's call safe without a second gate.
        return EventState::Idle;
    }

    bool PDv2InstanceScript::EventHudFields(uint32& secLeft, uint32& npcPct) const
    {
        uint32 const now = getMSTime();
        for (EventRoom const& event : _events)
        {
            if (event.state != EventState::Running)
            {
                continue;
            }

            Creature* host = instance->GetCreature(event.host);
            if (!host)
            {
                // He is gone and the next tick will call it lost. Reporting
                // nothing for that one second is better than a bar read off
                // a creature that is not there.
                continue;
            }

            // Ceiling, so a defence with 200 ms left still reads "1" and the
            // countdown only shows 0 once it is actually over. Wrap-safe like
            // every other read of this deadline.
            int32 const leftMs = static_cast<int32>(event.deadlineMs - now);
            secLeft = leftMs > 0 ? static_cast<uint32>((leftMs + 999) / 1000) : 0u;
            // Truncated on purpose, the way a health bar is read everywhere
            // else: 99.6 % is not yet full and must not print as 100.
            npcPct = static_cast<uint32>(host->GetHealthPct());
            return true;
        }
        return false;
    }
}
