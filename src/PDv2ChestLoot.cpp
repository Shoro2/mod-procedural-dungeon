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

#include "DataMap.h"
#include "GameObject.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "PDDefines.h"
#include "PDv2InstanceScript.h"
#include "PDv2LootMgr.h"
#include "PDv2Mgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "Unit.h"
#include "generator/PDv2GameMath.h"

#include <string_view>
#include <vector>

// PDv2 chest loot: Round E / L4 (gear) and L5 (the legacy rares).
//
// Both PDv2 chests KEEP their gameobject_loot_template rows - the Shifting
// Cache its stock table, Chromie's Cache the five FL mats that
// mod_pdungeon_chromie.sql now writes at 25 % - and this file ADDS to that,
// out of the same pools and the same rolls a kill uses (PDv2LootMgr). The
// split is not arbitrary: a template row is data an operator retunes with one
// SQL statement, while the gear has to know the run's dlvl, difficulty and
// room factor, and no loot table can be told those.
//
// WHY GO_ACTIVATED IS THE MOMENT. Player::SendLoot's gameobject branch runs in
// this order (Player.cpp:7860-7930 and 8200-8205):
//
//   :7860  loot = &go->loot;
//   :7867  if (go->getLootState() == GO_READY)     <- only then is it filled
//   :7893      loot->FillLoot(lootid, LootTemplates_Gameobject, ...)
//   :7930      go->SetLootState(GO_ACTIVATED, this)
//   :8200  WorldPacket data(SMSG_LOOT_RESPONSE, (9 + 50));
//   :8203  data << LootView(*loot, this, permission);
//   :8205  SendDirectMessage(&data);
//
// GameObject::SetLootState (GameObject.cpp:2462-2472) calls
// sScriptMgr->OnGameObjectLootStateChanged from inside :7930, so an item added
// here is in go->loot BEFORE the LootView built at :8203 reads it. The player
// opens ONE window holding the template rows and our rolls together - no
// second packet, no re-open, and nothing for the client to reconcile.
//
// The :7867 gate is also why re-filling is not our problem: the template loot
// is only (re)generated while the state is GO_READY, and both chests are
// Data3 = 1 (consumable), so a second open never refills. The `injected` flag
// guards the other half - a chest that reaches GO_ACTIVATED twice (a restock,
// a GM, a scripted re-open) must not roll a second set of gear - and it is set
// BEFORE the first roll, so an early return anywhere below still leaves the
// chest spent.
namespace
{
    using namespace PDungeon;

    // The gear pools by name, as scripts/106_pd_loot_pools.py writes them.
    // Named here rather than in PDv2LootMgr.h for the reason
    // PDv2InstanceScript.cpp's LOOT_POOL_BOSS gives: the pools are DATA, and
    // which name a source asks for is the engine's whole share of them.
    //
    // The low band is a UNION, never a choice between two pools: HC5_EPIC +
    // RAID_N is one reward tier, and picking a pool first would pay the
    // 158-row pool as often as the 2 235-row one (PDv2LootMgr.h:125-131).
    std::string_view const LOOT_POOL_CHEST_LOW_A = "HC5_EPIC";
    std::string_view const LOOT_POOL_CHEST_LOW_B = "RAID_N";
    std::string_view const LOOT_POOL_ICC_N = "ICC_N";
    std::string_view const LOOT_POOL_ICC_HC = "ICC_HC";

    // Round E / L2. The two currency tiers that belong to the FINALE, indices
    // 3 and 4 of the conf's five. T1-T3 are the kill tiers and stop at
    // LOOT_CURRENCY_MOB_TIERS in PDv2InstanceScript.cpp; these two are out of
    // a trash mob's reach on purpose, so the run's closing tier is something
    // only the cache can hand out.
    int const LOOT_CURRENCY_FINAL_FIRST = 3;
    int const LOOT_CURRENCY_TIERS = 5;
    int const LOOT_CURRENCY_FINAL_COUNT =
        LOOT_CURRENCY_TIERS - LOOT_CURRENCY_FINAL_FIRST;

