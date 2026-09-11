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

#include "PDv2Mgr.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "PDDefines.h"
#include "QueryResult.h"
#include "generator/PDv2WalkGrid.h"

#include <algorithm>
#include <cstdio>

namespace PDungeon
{
    namespace
    {
        // Round E / WP10. Does the Endless Storage table exist on THIS realm?
        //
        // SHOW TABLES and not `SELECT 1 FROM custom_endless_storage LIMIT 1`,
        // because the two failures are indistinguishable through the second
        // one: a missing table and an EMPTY table both come back as a null
        // QueryResult, and a fresh realm has an empty storage on the day it is
        // installed. SHOW TABLES answers about the schema and nothing else -
        // one row means the table is there whatever is in it.
        //
        // Asked from LoadConfig, which is safe: World::SetInitialWorldSettings
        // starts the database pools well before it reads the config
        // (World.cpp:318, and LoadDBAllowedSecurityLevel queries three lines
        // later), so this never runs against a pool that is not up.
        bool StorageTableExists()
        {
            return CharacterDatabase.Query(
                       "SHOW TABLES LIKE 'custom_endless_storage'") != nullptr;
        }
    }

    PDv2Mgr* PDv2Mgr::instance()
    {
        static PDv2Mgr mgr;
        return &mgr;
    }

