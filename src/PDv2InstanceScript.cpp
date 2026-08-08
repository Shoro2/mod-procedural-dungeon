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
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "PDDefines.h"
#include "PDv2Affixes.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "PDv2UILink.h"
#include "Player.h"
#include "Position.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

#include <cmath>

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
        uint32 const PLACEHOLDER_CREATURE = 29402;   // Anub'ar Skirmisher

        float const SPAWN_SPREAD_YD = 12.0f;

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

        // A plan can be re-rolled while this instance is alive - the account
        // keeps the instance, so the old creatures and the old walk grid would
        // otherwise survive under new terrain. Rebuilding on a seed change is
        // what makes `.pdungeon v2 gen` mean the same thing inside as outside.
        if (_spawned && _spawnedSeed != plan->effectiveSeed)
        {
            LOG_INFO(PD_LOG, "PDv2: instance {} was built for seed {}, plan is now {} - "
                             "rebuilding", instance->GetInstanceId(), _spawnedSeed,
                     plan->effectiveSeed);
            DespawnAll();
            _spawned = false;
            _gridReady = false;
            _gridTried = false;
        }

        EnsureWalkGrid(*plan);

        if (!_spawned)
        {
            SpawnFromPlan(*plan);
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

        ++_run.killed;
        if (tag->isRunBoss && _run.bossKilled < _run.bossTotal)
        {
            ++_run.bossKilled;
        }

        if (tag->roomIndex < _roomAlive.size() && _roomAlive[tag->roomIndex] > 0)
        {
            if (--_roomAlive[tag->roomIndex] == 0)
            {
                ++_run.roomsCleared;
            }
        }
        MarkRunDirty();

        // 01 §8: the FL mats drop ON TOP of whatever the creature's own loot
        // table gives, which is what makes the dungeon a way to farm existing
        // content rather than a replacement for it.
        RollBonusLoot(killer);

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
        PDv2RunReward const reward = sPDv2Mgr->GrantRunReward(_accountId, _run.roomsTotal);

        // The HUD's completion toast rides the same grant the chat lines below
        // announce, and fires exactly once because FinishRun does.
        sPDv2UILink->SendEnd(instance, reward);

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
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_runs (seed, map_id, instance_id, leader_guid, account_id, "
            "dlvl, difficulty, loot_mult_x100, rooms_cleared, result, completed_at) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, 1, NOW())",
            _spawnedSeed, instance->GetId(), instance->GetInstanceId(), _leaderGuid,
            _accountId, reward.newDlvl, uint32(_run.difficulty), _run.lootMultX100,
            _run.roomsCleared);

        LOG_INFO(PD_LOG, "PDv2: account {} completed instance {} (seed {}): {}/{} rooms, "
                         "{} kill(s), +{} dxp -> dlvl {}", _accountId,
                 instance->GetInstanceId(), _spawnedSeed, uint32(_run.roomsCleared),
                 uint32(_run.roomsTotal), uint32(_run.killed), reward.dxpGained,
                 reward.newDlvl);

        // Nobody is teleported out. The dungeon stays walkable after its last
        // boss because farming it is the point (01 §8) - the way out is the way
        // the player came in.
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
    }

    void PDv2InstanceScript::EnsureWalkGrid(BlockPlan const& plan)
    {
        if (_gridTried)
        {
            return;
        }
        _gridTried = true;

        std::string error;
        if (!BuildWalkGrid(plan, [](int chunkId) { return sPDv2Mgr->WalkMaskFor(chunkId); },
                           &_grid, &error))
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

        // Rooms only, in plan order. A corridor is 8.3 yd wide, so anything
        // standing in one would be shoulder to shoulder with the walls; the
        // entrance stays empty so an arriving player is not already in combat.
        std::vector<PlacedBlock const*> roomBlocks;
        SpawnSelectInputs inputs;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId < 0 || b.role == BlockRole::RoomEntrance)
            {
                continue;
            }

            RoomRequest room;
            room.roomIndex = static_cast<int>(roomBlocks.size());
            room.isBoss = b.role == BlockRole::RoomBoss;
            inputs.rooms.push_back(room);
            roomBlocks.push_back(&b);
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
                fallback.picks.assign(static_cast<size_t>(count),
                                      SpawnPick{ PLACEHOLDER_CREATURE, PACK_ROLE_MELEE, 0 });
                spawns.push_back(fallback);
            }
        }

        _roomAlive.assign(roomBlocks.size(), 0);
        _run.roomsTotal = static_cast<uint8>(roomBlocks.size());

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
        for (size_t r = 0; r < roomBlocks.size() && r < spawns.size(); ++r)
        {
            PlacedBlock const& b = *roomBlocks[r];
            bool const isBossRoom = b.role == BlockRole::RoomBoss;
            std::vector<SpawnPick> const& picks = spawns[r].picks;
            int const count = static_cast<int>(picks.size());

            for (int i = 0; i < count; ++i)
            {
                // A small fixed pattern around the block centre. Deliberately
                // NOT random: the same plan must produce the same dungeon, and
                // an unseeded draw here would break that quietly.
                double const angle = 2.0 * 3.14159265358979 * i / count;
                double const du = std::cos(angle) * SPAWN_SPREAD_YD;
                double const dv = std::sin(angle) * SPAWN_SPREAD_YD;

                float x = 0.0f, y = 0.0f, z = 0.0f;
                sPDv2Mgr->BlockToWorld(b.bx, b.by, mid + du, mid + dv, x, y, z);

                // Exactly ON the floor plane. This used to add 0.5 yd "so a
                // creature is not spawned inside the floor" - harmless while
                // gravity would have settled them, but with gravity disabled
                // that offset is a permanent hover (operator report
                // 2026-08-06: mobs stood slightly in the air until a pull and
                // evade walked them onto their home position).
                if (Creature* c = instance->SummonCreature(picks[i].entry,
                                                           Position(x, y, z, 0.0f)))
                {
                    c->SetHomePosition(x, y, z, 0.0f);

                    // Gravity OFF, and this is not cosmetic: the server has no
                    // terrain here, so Map::GetHeight answers INVALID_HEIGHT
                    // and every creature is permanently "above nothing" - they
                    // fall through the platforms the client draws (operator
                    // report 2026-08-06). Disabling gravity pins them to the
                    // floor plane the kit was generated at, which is the only
                    // floor the server knows. Movement is unaffected: the walk
                    // grid drives it and every waypoint carries the same Z.
                    c->SetDisableGravity(true);

                    // The tag is what makes this creature a PDv2 mob for every
                    // other hook in the module. GetDefault here (it creates),
                    // Get everywhere else (it must not).
                    PDv2MobData* tag = c->CustomData.GetDefault<PDv2MobData>(PD_MOB_DATA_KEY);
                    tag->role = picks[i].role;
                    tag->casterSpellId = picks[i].casterSpellId;
                    tag->roomIndex = static_cast<uint32>(r);
                    tag->counted = false;

                    // A boss room's FIRST pick is its boss - PDv2PackMgr.h
                    // states that contract ("every boss room gets exactly one
                    // role-2 entry, drawn fresh"), and it holds even when the
                    // packs have no role-2 member and a trash stand-in takes
                    // the slot. Keying completion on the slot rather than on
                    // the drawn role is what keeps that data state finishable.
                    tag->isRunBoss = isBossRoom && i == 0;
                    if (tag->isRunBoss)
                    {
                        ++_run.bossTotal;
                    }

                    // MEMBERSHIP FIRST, AURA SECOND. The mask is what every
                    // affix hook in the module reads; the spells below are the
                    // look. Writing the mask here - the one site that knows
                    // which mobs the seeded draw picked - is what lets a hook
                    // gate on a bit test instead of on HasAura, which a dispel
                    // or a purge could quietly take away (PDv2Affixes.h).
                    tag->splitDepth = 0;
                    tag->affixMask = picks[i].affixed ? _runAffixMask : uint16(0);

                    // The affixes, cast the way mod-dungeon-challenge casts
                    // them (ApplyAffixToCreature: CastSpell on self, triggered,
                    // DungeonChallenge.cpp:676-768) so a mob wearing one looks
                    // the same in both dungeons. Which mobs are affixed came
                    // out of the SEEDED draw, so nothing about it is stored: a
                    // rebuild of the same plan re-rolls the identical set.
                    if (tag->affixMask)
                    {
                        for (AffixDef const& affix : _runAffixes)
                        {
                            c->CastSpell(c, affix.spellId, true);
                        }
                        ++affixedMobs;
                    }

                    _spawnedGuids.push_back(c->GetGUID());
                    ++_roomAlive[r];
                    ++spawned;
                }
            }
        }
        _run.total = static_cast<uint16>(spawned);

        LOG_INFO(PD_LOG, "PDv2: instance {} on map {} spawned {} creature(s) in {} room(s) "
                         "({} boss) from a {}-block plan, difficulty {} lootMult {}, "
                         "{} mob(s) wearing {} affix(es)",
                 instance->GetInstanceId(), instance->GetId(), spawned,
                 uint32(_run.roomsTotal), uint32(_run.bossTotal),
                 uint32(plan.blocks.size()), uint32(_run.difficulty),
                 uint32(_run.lootMultX100), affixedMobs, uint32(_runAffixes.size()));
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
            LOG_DEBUG(PD_LOG, "PDv2: returned {} to the entrance from Z {}",
                      player->GetName(), player->GetPositionZ());
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
            EvictDisconnected();

            // The clock rides the same one-second tick. It is not marked dirty:
            // the UI polls at 1 Hz anyway, and a flag that ticks on its own
            // would tell a reader "something happened" once a second forever.
            if (_run.started && !_run.complete)
            {
                _run.elapsedSec = GetMSTimeDiffToNow(_run.startedMs) / 1000;
            }

            // The HUD's whole pull side, after the clock so the frame carries
            // the second it was sent in. The link decides whether anything is
            // worth saying; a still dungeon costs nothing on the wire.
            sPDv2UILink->OnInstanceTick(this);
        }
        else
        {
            _fallCheckTimer -= diff;
        }
    }
}