    // The run this chest belongs to, or nullptr. The two-step
    // PDv2Scaling.cpp:93-108 takes for a creature - map id, then a
    // dynamic_cast that only ever happens on the dungeon map - and for the
    // same reason: a loot-state change fires for every chest, door and bobber
    // on the realm, so being off map 760 has to cost one integer compare.
    PDv2InstanceScript* RunOf(GameObject* go)
    {
        Map* map = go ? go->GetMap() : nullptr;
        if (!map || map->GetId() != sPDv2Mgr->GetConfig().mapId)
        {
            return nullptr;
        }
        return dynamic_cast<PDv2InstanceScript*>(go->GetInstanceScript());
    }

    // One item into the chest's loot, certain by construction: chance 100, no
    // quest flag, group 0. The roll that decided it already happened above,
    // and the loot table is only being used to carry the answer into the
    // window the player is about to open - the row
    // PDv2InstanceScript::InjectBossGear builds for a corpse, with a stack.
    //
    // Returns false when the row did NOT go in, which is the one thing
    // Loot::AddItem will not tell anyone: it loops on
    // `lootItems.size() < limit` with limit = MAX_NR_LOOT_ITEMS
    // (LootMgr.cpp:491-497) and returns void, so an add past the window is a
    // silent no-op. 18 is the client's hard ceiling on a 3.3.5a loot window
    // (LootMgr.h:51-52), not a server tunable, so the answer is to test the
    // window first and let the caller report what it lost.
    bool AddCertain(GameObject* go, uint32 item, uint32 count)
    {
        if (go->loot.items.size() >= static_cast<size_t>(MAX_NR_LOOT_ITEMS))
        {
            return false;
        }

        // LootItem::count is a uint8 bitfield and LootStoreItem's maxcount is
        // a uint8, so a stack the bonus table could ask for past 255 has to be
        // clamped rather than wrapped down to near-nothing; a 0 would build a
        // row nobody can loot, so it floors at one.
        uint32 const wanted = count ? count : 1;
        uint8 const stack =
            wanted > 255 ? uint8(255) : static_cast<uint8>(wanted);
        go->loot.AddItem(LootStoreItem(item, 0, 100.0f, false,
                                       LOOT_MODE_DEFAULT, 0,
                                       static_cast<int32>(stack), stack));
        return true;
    }

    // The tail an over-full cache had to throw away, once per injection and at
    // WARN: a dropped row is a reward the run rolled and the player never sees
    // - not a bug the code can fix at runtime, but a statement that the dials
    // have outgrown the window and something (template filler rows, the gear
    // count, the bonus table) has to come down. Silence here is what let the
    // finding stand in the first place.
    void WarnDropped(GameObject* go, uint32 accountId, int dropped)
    {
        if (dropped <= 0)
        {
            return;
        }

        LOG_WARN(PD_LOG,
                 "PDv2 loot: cache {} for account {} dropped {} item(s) at "
                 "the {}-slot loot window",
                 go->GetEntry(), accountId, dropped, MAX_NR_LOOT_ITEMS);
    }

    // Round E / L4, the Shifting Cache (910030): gear from the run's band.
    //
    // This chest needs no priority argument the way the finale below does. Its
    // template carries three filler rows (mod_pdungeon_phase2.sql:24-26) and
    // the gear tops out at four at lootMult 3.6, so seven of the eighteen
    // slots is the worst case and everything rolled fits. The window is still
    // tested on every add: a retuned template is one UPDATE away, and a
    // silently eaten row would read in-game as a broken pool.
    void InjectShiftingCache(GameObject* go, PDv2InstanceScript* run,
                             Player* looter)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2RunState const& state = run->GetRunState();

        // D9: floor(Items x lootMult) items are certain and the fraction left
        // over is the percent chance of one more. The dice the engine-free
        // formula deliberately does not roll (PDv2GameMath.h) are urand's and
        // never PDRandom's - a seeded loot roll would turn the same chest on
        // the same seed into a lookup table (PDv2InstanceScript.cpp:609-618).
        int const count = GameScaledCount(cfg.lootChestItems,
                                          static_cast<int>(state.lootMultX100),
                                          static_cast<int>(urand(1, 100)));

        // V2.Loot.IccDlvl, the one rung the chests climb: below it the run
        // pays the heroic-5 / normal-raid band, at or above it the ICC normal
        // one. The run's FROZEN dlvl, like every other gameplay read in this
        // module - a dlvl gained by finishing THIS run must not retune the
        // chests still standing in it.
        bool const icc = static_cast<int>(state.dlvl) >= cfg.lootIccDlvl;