    void PDv2Mgr::LoadConfig()
    {
        _config.enabled = sConfigMgr->GetOption<bool>("ProceduralDungeon.V2.Enable", false);
        _config.mapId = sConfigMgr->GetOption<uint32>("ProceduralDungeon.V2.MapId", 760);
        _config.floorZ = sConfigMgr->GetOption<float>("ProceduralDungeon.V2.FloorZ", 50.0f);
        _config.rooms = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.Rooms", 5);
        _config.bossRooms = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.BossRooms", 1);
        _config.fieldBlocks = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.FieldBlocks", 8);
        _config.originBX = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.OriginBX", 256);
        _config.originBY = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.OriginBY", 256);
        // Round B (B0b): the chance, per boss segment, that a loop room hangs
        // off the spine's corridor run. Clamped into a percent; persisted in
        // the gen_loop_pct column, whose name predates the loop rooms.
        _config.detourChancePct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.DetourChance", 33)));

        // Round B: pocket rooms hanging off the spine. Clamped into [0, 255]
        // because the value is persisted: pdungeon_account.gen_branches is
        // TINYINT UNSIGNED, so an operator typo above it makes SavePlanToDB
        // fail under strict sql_mode and the layout that was just generated is
        // never stored - the same reason the percent above is clamped. The
        // planner's own arithmetic bounds the effective value far lower; the
        // ceiling here only keeps the column writable.
        _config.branches = std::min(255, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Branches", 2)));

        _config.theme = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.Theme", 1);
        _config.manifestPath = sConfigMgr->GetOption<std::string>(
            "ProceduralDungeon.V2.ManifestPath", "");

        _config.xpPerRoom = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.XP.PerRoom", 10);
        _config.xpPerDlvl = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.XP.PerDlvl", 100);
        _config.dlvlCap = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.DlvlCap", 30);
        _config.spawnsPerRoom = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.SpawnsPerRoom", 5);
        _config.bossRoomAdds = sConfigMgr->GetOption<int32>("ProceduralDungeon.V2.BossRoomAdds", 2);
        _config.lootBonusRollPct = sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.BonusRollPct", 15);
        _config.castRangeYd = sConfigMgr->GetOption<float>("ProceduralDungeon.V2.CastRangeYd", 25.0f);
        _config.aggroRangeYd = sConfigMgr->GetOption<float>("ProceduralDungeon.V2.AggroRangeYd", 20.0f);

        // Clamped at 0 on the way in: a negative percentage would make a HARDER
        // dungeon hit softer, and at difficulty 100 it would drive the x100
        // multiplier below zero and underflow the unsigned arithmetic the
        // scaling hooks do. There is no upper clamp - an operator who wants a
        // brutal curve is entitled to one.
        _config.diffHealthPctPerLevel = std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Diff.HealthPctPerLevel", 5));
        _config.diffDamagePctPerLevel = std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Diff.DamagePctPerLevel", 2));

        // Clamped into [0, 100] because it is handed straight to a percent
        // roll: 0 means "no affixed mobs", 100 means "all of them", and there
        // is nothing sensible outside that.
        _config.affixPct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Affix.Percentage", 40)));

        _config.decorEnable = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Decor.Enable", true);

        // Round B / B3: a percent handed straight to a threshold test, so it
        // gets the same [0, 100] clamp the affix share does. 0 opens every
        // barrier on the first kill of its segment, 100 demands all of it.
        _config.barrierPct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Barrier.Pct", 50)));
        // Radians, deliberately unclamped: an orientation is periodic, so
        // there is no value an operator could type that means nothing.
        _config.barrierOrientNS = sConfigMgr->GetOption<float>(
            "ProceduralDungeon.V2.Barrier.OrientNS", 0.0f);
        _config.barrierOrientEW = sConfigMgr->GetOption<float>(
            "ProceduralDungeon.V2.Barrier.OrientEW", 1.5708f);

        // B4: never below 100. The patroller is drawn from the trash pool and
        // is meant to be the hardest thing in the corridor; a multiplier under
        // 100 would make it the softest thing in the dungeon.
        _config.patrolHealthMultPct = std::max(100, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Patrol.HealthMult", 300));

        // Round D / D2: both are compared against the run's 1..100 dial, so
        // they are clamped into that range and into nothing else. The two are
        // deliberately NOT clamped against each other - PDv2Mgr.h says why.
        _config.patrolSize2Diff = std::min(100, std::max(1, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Patrol.Size2Diff", 50)));
        _config.patrolSize3Diff = std::min(100, std::max(1, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Patrol.Size3Diff", 75)));
        // Never below zero: a negative follow distance is not a formation, it
        // is MoveFollow aiming at a point on the far side of the leader. Not
        // clamped from above - a wide file is a look, not a fault.
        _config.patrolFollowDistYd = std::max(0.0f, sConfigMgr->GetOption<float>(
            "ProceduralDungeon.V2.Patrol.FollowDistYd", 3.0f));

        // Round C: off. Every AI diagnostic in PDv2CreatureAI.cpp reads this
        // key on the tick that would log, so `.reload config` both arms and
        // disarms it mid-run - which is the whole point, because the evidence
        // it produces is wanted for one pull and not for the rest of the run.
        _config.patrolDebug = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Patrol.Debug", false);

        // Round E / R3: off, and read exactly like the patrol switch above and
        // for the same reason - every gated line asks PDv2Debug() on the tick
        // that would print it, so `.reload config` arms and disarms the whole
        // per-creature / per-tick / per-verb stream on a run that is already
        // being walked. Two keys rather than one because the two hunts have
        // nothing to do with each other (PDv2Mgr.h says which is which).
        _config.debug = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Debug", false);

        // B5: a percent roll like the ones above, and a mob count the spawn
        // ring can actually seat (0 disarms the ambush without disarming
        // anything else). V2.Ambush.RadiusYd is gone since Round C / C2 - the
        // trigger is the corridor block and has no radius to read.
        _config.ambushChancePct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Ambush.Chance", 50)));
        _config.ambushMobs = std::min(8, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Ambush.Mobs", 4)));
        // Unclamped on purpose: 0 means "no stun at all", and any other id is
        // the operator's choice of spell, which this module must not overrule.
        _config.ambushStunSpell = sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Ambush.StunSpell", 20170);

        // Round E / WP5: the event room. ChancePct is clamped into a percent
        // for both of the reasons V2.Branches is clamped - it IS one, and it is
        // persisted into a TINYINT UNSIGNED column (gen_event_pct) that a typo
        // above 255 would make unwritable, taking the layout that was just
        // generated down with it. It is also the only one of the five read at
        // GENERATION time; the four below are engine-side and read live.
        _config.eventChancePct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Event.ChancePct", 25)));
        // 10..600 s. Under ten seconds the wave timer barely gets its first
        // attacker out and the reward is free; over ten minutes one event
        // outlasts the run it sits in.
        _config.eventDurationSec = std::min(600, std::max(10, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Event.DurationSec", 60)));
        // 1..60 s. One per second is as fast as the 1 Hz tick can spawn, and a
        // gap longer than a minute outlives the longest defence above.
        _config.eventSpawnEverySec = std::min(60, std::max(1, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Event.SpawnEverySec", 5)));
        _config.eventCasterPct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Event.CasterPct", 10)));
        // Unclamped on purpose, exactly like the ambush's stun spell above: the
        // type is already the bound, 0 means "pay no Paragon XP", and what one
        // module pays into another's progression is the operator's call.
        _config.eventParagonXp = sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Event.ParagonXp", 1000);

        // Round E / L2: the five currency items. Clamped to at least 1 because
        // 0 is not an item id and every grant would silently fail on it; a tier
        // is retired with its ChancePct, never by blanking its item. These five
        // reads are the module's only knowledge of a PD item id.
        _config.lootCurrencyItem[0] = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier1.Item", 920105));
        _config.lootCurrencyItem[1] = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier2.Item", 920106));
        _config.lootCurrencyItem[2] = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier3.Item", 920107));
        _config.lootCurrencyItem[3] = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier4.Item", 920108));
        _config.lootCurrencyItem[4] = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier5.Item", 920109));

        // Percent rolls, so the same [0, 100] every other chance here gets: 0
        // retires a tier without touching the other four, 100 is every mob (T1,
        // T2, T3) or every looter (T4, T5), before the room factor scales it.
        _config.lootCurrencyChancePct[0] = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier1.ChancePct", 100)));
        _config.lootCurrencyChancePct[1] = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier2.ChancePct", 5)));
        _config.lootCurrencyChancePct[2] = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier3.ChancePct", 1)));
        _config.lootCurrencyChancePct[3] = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier4.ChancePct", 50)));
        _config.lootCurrencyChancePct[4] = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier5.ChancePct", 10)));

        // Only the two cache tiers are gated, so only those two have a key -
        // slots 0..2 keep the header's 1, the bottom of the dial, which every
        // run clears. Both are clamped into the run's own 1..100 difficulty
        // range, exactly like V2.Patrol.Size2Diff/Size3Diff: a gate outside the
        // dial would be a tier that can never drop or one that is not gated at
        // all, and neither is what the key says it does.
        _config.lootCurrencyMinDiff[3] = std::min(100, std::max(1, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier4.MinDiff", 50)));
        _config.lootCurrencyMinDiff[4] = std::min(100, std::max(1, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.Tier5.MinDiff", 75)));

        // The room factor's two inputs (GameRoomFactorX100, which takes both as
        // parameters precisely so this pair stays the only copy of them). The
        // baseline is a room COUNT and never 0: at 0 every run would count as
        // full and the length of a dungeon would stop paying anything. The
        // per-room bonus is a percent and gets the percent clamp; 0 makes a
        // long run merely as good as the baseline, never worse.
        _config.lootRoomsBaseline = std::max(1, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.RoomsBaseline", 10));
        _config.lootRoomsBonusPctPerRoom = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Currency.RoomsBonusPctPerRoom", 1)));

        _config.lootExtraMobsDropCurrency = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Loot.Currency.ExtraMobsDropCurrency", false);

        // WP8. No clamp to write: a bool has no range to get wrong, and it is
        // read live like every other V2 key, so `.reload config` decides what
        // the NEXT corpse carries without disturbing the run being walked.
        _config.lootNativeItems = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Loot.NativeItems", false);

        // L3. The chance is the on/off switch for materials; the ceiling is a
        // count and gets the [0, 10] count clamp. 0 and 1 both mean one
        // material per mob, because GameMatsMaxCount floors its band at 1 - the
        // way to stop materials dropping is Mats.ChancePct 0.
        _config.lootMatsChancePct = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Mats.ChancePct", 100)));
        _config.lootMatsMaxPerMobAtCap = std::min(10, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Mats.MaxPerMobAtCap", 5)));

        // Round E / WP10: materials into the Endless Storage instead of the
        // bags. The key says what the operator WANTS; the probe below decides
        // whether it is possible, because the table belongs to another module
        // and PDv2 does not require it.
        _config.lootMatsToStorage = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Loot.MatsToStorage", true);
        if (_config.lootMatsToStorage && !StorageTableExists())
        {
            // Off for this session rather than per deposit: without it every
            // material of every kill would be one failed INSERT and one line in
            // the DB error log, on a hot path, for as long as the realm runs.
            // `.reload config` probes again, so an operator who installs
            // mod-endless-storage gets the feature without a restart.
            _config.lootMatsToStorage = false;
            LOG_WARN(PD_LOG, "PDv2: V2.Loot.MatsToStorage is on but the characters "
                             "table `custom_endless_storage` does not exist - materials "
                             "go to the bags. The table belongs to mod-endless-storage; "
                             "install it (or set the key to 0) and `.reload config`");
        }

        // L4. Counts, clamped into [0, 10]: 0 silences that one source and 10
        // is already three times what a difficulty-100 run multiplies it to.
        _config.lootChestItems = std::min(10, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Chest.Items", 1)));
        _config.lootBossItems = std::min(10, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Boss.Items", 1)));
        _config.lootFinalItems = std::min(10, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.Final.Items", 1)));

        // Clamped into [0, V2.DlvlCap] and therefore read AFTER it: the key is
        // a dlvl and a dlvl above the cap is one no account can reach, which
        // would be an ICC switch that never happens. The cap itself is floored
        // at 0 here because V2.DlvlCap is deliberately unclamped above and a
        // negative one must not drag this key below zero.
        int const dlvlCeil = std::max(0, _config.dlvlCap);
        _config.lootIccDlvl = std::min(dlvlCeil, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Loot.IccDlvl", 10)));

        _config.lootClassFilter = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Loot.ClassFilter", true);

        // Round E / R1. The two unlock steps of the account-wide difficulty
        // cap. Clamped into [0, 100] and not into the dial's [1, 100]: 0 is a
        // meaningful setting here - it says that outcome unlocks nothing - and
        // an unlock wider than the dial itself cannot mean anything, because
        // RaiseDiffCap clamps the resulting cap into the dial anyway.
        _config.capDeathUnlock = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Cap.DeathUnlock", 3)));
        _config.capCleanUnlock = std::min(100, std::max(0, sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.Cap.CleanUnlock", 5)));

        // Round E / WP6: the respawn echoes. Engine-side and read LIVE like the
        // four V2.Event keys above, so `.reload config` arms or disarms the
        // Forgotten Talents node on a run that is already being walked - the
        // only way an operator can answer "is THIS what is flooding the room"
        // without a restart.
        _config.respawnEnable = sConfigMgr->GetOption<bool>(
            "ProceduralDungeon.V2.Respawn.Enable", true);
        // Only a ceiling, no floor: the type is unsigned, so 0 is already the
        // bottom and it means "the mechanic runs and pays nothing". Five is
        // more corpses per pull than the L2-L4 loot funnel was ever tuned for,
        // and the node's own maximum is 2 - this clamp is what a content change
        // or a typo in the FT extension contract runs into, not the default.
        _config.respawnMaxCopies = std::min<uint32>(5, sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.Respawn.MaxCopies", 2));

        // Round E / WP10: the finale portal's destination, by game_tele NAME.
        // Not validated here on purpose - the tele store is loaded from the
        // world DB after the first config read, so a lookup at this point
        // would report every name as missing on startup and only ever succeed
        // on `.reload config`. PDExitObjects.cpp resolves it per click and
        // says so loudly when the row is not there.
        _config.finaleTeleName = sConfigMgr->GetOption<std::string>(
            "ProceduralDungeon.V2.Finale.TeleName", "flcapital");

        LOG_INFO(PD_LOG, "PDv2: {} map {} floorZ {} rooms {}+{} field {} origin ({},{}) pockets {} detour {}%",
                 _config.enabled ? "enabled" : "disabled", _config.mapId, _config.floorZ,
                 _config.rooms, _config.bossRooms, _config.fieldBlocks,
                 _config.originBX, _config.originBY, _config.branches, _config.detourChancePct);
        if (_config.enabled && _config.manifestPath.empty())
        {
            LOG_WARN(PD_LOG, "PDv2: ProceduralDungeon.V2.ManifestPath is empty - `.pdungeon v2 gen` "
                             "can plan but cannot hand the manifest to the client");
        }
    }

    bool PDv2Mgr::GeneratePlan(uint32_t accountId, uint32_t seed, BlockPlan& out,
                               int themeOverride)
    {
        PDv2AccountState const state = GetAccountState(accountId);
        int const dlvl = static_cast<int>(state.dlvl);

        BlockCfg cfg;
        cfg.seed = seed;
        // 01 §8: the shape of the dungeon follows the account's progression and
        // its chosen room count, clamped into the band that dlvl unlocked. The
        // server config stays as the fallback for an account with no row, so a
        // fresh install still generates what the operator configured.
        cfg.rooms = state.loaded ? GameClampRooms(state.cfgRooms, dlvl)
                                 : GameClampRooms(_config.rooms, dlvl);
        cfg.bossRooms = state.loaded ? GameBossRooms(dlvl) : _config.bossRooms;
        // fieldBlocks now FOLLOWS the room count downwards, and is still capped
        // by the configured value on the way up.
        //
        // It used to be the config value flat, on the reasoning that 8 blocks
        // is exactly one ADT tile and a multi-tile plan is untested on the
        // client. That ceiling is untouched and still load-bearing - what
        // changed is the floor. Holding the field at 8 for every dlvl scattered
        // three rooms across 64 cells, and the corridors between them became
        // the whole run; the operator's verdict after the first live host run
        // (2026-08-10) was that low levels are mostly walking. Scaling the
        // field with the room count keeps cells-per-room roughly constant, so a
        // small dungeon is small instead of sparse. The room cap
        // (PD_GAME_ROOMS_CAP_MEASURED) still keeps big layouts inside the tile.
        //
        // The field follows the TOTAL room count, bosses included: the layout
        // seats rooms + bossRooms cells, and sizing it from cfg.rooms alone
        // proposed a field that provably cannot hold the plan. A dlvl-30
        // account that picks two rooms runs six rooms (2 + 4 bosses), and six
        // rooms do not exist on the 3x3 field the old expression handed it -
        // MIN_ROOM_GAP 2 admits at most 5 cells on 3x3, so every attempt failed
        // and the player got "no valid layout" (final review of B0, 2026-09-03).
        // Round E / R2 adds the ENTRANCE to that total: since the slider
        // counts ordinary rooms, the plan seats rooms + bossRooms + 1 cells,
        // and a field sized one cell short is the same provable misfit this
        // paragraph was written about. The live default is unmoved:
        // 5 + 1 + 1 = 7 rooms still asks for 6x6, capped at V2.FieldBlocks.
        cfg.fieldBlocks = std::min(_config.fieldBlocks,
                                   GameFieldBlocksForRooms(cfg.rooms + cfg.bossRooms + 1));
        cfg.detourChancePct = _config.detourChancePct;
        cfg.branches = _config.branches;
        // Round E / WP5: a layout input like the two above it, so the value the
        // plan was generated with is the one SavePlanToDB stores (gen_event_pct).
        cfg.eventChancePct = _config.eventChancePct;
        cfg.originBX = _config.originBX;
        cfg.originBY = _config.originBY;
        cfg.theme = themeOverride ? themeOverride : _config.theme;

        if (!GenerateBlockPlan(cfg, &out))
        {
            LOG_ERROR(PD_LOG, "PDv2: no valid layout for seed {} after {} tries", seed, cfg.maxTries);
            return false;
        }

        StorePlan(accountId, out);
        SavePlanToDB(accountId, out);
        return true;
    }

    void PDv2Mgr::StorePlan(uint32_t accountId, BlockPlan const& plan)
    {
        // A fresh immutable object every time, replaced under the lock: readers
        // that fetched the previous shared_ptr keep a complete, never-mutated
        // plan for as long as they hold it. That is the whole fix for the
        // pointer-into-the-map pattern this replaced - a re-roll can no longer
        // pull the plan out from under a map thread mid-read.
        auto fresh = std::make_shared<BlockPlan const>(plan);
        std::lock_guard<std::mutex> guard(_lock);
        _plans[accountId] = std::move(fresh);
    }

    void PDv2Mgr::SavePlanToDB(uint32_t accountId, BlockPlan const& plan)
    {
        BlockCfg const& cfg = plan.config;
        PDv2AccountState const state = GetAccountState(accountId);
        std::string packs = state.cfgPacks;
        CharacterDatabase.EscapeString(packs);

        // Layout columns only on UPDATE - dlvl/dxp and the cfg_* gameplay knobs
        // belong to the gameplay slice, and a reroll must never clobber them.
        //
        // The cfg_* values appear in the INSERT half all the same, and only
        // there: when this statement CREATES the row it would otherwise write
        // the column defaults, and cfg_mob_level_min's default of 1 selects no
        // pack at all against v1's level-80 stock. Seeding the row from the
        // cached state keeps a first `v2 gen` from silently disagreeing with
        // the state the server has been using since login.

        // gen_loop_pct carries V2.DetourChance since B0b; gen_event_pct carries
        // V2.Event.ChancePct since Round E / WP5
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, theme, layout_seed, layout_version, "
            "gen_rooms, gen_boss_rooms, gen_field_blocks, gen_origin_bx, gen_origin_by, "
            "gen_loop_pct, gen_branches, gen_event_pct, cfg_rooms, cfg_difficulty, "
            "cfg_caster_pct, cfg_mob_level_min, cfg_stat_profile, cfg_packs) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}') "
            "ON DUPLICATE KEY UPDATE theme = VALUES(theme), "
            "layout_seed = VALUES(layout_seed), layout_version = VALUES(layout_version), "
            "gen_rooms = VALUES(gen_rooms), gen_boss_rooms = VALUES(gen_boss_rooms), "
            "gen_field_blocks = VALUES(gen_field_blocks), gen_origin_bx = VALUES(gen_origin_bx), "
            "gen_origin_by = VALUES(gen_origin_by), gen_loop_pct = VALUES(gen_loop_pct), "
            "gen_branches = VALUES(gen_branches), gen_event_pct = VALUES(gen_event_pct)",
            accountId, cfg.theme, cfg.seed, PD_LAYOUT_VERSION, cfg.rooms, cfg.bossRooms,
            cfg.fieldBlocks, cfg.originBX, cfg.originBY, cfg.detourChancePct, cfg.branches,
            cfg.eventChancePct, state.cfgRooms, state.cfgDifficulty, state.cfgCasterPct,
            state.cfgBandMin, uint32(state.cfgStatProfile), packs);
    }

    void PDv2Mgr::LoadAccountState(uint32_t accountId)
    {
        {
            std::lock_guard<std::mutex> guard(_lock);
            if (_accounts.find(accountId) != _accounts.end())
            {
                return;
            }
        }

        PDv2AccountState state;
        QueryResult result = CharacterDatabase.Query(
            "SELECT dlvl, dxp, cfg_rooms, cfg_difficulty, cfg_caster_pct, cfg_mob_level_min, "
            "cfg_packs, diff_cap, cfg_stat_profile FROM pdungeon_account WHERE accountId = {}",
            accountId);
        if (result)
        {
            Field* fields = result->Fetch();
            state.dlvl = fields[0].Get<uint32>();
            state.dxp = fields[1].Get<uint32>();
            state.cfgRooms = fields[2].Get<uint8>();
            state.cfgDifficulty = fields[3].Get<uint8>();
            state.cfgCasterPct = fields[4].Get<uint8>();
            state.cfgBandMin = fields[5].Get<uint8>();
            state.cfgPacks = fields[6].Get<std::string>();
            // Round E / R1. Appended to the SELECT rather than slotted in
            // beside cfg_difficulty, because every index below it is a
            // positional read of this one statement and renumbering them buys
            // nothing but a chance to get one wrong.
            state.diffCap = fields[7].Get<uint8>();
            // Round E / WP9, appended for exactly the same reason as diff_cap
            // above: the tail of this SELECT is where a new column goes, and
            // renumbering the seven reads above it buys nothing.
            state.cfgStatProfile = fields[8].Get<uint8>();
            state.loaded = true;
        }

        // Clamp on the way IN, not only on the way out. A row edited by hand,
        // or written before a band or grid rule changed, must not be able to
        // put an illegal value into a live dungeon - and dlvl in particular
        // decides what the other clamps even allow.
        if (state.dlvl > static_cast<uint32_t>(_config.dlvlCap > 0 ? _config.dlvlCap : 0))
        {
            state.dlvl = static_cast<uint32_t>(_config.dlvlCap > 0 ? _config.dlvlCap : 0);
        }
        int const dlvl = static_cast<int>(state.dlvl);
        state.cfgRooms = GameClampRooms(state.cfgRooms, dlvl);
        // No dlvl argument any more: the dial is open from the first run, so
        // the only illegal difficulty is one outside [1, 100].
        state.cfgDifficulty = GameClampDiff(state.cfgDifficulty);
        // Round E / R1. The cap gets the same clamp for the same reason - a
        // hand-edited 0 or 200 must not become a live ceiling - and then bounds
        // the dial itself. Order matters: the cap has to be legal before it can
        // be used as a limit, and cfgDifficulty has to have passed its own
        // clamp before this one narrows it further. A row where the player's
        // chosen difficulty sits above the cap is not a fault to log about: it
        // is exactly what the pre-Round-E rows look like, and what a GM
        // `.pdungeon v2 cap` lowering leaves behind.
        state.diffCap = GameClampDiff(state.diffCap);
        state.cfgDifficulty = std::min(state.cfgDifficulty, state.diffCap);
        state.cfgCasterPct = GameClampCasterPct(state.cfgCasterPct);
        state.cfgBandMin = GameClampBandMin(state.cfgBandMin);
        // Round E / WP9. The column is a TINYINT an operator can edit and the
        // value crosses a wire from a client panel, so it gets the same
        // treatment as every knob above it. Note what is NOT re-checked here:
        // whether the account may HAVE a profile at all. That is the FT node,
        // it is per character, and a stored 2 on an account whose characters
        // all refunded it simply never bites (PDv2LootMgr::StatProfileFor) -
        // storing it is not the same as honouring it.
        state.cfgStatProfile = GameClampStatProfile(state.cfgStatProfile);

        std::lock_guard<std::mutex> guard(_lock);
        _accounts[accountId] = state;
    }

    PDv2AccountState PDv2Mgr::GetAccountState(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _accounts.find(accountId);
        return it == _accounts.end() ? PDv2AccountState() : it->second;
    }

    void PDv2Mgr::SetAccountCfg(uint32_t accountId, PDv2AccountState const& cfg)
    {
        std::lock_guard<std::mutex> guard(_lock);
        PDv2AccountState& state = _accounts[accountId];
        int const dlvl = static_cast<int>(state.dlvl);
        state.cfgRooms = GameClampRooms(cfg.cfgRooms, dlvl);
        // Round E / R1: the dial is bounded by the account's own cap on top of
        // its 1..100 clamp. `state.diffCap` and NOT `cfg.diffCap` on purpose -
        // the cap is progression, not a setting, and this entry point is fed
        // by player-facing commands and the UI link. Letting a caller hand in
        // a cap here would make every one of them a way around it; RaiseDiffCap
        // is the only door, and it only opens upwards.
        state.cfgDifficulty = std::min(GameClampDiff(cfg.cfgDifficulty), state.diffCap);
        state.cfgCasterPct = GameClampCasterPct(cfg.cfgCasterPct);
        state.cfgBandMin = GameClampBandMin(cfg.cfgBandMin);
        // Round E / WP9, and NO unlock parameter beside it on purpose: this
        // function clamps, it does not authorise. Whether the player owns
        // Discerning Eye at all is decided by the SET handler in
        // PDv2UILink.cpp before it ever calls here - the same shape the locked
        // band row has - so that every future caller of SetAccountCfg cannot
        // accidentally become a way around the node.
        state.cfgStatProfile = GameClampStatProfile(cfg.cfgStatProfile);
        state.cfgPacks = cfg.cfgPacks;
        state.loaded = true;
    }

    void PDv2Mgr::SaveAccountCfg(uint32_t accountId)
    {
        PDv2AccountState const state = GetAccountState(accountId);
        std::string packs = state.cfgPacks;
        // cfg_packs is the one free-text column here and it reaches this
        // statement from a player-facing command, so it is escaped rather than
        // trusted - the module's format-style Execute does not quote for us.
        CharacterDatabase.EscapeString(packs);

        // cfg_* columns only, the mirror image of SavePlanToDB: a settings
        // change must never touch progression or the stored layout.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, cfg_rooms, cfg_difficulty, "
            "cfg_caster_pct, cfg_mob_level_min, cfg_stat_profile, cfg_packs) "
            "VALUES ({}, {}, {}, {}, {}, {}, '{}') "
            "ON DUPLICATE KEY UPDATE cfg_rooms = VALUES(cfg_rooms), "
            "cfg_difficulty = VALUES(cfg_difficulty), cfg_caster_pct = VALUES(cfg_caster_pct), "
            "cfg_mob_level_min = VALUES(cfg_mob_level_min), "
            "cfg_stat_profile = VALUES(cfg_stat_profile), cfg_packs = VALUES(cfg_packs)",
            accountId, state.cfgRooms, state.cfgDifficulty, state.cfgCasterPct,
            state.cfgBandMin, uint32(state.cfgStatProfile), packs);
    }

    int PDv2Mgr::RaiseDiffCap(uint32_t accountId, int wanted)
    {
        int cap = 0;
        {
            std::lock_guard<std::mutex> guard(_lock);
            PDv2AccountState& state = _accounts[accountId];
            // The ratchet, and it lives HERE rather than at the call site: the
            // caller computes "difficulty + unlock" from the run it just
            // finished and cannot know whether some other session raised the
            // cap in the meantime, so a value below the cached cap has to be a
            // silent no-op instead of a downgrade.
            state.diffCap = std::max(state.diffCap, GameClampDiff(wanted));
            cap = state.diffCap;
            // Deliberately NOT touching cfgDifficulty: raising the ceiling
            // never moves the dial the player set under it. The cap only
            // bounds the next choice, and SetAccountCfg applies that bound.
            state.loaded = true;
        }

        // diff_cap only - the disjoint-writer rule this row is built on
        // (SavePlanToDB owns the layout, SaveAccountCfg the cfg_*, and
        // GrantRunReward dlvl/dxp). The INSERT half names the columns this
        // writer owns and lets the column defaults speak for the rest, exactly
        // as GrantRunReward does; on the path that matters the row always
        // exists by now, because a finished run means a stored layout, which
        // means SavePlanToDB has already seeded the cfg_* columns from the
        // cached state.
        //
        // GREATEST on the UPDATE half so the ratchet holds in the DATABASE too,
        // and not only in this process's cache: two worldservers on one
        // characters DB, or a cache built before a manual edit, must not be
        // able to write a cap backwards. VALUES(diff_cap) rather than the bound
        // parameter is the SaveAccountCfg idiom - one value, named once.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, diff_cap) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE diff_cap = GREATEST(diff_cap, VALUES(diff_cap))",
            accountId, cap);

        return cap;
    }

    int PDv2Mgr::SetDiffCap(uint32_t accountId, int wanted)
    {
        int const cap = GameClampDiff(wanted);
        {
            std::lock_guard<std::mutex> guard(_lock);
            PDv2AccountState& state = _accounts[accountId];
            // Assignment, not std::max: this is the one door that may close.
            // Everything the ratchet's comment says about not trusting a
            // caller's number still applies to the VALUE - it is clamped into
            // the dial above - but not to its DIRECTION, which is the whole
            // point of the entry point existing.
            state.diffCap = cap;
            // `loaded` for the same reason RaiseDiffCap sets it: a cap set
            // before this account ever had a row is still a real answer, and
            // GeneratePlan must follow it rather than the server config.
            state.loaded = true;
        }

        // No GREATEST on the UPDATE half, and that is the ONLY difference
        // from RaiseDiffCap's statement: the ratchet is enforced in the
        // database precisely so that no gameplay path can write a cap
        // backwards, and this path is not a gameplay path. Still diff_cap
        // alone - a test tool is not a licence to clobber the neighbouring
        // columns.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, diff_cap) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE diff_cap = VALUES(diff_cap)",
            accountId, cap);

        return cap;
    }

    PDv2RunReward PDv2Mgr::GrantRunReward(uint32_t accountId, int roomsUsed)
    {
        PDv2RunReward reward;
        uint32_t dxp = 0;

        {
            std::lock_guard<std::mutex> guard(_lock);
            PDv2AccountState& state = _accounts[accountId];
            uint32_t const oldDlvl = state.dlvl;

            reward.dxpGained = GameRunDxp(roomsUsed, _config.xpPerRoom);
            // Saturating add. 4 billion dxp is not reachable in play, but a
            // wrap would hand the account a dlvl reset it did not earn, and
            // that is not a bug anyone would think to look for.
            state.dxp = (state.dxp > UINT32_MAX - reward.dxpGained)
                            ? UINT32_MAX
                            : state.dxp + reward.dxpGained;
            dxp = state.dxp;
            state.dlvl = static_cast<uint32_t>(
                GameDlvlFromDxp(dxp, _config.xpPerDlvl, _config.dlvlCap));
            state.loaded = true;

            reward.newDlvl = static_cast<int>(state.dlvl);
            reward.leveledUp = state.dlvl > oldDlvl;
        }

        // dlvl and dxp only - the cfg_* knobs and the whole layout half of the
        // row have their own writers, and a reward must not speak for them.
        CharacterDatabase.Execute(
            "INSERT INTO pdungeon_account (accountId, dlvl, dxp) VALUES ({}, {}, {}) "
            "ON DUPLICATE KEY UPDATE dlvl = VALUES(dlvl), dxp = VALUES(dxp)",
            accountId, reward.newDlvl, dxp);

        return reward;
    }

    void PDv2Mgr::LoadPlanFromDB(uint32_t accountId)
    {
        if (GetPlan(accountId))
        {
            return;
        }

        QueryResult result = CharacterDatabase.Query(
            "SELECT layout_seed, layout_version, theme, gen_rooms, gen_boss_rooms, "
            "gen_field_blocks, gen_origin_bx, gen_origin_by, gen_loop_pct, gen_branches, "
            "gen_event_pct "
            "FROM pdungeon_account WHERE accountId = {}", accountId);
        if (!result)
        {
            return;
        }

        Field* fields = result->Fetch();
        uint32_t const seed = fields[0].Get<uint32>();
        uint32_t const version = fields[1].Get<uint32>();
        if (!seed)
        {
            return;
        }
        // A foreign version means the stored inputs no longer describe the
        // dungeon this generator would build from them, so the row is kept and
        // the account rerolls on its next `v2 gen`. What makes a row written
        // yesterday foreign is v5: `gen_event_pct` joined the generation inputs,
        // and a v4 row would otherwise come back at the column default of 0 - an
        // eventless dungeon, silently and for ever (PDv2Mgr.h, the ladder).
        if (version != PD_LAYOUT_VERSION)
        {
            LOG_INFO(PD_LOG, "PDv2: account {} has a layout stamped v{} (current v{}) - "
                             "kept in the DB, but a reroll is needed",
                     accountId, version, PD_LAYOUT_VERSION);
            return;
        }

        BlockCfg cfg;
        cfg.seed = seed;
        cfg.theme = fields[2].Get<uint8>();
        cfg.rooms = fields[3].Get<uint8>();
        cfg.bossRooms = fields[4].Get<uint8>();
        cfg.fieldBlocks = fields[5].Get<uint8>();
        cfg.originBX = fields[6].Get<uint16>();
        cfg.originBY = fields[7].Get<uint16>();
        cfg.detourChancePct = fields[8].Get<uint8>();
        cfg.branches = fields[9].Get<uint8>();
        cfg.eventChancePct = fields[10].Get<uint8>();

        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
        {
            // Determinism makes this near-impossible for a layout that once
            // generated; if it happens the generator changed without a
            // PD_LAYOUT_VERSION bump, and that is worth shouting about.
            LOG_ERROR(PD_LOG, "PDv2: account {}'s stored layout (seed {}) no longer "
                              "regenerates - PD_LAYOUT_VERSION should have been bumped",
                      accountId, seed);
            return;
        }

        StorePlan(accountId, plan);
        LOG_INFO(PD_LOG, "PDv2: restored account {}'s dungeon from seed {} ({} blocks)",
                 accountId, seed, uint32(plan.blocks.size()));
    }

    std::shared_ptr<BlockPlan const> PDv2Mgr::GetPlan(uint32_t accountId) const
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _plans.find(accountId);
        return it == _plans.end() ? nullptr : it->second;
    }

    bool PDv2Mgr::WriteManifest(BlockPlan const& plan, uint32_t seq, std::string& pathOut,
                                std::string& error) const
    {
        if (_config.manifestPath.empty())
        {
            error = "ProceduralDungeon.V2.ManifestPath is not set";
            return false;
        }

        std::string const text = EmitManifest(plan, static_cast<int>(seq));

        // Binary mode on purpose: the manifest is LF separated and both parsers
        // reject CR, but a text-mode stream on Windows rewrites every \n.
        FILE* fh = std::fopen(_config.manifestPath.c_str(), "wb");
        if (!fh)
        {
            error = "cannot open " + _config.manifestPath + " for writing";
            return false;
        }
        size_t const written = std::fwrite(text.data(), 1, text.size(), fh);
        std::fclose(fh);
        if (written != text.size())
        {
            error = "short write to " + _config.manifestPath;
            return false;
        }

        pathOut = _config.manifestPath;
        return true;
    }

    void PDv2Mgr::BlockToWorld(int bx, int by, double u, double v,
                               float& x, float& y, float& z) const
    {
        // The math lives in generator/PDv2WorldMath.h beside its inverse, so
        // the harness can prove the two agree - see the header's rationale.
        double wx = 0.0, wy = 0.0;
        BlockLocalToWorld(bx, by, u, v, wx, wy);
        x = static_cast<float>(wx);
        y = static_cast<float>(wy);
        z = _config.floorZ;
    }

    void PDv2Mgr::LoadChunkMeta()
    {
        _walkMasks.clear();
        _chunkPatrol.clear();
        _chunkAnchors.clear();
        _chunkRoomAnchors.clear();
        _chunkProps.clear();

        // Highest kit version wins per chunk id: rows are read in ascending
        // kitVersion order and later ones overwrite.
        //
        // ALL themes load, not just the configured one: `V2.Theme` steers only
        // NEW generations, while a stored dungeon regenerates with the theme
        // frozen into its account row - flipping the config must never leave
        // an old dungeon without masks (the "0 masks = mobs stand still"
        // failure, per dungeon). Chunk ids are globally unique across themes
        // by the kit id scheme, so one map holds them all.
        //
        // The anchors ride along in the same row as the mask on purpose: they
        // describe the same block, and reading them from a second query - or
        // worse, a second file - is how a kit regeneration ends up half
        // applied.
        //
        // Round D / D2's three clearance columns are asked for ONLY when they
        // exist. `mod_pdungeon_chunk_meta.sql` adds them with an
        // information_schema-guarded ALTER of its own, so on a server whose
        // updater has run they are always there - but a server with SQL updates
        // switched off, or one that has not restarted since the kit was
        // regenerated, still has to load its walk masks. Naming a missing
        // column would fail the WHOLE query and leave the dungeon with no grid
        // at all ("0 masks = mobs stand still"), which is a far worse outcome
        // than patrols walking cell centres for one more restart. The probe is
        // one row at startup, and it is the only second query in this function
        // - the DATA still comes out of a single row per chunk.
        bool hasPatrolLayer = false;
        if (QueryResult probe = WorldDatabase.Query(
                "SELECT COUNT(*) FROM information_schema.COLUMNS "
                "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'pdungeon_chunk_meta' "
                "AND COLUMN_NAME IN ('patrolClear', 'patrolDu', 'patrolDv')"))
        {
            hasPatrolLayer = probe->Fetch()[0].Get<uint64>() == 3;
        }

        std::string query =
            "SELECT chunkId, kitVersion, walkMask, anchors, theme";
        if (hasPatrolLayer)
        {
            query += ", patrolClear, patrolDu, patrolDv";
        }
        query += " FROM pdungeon_chunk_meta ORDER BY kitVersion";

        QueryResult result = WorldDatabase.Query(query);
        if (!result)
        {
            LOG_ERROR(PD_LOG, "PDv2: pdungeon_chunk_meta has no rows - "
                              "mod_pdungeon_chunk_meta.sql was not applied, and no "
                              "walk grid can be built (creatures will not chase)");
            return;
        }

        uint32 bad = 0;
        uint32 configThemeRows = 0;
        do
        {
            Field* fields = result->Fetch();
            int const chunkId = static_cast<int>(fields[0].Get<uint32>());
            std::string const rle = fields[2].Get<std::string>();
            if (static_cast<int>(fields[4].Get<uint8>()) == _config.theme)
            {
                ++configThemeRows;
            }

            std::vector<uint8_t> mask;
            if (!DecodeWalkMaskRle(rle, mask) ||
                mask.size() != PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK)
            {
                LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed walkMask ('{}')",
                          chunkId, rle);
                ++bad;
                continue;
            }

            auto& slot = _walkMasks[chunkId];
            std::copy(mask.begin(), mask.end(), slot.begin());

            // Round D / D2, the clearance layer. ALL THREE OR NONE: a clearance
            // without its offset is a number nobody can walk to, and half a
            // layer would send patrols to cell centres it had already decided
            // were tight. A NULL, an empty string or a blob of the wrong length
            // therefore leaves the chunk out of _chunkPatrol entirely, which
            // BuildWalkGrid reads as "every walkable cell is free" - today's
            // behaviour, and the right answer for theme 1, which places no
            // facades at all.
            if (hasPatrolLayer)
            {
                std::vector<uint8_t> layer[3];
                bool ok = true;
                for (int i = 0; i < 3 && ok; ++i)
                {
                    Field const& f = fields[5 + i];
                    if (f.IsNull())
                    {
                        ok = false;
                        break;
                    }
                    std::string const text = f.Get<std::string>();
                    if (text.empty())
                    {
                        ok = false;
                        break;
                    }
                    ok = DecodeWalkMaskRle(text, layer[i]) &&
                         layer[i].size() == PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK;
                    if (!ok)
                    {
                        LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed patrol clearance "
                                          "column {} - that chunk's cells are read as free",
                                  chunkId, i);
                    }
                }
                if (ok)
                {
                    auto& p = _chunkPatrol[chunkId];
                    std::copy(layer[0].begin(), layer[0].end(), p.clear.begin());
                    std::copy(layer[1].begin(), layer[1].end(), p.du.begin());
                    std::copy(layer[2].begin(), layer[2].end(), p.dv.begin());
                }
                else
                {
                    // A row that overwrites a lower kitVersion has to overwrite
                    // its layer too, or a chunk would keep the clearance of a
                    // kit it no longer is.
                    _chunkPatrol.erase(chunkId);
                }
            }

            // A chunk with no anchors is ordinary - every corridor variant has
            // none - so an empty list is stored rather than nothing, and only
            // a malformed one is worth a line. It costs the decor planner its
            // clearance check for that chunk and nothing else.
            std::string const anchorText = fields[3].Get<std::string>();
            std::vector<DecorAnchor> anchors;
            if (!DecodeAnchorList(anchorText, anchors))
            {
                LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed anchors field ('{}')",
                          chunkId, anchorText);
                anchors.clear();
            }
            _chunkAnchors[chunkId] = std::move(anchors);

            // Round B / B1: the same text, decoded a second time with the
            // KINDS kept. Not derived from the flat list - that one has
            // deliberately thrown the kinds away - and stored for every chunk
            // so a corridor answers an empty RoomAnchors rather than nullptr
            // for the wrong reason. A malformed blob is reported and whatever
            // the scanner got is stored as it stands - every reader gates on
            // the has* flags, so a half-decoded row degrades to "no entry for
            // that chunk" exactly like a missing anchor.
            RoomAnchors typed;
            if (!DecodeRoomAnchors(anchorText, typed))
            {
                LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed typed anchors field", chunkId);
            }
            _chunkRoomAnchors[chunkId] = std::move(typed);

            // The structural props ride the same column. A malformed list is
            // reported and dropped like a malformed anchor list - the block
            // simply stands undecorated, nothing else depends on it.
            std::vector<KitProp> props;
            if (!DecodePropList(anchorText, props))
            {
                LOG_ERROR(PD_LOG, "PDv2: chunk {} has a malformed props field ('{}')",
                          chunkId, anchorText);
                props.clear();
            }
            if (!props.empty())
            {
                _chunkProps[chunkId] = std::move(props);
            }
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} walk mask(s) from pdungeon_chunk_meta "
                         "across all themes ({} for configured theme {}, {} malformed), "
                         "{} with a patrol clearance layer",
                 uint32(_walkMasks.size()), configThemeRows, _config.theme, bad,
                 uint32(_chunkPatrol.size()));
        if (!hasPatrolLayer)
        {
            // Not an error: the module works without it, patrols simply walk
            // cell centres. But it IS the difference between "the fix is in"
            // and "the fix is compiled in and doing nothing", so it says so.
            LOG_INFO(PD_LOG, "PDv2: pdungeon_chunk_meta has no patrol clearance columns - "
                             "patrols walk cell centres until the kit v38 SQL is applied");
        }
        if (configThemeRows == 0)
        {
            LOG_ERROR(PD_LOG, "PDv2: configured theme {} has NO chunk-meta rows - "
                              "new generations will fail until the kit ships that "
                              "theme or V2.Theme points at one it has",
                      _config.theme);
        }
    }

    uint8_t const* PDv2Mgr::WalkMaskFor(int chunkId) const
    {
        auto it = _walkMasks.find(chunkId);
        return it == _walkMasks.end() ? nullptr : it->second.data();
    }

    PatrolLayers PDv2Mgr::PatrolLayersFor(int chunkId) const
    {
        auto it = _chunkPatrol.find(chunkId);
        if (it == _chunkPatrol.end())
        {
            return PatrolLayers{};      // three nulls = every cell free
        }
        return PatrolLayers{ it->second.clear.data(), it->second.du.data(),
                             it->second.dv.data() };
    }

    std::vector<DecorAnchor> const* PDv2Mgr::AnchorsFor(int chunkId) const
    {
        auto it = _chunkAnchors.find(chunkId);
        return it == _chunkAnchors.end() ? nullptr : &it->second;
    }

    RoomAnchors const* PDv2Mgr::RoomAnchorsFor(int chunkId) const
    {
        auto it = _chunkRoomAnchors.find(chunkId);
        return it == _chunkRoomAnchors.end() ? nullptr : &it->second;
    }

    std::vector<KitProp> const* PDv2Mgr::PropsFor(int chunkId) const
    {
        auto it = _chunkProps.find(chunkId);
        return it == _chunkProps.end() ? nullptr : &it->second;
    }

    void PDv2Mgr::LoadDecorRules()
    {
        _decorRules.clear();

        // Every theme's rules, ascending id: the planner filters by the plan's
        // own theme, so an account on a second theme is one config value away
        // rather than one restart. ORDER BY id is the fixed iteration order the
        // determinism promise rests on - see PDv2DecorPlan.h.
        QueryResult result = WorldDatabase.Query(
            "SELECT id, theme, roleFilter, goEntry, placement, minPerBlock, "
            "maxPerBlock, weight, minSpacingYd FROM pdungeon_decor_rules ORDER BY id");
        if (!result)
        {
            LOG_INFO(PD_LOG, "PDv2: pdungeon_decor_rules has no rows - dungeons "
                             "will be built without props");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            DecorRule rule;
            rule.id = static_cast<int>(fields[0].Get<uint32>());
            rule.theme = fields[1].Get<uint8>();
            rule.roleFilter = fields[2].Get<std::string>();
            rule.goEntry = static_cast<int>(fields[3].Get<uint32>());
            rule.placement = fields[4].Get<std::string>();
            rule.minPerBlock = fields[5].Get<uint8>();
            rule.maxPerBlock = fields[6].Get<uint8>();
            rule.weight = static_cast<int>(fields[7].Get<uint32>());
            rule.minSpacingYd = fields[8].Get<float>();

            if (rule.placement != DECOR_PLACEMENT_WALL_FOOT &&
                rule.placement != DECOR_PLACEMENT_CORNER &&
                rule.placement != DECOR_PLACEMENT_SCATTER)
            {
                // Kept in the list all the same: the planner skips it by the
                // same test, and dropping it here would hide a typo that the
                // operator can only find by counting props.
                LOG_ERROR(PD_LOG, "PDv2: decor rule {} asks for placement '{}', which "
                                  "no planner implements - it will place nothing",
                          rule.id, rule.placement);
            }

            _decorRules.push_back(std::move(rule));
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} decor rule(s) from pdungeon_decor_rules",
                 uint32(_decorRules.size()));
    }

    void PDv2Mgr::LoadCritterRules()
    {
        _critterRules.clear();

        // Ambient life, ascending id: BuildCritterPlan's draw sequence follows
        // this order, so ORDER BY id is the fixed iteration order the
        // determinism promise rests on - see PDv2DecorPlan.h.
        QueryResult result = WorldDatabase.Query(
            "SELECT id, theme, roleFilter, creatureEntry, minPerBlock, "
            "maxPerBlock, weight FROM pdungeon_critter_rules ORDER BY id");
        if (!result)
        {
            LOG_INFO(PD_LOG, "PDv2: pdungeon_critter_rules has no rows - dungeons "
                             "will be built without ambient life");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            CritterRule rule;
            rule.id = static_cast<int>(fields[0].Get<uint32>());
            rule.theme = fields[1].Get<uint8>();
            rule.roleFilter = fields[2].Get<std::string>();
            rule.creatureEntry = static_cast<int>(fields[3].Get<uint32>());
            rule.minPerBlock = fields[4].Get<uint8>();
            rule.maxPerBlock = fields[5].Get<uint8>();
            rule.weight = static_cast<int>(fields[6].Get<uint32>());
            _critterRules.push_back(std::move(rule));
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} critter rule(s) from pdungeon_critter_rules",
                 uint32(_critterRules.size()));
    }

    bool PDv2Mgr::EntranceWorldPos(BlockPlan const& plan, float& x, float& y, float& z) const
    {
        if (plan.entranceIndex < 0 ||
            plan.entranceIndex >= static_cast<int>(plan.blocks.size()))
        {
            return false;
        }
        PlacedBlock const& b = plan.blocks[static_cast<size_t>(plan.entranceIndex)];
        double const mid = PD_BLOCK_SIZE_YD / 2.0;
        BlockToWorld(b.bx, b.by, mid, mid, x, y, z);
        return true;
    }
}