        int dropped = 0;
        for (int i = 0; i < count; ++i)
        {
            uint32 const item =
                icc ? sPDv2LootMgr->RollGear(LOOT_POOL_ICC_N, looter)
                    : sPDv2LootMgr->RollGearUnion(LOOT_POOL_CHEST_LOW_A,
                                                  LOOT_POOL_CHEST_LOW_B,
                                                  looter);
            if (!item)
            {
                // An empty or unloaded pool. Nothing to add, and nothing to
                // say either - `.pdungeon v2 info` is where an operator sees
                // a pool sitting at 0 rows.
                continue;
            }
            if (!AddCertain(go, item, 1))
            {
                ++dropped;
            }
        }

        WarnDropped(go, run->GetAccountId(), dropped);
    }

    // Round E / L4 + L5, Chromie's Cache (910068): the run's closing payout -
    // gear one band above the chests, the two finale currencies, and the
    // legacy-rare bonus rolls.
    //
    // THE ORDER BELOW IS A PRIORITY ORDER AND IS LOAD-BEARING. A 3.3.5a loot
    // window holds 18 rows, and Loot::AddItem drops everything past that
    // without a word (LootMgr.cpp:491-497, LootMgr.h:51). This cache can ask
    // for more at the top of the dials: the five template filler rows are
    // already in go->loot when the hook runs (mod_pdungeon_chromie.sql), plus
    // up to 4 gear at lootMult 3.6, plus T4 and T5, plus up to 9 bonus rows -
    // about twenty. The cap eats the TAIL, so the tail has to be the cheapest
    // thing:
    //
    //   1. the legacy rares FIRST. They are the rarest rows in the module, the
    //      headline of a finished run and the reason anyone pushes the dial;
    //      appended last (as they were) they are the first to be thrown away,
    //      and a player who never sees the row cannot tell that from bad luck.
    //   2. T4 and T5 currency. Two rows at most, and the cache is their only
    //      source on the realm - a trash mob cannot pay these tiers.
    //   3. gear last. Same pool the next run rolls again, and the one part of
    //      the payout that exists elsewhere.
    //
    // Whatever still did not fit is reported once, at WARN, by WarnDropped.
    void InjectFinalCache(GameObject* go, PDv2InstanceScript* run,
                          Player* looter)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2RunState const& state = run->GetRunState();

        int dropped = 0;

        // L5, the legacy rares: independent rolls over `pdungeon_loot_bonus`,
        // each already gated on the run's difficulty and scaled by the same
        // room factor inside PDv2LootMgr, and each already resolved to the
        // looter's armour class where the row asks for one. The answer may
        // hold none, one or all of the rows.
        std::vector<LootBonusHit> const bonus =
            sPDv2LootMgr->RollBonus(state.difficulty,
                                    static_cast<int>(state.roomFactorX100),
                                    looter);

        int bonusAdded = 0;
        for (LootBonusHit const& hit : bonus)
        {
            if (!AddCertain(go, hit.item, hit.count))
            {
                ++dropped;
                continue;
            }
            ++bonusAdded;
        }

        // T4 and T5. The formula the kill tiers use
        // (PDv2InstanceScript::RollCurrency) - conf item, conf difficulty
        // gate, conf percent scaled by the run's frozen room factor - but ONE
        // roll for the cache rather than one per player: a cache is a shared
        // object with a single loot window, so a personal roll would have
        // nowhere to put its answer.
        int hits[LOOT_CURRENCY_FINAL_COUNT] = { 0, 0 };
        for (int tier = LOOT_CURRENCY_FINAL_FIRST;
             tier < LOOT_CURRENCY_TIERS; ++tier)
        {
            uint32 const item = cfg.lootCurrencyItem[tier];
            if (!item)
            {
                // 0 is how an operator turns a tier off, and the conf is read
                // live, so it is a per-cache question and not a load-time one.
                continue;
            }

            // The run's frozen dial, not the live account row: a difficulty
            // raised while the finale was already standing must not unlock a
            // tier the run was never fought at.
            if (cfg.lootCurrencyMinDiff[tier] >
                static_cast<int>(state.difficulty))
            {
                continue;
            }

            int const chanceBp =
                GameChanceBp(cfg.lootCurrencyChancePct[tier],
                             static_cast<int>(state.roomFactorX100));
            if (chanceBp <= 0)
            {
                continue;
            }
            if (static_cast<int>(urand(1, PD_GAME_CHANCE_BP_MAX)) > chanceBp)
            {
                continue;
            }

            if (!AddCertain(go, item, 1))
            {
                ++dropped;
                continue;
            }
            hits[tier - LOOT_CURRENCY_FINAL_FIRST] = 1;
        }

        // L4, the gear, LAST for the reason the block comment gives: it is the
        // most replaceable half of the payout, so it is the half that should
        // meet the window first if anything has to.
        //
        // The same rung the chests use, one band higher on both sides.
        bool const icc = static_cast<int>(state.dlvl) >= cfg.lootIccDlvl;
        std::string_view const pool = icc ? LOOT_POOL_ICC_HC : LOOT_POOL_ICC_N;

        int const wanted =
            GameScaledCount(cfg.lootFinalItems,
                            static_cast<int>(state.lootMultX100),
                            static_cast<int>(urand(1, 100)));

        int gear = 0;
        for (int i = 0; i < wanted; ++i)
        {
            uint32 const item = sPDv2LootMgr->RollGear(pool, looter);
            if (!item)
            {
                continue;
            }
            if (!AddCertain(go, item, 1))
            {
                ++dropped;
                continue;
            }
            ++gear;
        }

        // One line per cache, at INFO and not DEBUG: this is the whole closing
        // payout of a run, so an operator reading a night of logs can see what
        // each finished run actually paid without turning anything on. The
        // chests stay silent - there are up to a dozen of them per run and
        // they would drown this. Every count is what actually went INTO the
        // loot, never what was rolled, so this line and the WARN below add up
        // to the roll.
        LOG_INFO(PD_LOG,
                 "PDv2 loot: final cache for account {} (diff {}, dlvl {}, "
                 "rooms x{}): {} gear, T4 {}, T5 {}, bonus {}",
                 run->GetAccountId(), uint32(state.difficulty),
                 uint32(state.dlvl), uint32(state.roomFactorX100), gear,
                 hits[0], hits[1], bonusAdded);

        WarnDropped(go, run->GetAccountId(), dropped);
    }

    // One script for both chests. The hook is GLOBAL - it fires for every
    // gameobject on the realm - so the gate has to live here, and one gate is
    // both cheaper to run and easier to read than two.
    class PDv2ChestLootScript : public AllGameObjectScript
    {
    public:
        PDv2ChestLootScript() : AllGameObjectScript("PDv2ChestLootScript") { }

        void OnGameObjectLootStateChanged(GameObject* go, uint32 state,
                                          Unit* unit) override
        {
            if (state != GO_ACTIVATED || !go || !sPDv2Mgr->IsEnabled())
            {
                return;
            }

            // The entry compare before anything else that dereferences: two
            // template ids out of the tens of thousands a realm has do the
            // real filtering, and they cost one field read.
            uint32 const entry = go->GetEntry();
            if (entry != GO_CHEST && entry != GO_REWARD_CHEST)
            {
                return;
            }

            // `unit` is the player Player::SendLoot handed to SetLootState at
            // Player.cpp:7930, which is exactly the looter whose class the D5
            // filter narrows the pools to. Anything that is not a player rolls
            // NOTHING rather than rolling unfiltered: the only other ways into
            // GO_ACTIVATED here are a GM and a script, and neither should
            // spend a player's chest.
            Player* const looter = unit ? unit->ToPlayer() : nullptr;
            if (!looter)
            {
                return;
            }

            PDv2InstanceScript* const run = RunOf(go);
            if (!run)
            {
                return;
            }

            // LAST, because GetDefault CREATES the entry: run any earlier and
            // it would allocate a DataMap node on every gameobject on the
            // realm. Set BEFORE the first roll, so an early return further
            // down still leaves the chest spent rather than re-rollable.
            PDv2ChestData* const data =
                go->CustomData.GetDefault<PDv2ChestData>(PD_CHEST_DATA_KEY);
            if (data->injected)
            {
                return;
            }
            data->injected = true;

            if (entry == GO_CHEST)
            {
                InjectShiftingCache(go, run, looter);
            }
            else
            {
                InjectFinalCache(go, run, looter);
            }
        }
    };
}

void AddPDv2LootScripts()
{
    new PDv2ChestLootScript();
}
