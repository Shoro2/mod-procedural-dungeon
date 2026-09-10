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

#include "PDv2LootMgr.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "PDDefines.h"
#include "PDv2Mgr.h"
#include "PDv2TaggedAura.h"
#include "Player.h"
#include "QueryResult.h"
#include "Random.h"
#include "Timer.h"
#include "WorldSession.h"
#include "generator/PDv2GameMath.h"

#include <algorithm>

namespace PDungeon
{
    namespace
    {
        // How many dropped rows are named one by one before the loader
        // switches to counting them. A foreign environment can be missing
        // every item in a 5 000-row table, and 5 000 error lines do not make
        // that clearer than ten plus a total do - they bury whatever else
        // went wrong at boot.
        constexpr uint32 LOOT_MAX_NAMED_DROPS = 10;
    }

    PDv2LootMgr* PDv2LootMgr::instance()
    {
        static PDv2LootMgr mgr;
        return &mgr;
    }

    void PDv2LootMgr::Load()
    {
        _pools.clear();
        _bonus.clear();

        LoadPools();
        LoadBonus();

        // ONE boot line for both tables, per pool, with the expansion split
        // for any pool that spans more than one - which in the shipped data
        // is MATS and only MATS, but is written as a property rather than as
        // a pool name so a second mixed pool would report itself.
        //
        // A pool that reads 0 - or is MISSING from the line, which is what a
        // pool whose every row was dropped looks like, since a pool exists
        // here only once a row of it survived - is the failure this module
        // degrades most quietly on: a run still generates, still spawns,
        // still lets itself be cleared, and simply pays nothing.
        std::string line;
        for (LootPool const& pool : _pools)
        {
            if (!line.empty())
            {
                line += ", ";
            }
            line += pool.name + " " + std::to_string(pool.entries.size());

            uint32 byExpansion[PD_LOOT_EXPANSION_MAX + 1] = { 0, 0, 0 };
            for (LootPoolEntry const& entry : pool.entries)
            {
                ++byExpansion[entry.expansion];
            }

            uint32 spans = 0;
            for (uint32 const count : byExpansion)
            {
                if (count)
                {
                    ++spans;
                }
            }
            if (spans > 1)
            {
                line += " (classic " + std::to_string(byExpansion[0]) +
                        " / tbc " + std::to_string(byExpansion[1]) +
                        " / wotlk " + std::to_string(byExpansion[2]) + ")";
            }
        }

        if (!line.empty())
        {
            line += ", ";
        }
        line += "bonus " + std::to_string(_bonus.size());

        LOG_INFO(PD_LOG, "PDv2 loot: {}", line);
    }

    void PDv2LootMgr::LoadPools()
    {
        // No ORDER BY: the pools are named on every line that prints them, so
        // their order carries no meaning, and nothing downstream depends on
        // it - the rolls are urand over a weight sum, not a walk of a stored
        // sequence.
        QueryResult result = WorldDatabase.Query(
            "SELECT pool, item, weight, expansion, category "
            "FROM pdungeon_loot_pool");
        if (!result)
        {
            LOG_ERROR(PD_LOG, "PDv2 loot: pdungeon_loot_pool has no rows - "
                              "mod_pdungeon_loot_pools.sql was not applied, "
                              "and no chest, boss or cache pays any gear");
            return;
        }

        uint32 missing = 0;
        uint32 clamped = 0;
        do
        {
            Field* fields = result->Fetch();
            std::string const name = fields[0].Get<std::string>();
            uint32 const item = fields[1].Get<uint32>();

            // The pools are RESOLVED out of this realm's own item_template
            // (script 106), so a row that no longer resolves means an item
            // was deleted under them. Dropping it here is the difference
            // between a regenerate-the-pools warning at boot and a player
            // being handed an id his client cannot draw.
            ItemTemplate const* const proto = sObjectMgr->GetItemTemplate(item);
            if (!proto)
            {
                if (missing < LOOT_MAX_NAMED_DROPS)
                {
                    LOG_ERROR(PD_LOG, "PDv2 loot: pool {} references item {} "
                                      "which no item_template row defines - "
                                      "row dropped", name, item);
                }
                ++missing;
                continue;
            }

            LootPoolEntry entry;
            entry.item = item;
            entry.weight = fields[2].Get<uint16>();
            entry.expansion = fields[3].Get<uint8>();
            // Round E / WP9. The template is already in hand from the
            // existence check above, so the stat mask costs this loader
            // nothing beyond the ten-slot walk - and it is the LAST moment it
            // is cheap. Computing it per roll instead would put an
            // sObjectMgr lookup on every candidate of every gear roll.
            entry.statMask = StatMaskFor(proto);
            entry.category = fields[4].Get<std::string>();

            // LOAD-BEARING, not tidiness: RollMaterial tests
            // (1 << expansion) & mask, and an expansion past the mask's
            // width would shift out of it. Clamping to wotlk keeps such a
            // row rollable under the default mask instead of silently
            // vanishing from a pool whose count still includes it.
            if (entry.expansion > PD_LOOT_EXPANSION_MAX)
            {
                if (clamped < LOOT_MAX_NAMED_DROPS)
                {
                    LOG_ERROR(PD_LOG, "PDv2 loot: pool {} item {} carries "
                                      "expansion {} - clamped to {}", name,
                              item, uint32(entry.expansion),
                              uint32(PD_LOOT_EXPANSION_MAX));
                }
                entry.expansion = PD_LOOT_EXPANSION_MAX;
                ++clamped;
            }

            PoolFor(name).entries.push_back(entry);
        } while (result->NextRow());

        if (missing > LOOT_MAX_NAMED_DROPS)
        {
            LOG_ERROR(PD_LOG, "PDv2 loot: {} pool row(s) in total referenced "
                              "an item that does not exist - regenerate "
                              "mod_pdungeon_loot_pools.sql", missing);
        }
        if (clamped > LOOT_MAX_NAMED_DROPS)
        {
            LOG_ERROR(PD_LOG, "PDv2 loot: {} pool row(s) in total carried an "
                              "expansion above {}", clamped,
                      uint32(PD_LOOT_EXPANSION_MAX));
        }
    }

    void PDv2LootMgr::LoadBonus()
    {
        // ORDER BY id so the rows are rolled in the order an operator reads
        // them in the file. They are independent rolls, so the order changes
        // no outcome - it changes which line of the log is which row.
        QueryResult result = WorldDatabase.Query(
            "SELECT id, item, chance_bp, min_count, max_count, min_diff, "
            "armor_pick FROM pdungeon_loot_bonus ORDER BY id");
        if (!result)
        {
            LOG_ERROR(PD_LOG, "PDv2 loot: pdungeon_loot_bonus has no rows - "
                              "the final cache pays no legacy rares");
            return;
        }

        uint32 missing = 0;
        do
        {
            Field* fields = result->Fetch();

            LootBonusRow row;
            row.id = fields[0].Get<uint16>();
            row.item = fields[1].Get<uint32>();
            row.chanceBp = fields[2].Get<uint32>();
            row.minCount = fields[3].Get<uint16>();
            row.maxCount = fields[4].Get<uint16>();
            row.minDiff = fields[5].Get<uint8>();
            row.armorPick = fields[6].Get<uint8>() != 0;

            // An armour-pick row is FOUR items, not one, and a row whose
            // fourth entry was never created would pay plate wearers
            // nothing while paying everyone else. All four are checked
            // here, once, rather than discovered by the one class that
            // happens to loot it.
            uint32 const variants = row.armorPick ? 4u : 1u;
            bool ok = true;
            for (uint32 n = 0; n < variants; ++n)
            {
                if (!sObjectMgr->GetItemTemplate(row.item + n))
                {
                    LOG_ERROR(PD_LOG, "PDv2 loot: bonus row {} references "
                                      "item {} which no item_template row "
                                      "defines - row dropped", row.id,
                              row.item + n);
                    ok = false;
                }
            }
            if (!ok)
            {
                ++missing;
                continue;
            }

            // urand ASSERTs max >= min (Random.cpp:46), and an ASSERT at the
            // moment a cache is opened takes the whole worldserver down. A
            // swapped pair is a typo in a data file and must cost a log line,
            // not the realm.
            if (row.minCount < 1)
            {
                row.minCount = 1;
            }
            if (row.maxCount < row.minCount)
            {
                LOG_ERROR(PD_LOG, "PDv2 loot: bonus row {} has max_count {} "
                                  "below min_count {} - max raised to the "
                                  "minimum", row.id, uint32(row.maxCount),
                          uint32(row.minCount));
                row.maxCount = row.minCount;
            }

            _bonus.push_back(row);
        } while (result->NextRow());

        if (missing)
        {
            LOG_ERROR(PD_LOG, "PDv2 loot: {} bonus row(s) dropped for a "
                              "missing item", missing);
        }
    }

    PDv2LootMgr::LootPool const* PDv2LootMgr::FindPool(
        std::string_view name) const
    {
        for (LootPool const& pool : _pools)
        {
            if (pool.name == name)
            {
                return &pool;
            }
        }
        return nullptr;
    }

    PDv2LootMgr::LootPool& PDv2LootMgr::PoolFor(std::string const& name)
    {
        for (LootPool& pool : _pools)
        {
            if (pool.name == name)
            {
                return pool;
            }
        }
        _pools.emplace_back();
        _pools.back().name = name;
        return _pools.back();
    }

    void PDv2LootMgr::Collect(LootPool const& pool, Player const* filterFor,
                              uint8_t expansionMask, uint8_t profile,
                              std::vector<LootPoolEntry const*>& out)
    {
        // Read once, not per entry: a filtered draw over RAID_N asks these
        // two questions 2 235 times.
        uint8_t const classId = filterFor ? filterFor->getClass() : uint8_t(0);
        uint32_t const raceMask = filterFor ? filterFor->getRaceMask() : 0u;

        out.reserve(out.size() + pool.entries.size());
        for (LootPoolEntry const& entry : pool.entries)
        {
            if (((1u << entry.expansion) & expansionMask) == 0)
            {
                continue;
            }
            // WP9, and BEFORE the class test on purpose: this one is a byte
            // compare against a mask the loader already computed, while the
            // class test costs an sObjectMgr lookup per surviving candidate.
            // The cheaper question first is worth stating, because the two
            // read as though their order were arbitrary.
            if (!FitsProfileRaw(entry.statMask, profile))
            {
                continue;
            }
            if (filterFor &&
                !ItemFitsPlayer(sObjectMgr->GetItemTemplate(entry.item),
                                classId, raceMask))
            {
                continue;
            }
            out.push_back(&entry);
        }
    }

    uint32_t PDv2LootMgr::PickWeighted(
        std::vector<LootPoolEntry const*> const& candidates)
    {
        uint32 total = 0;
        for (LootPoolEntry const* entry : candidates)
        {
            total += entry->weight;
        }
        if (!total)
        {
            // Empty, or every candidate disabled with weight 0. Returning 0
            // here is also what keeps urand's min <= max below.
            return 0;
        }

        // The RollBonusLoot idiom (PDv2InstanceScript.cpp:622-631): one roll
        // into the summed weight, then walk until it runs out. urand and NOT
        // PDRandom - see the determinism note at the top of the header.
        int64_t roll = static_cast<int64_t>(urand(1, total));
        for (LootPoolEntry const* entry : candidates)
        {
            roll -= static_cast<int64_t>(entry->weight);
            if (roll <= 0)
            {
                return entry->item;
            }
        }

        // Unreachable: the walk cannot outlast a roll bounded by the same
        // sum it subtracts. The last candidate is the honest answer if a
        // future weight type ever makes it reachable.
        return candidates.back()->item;
    }

    uint32_t PDv2LootMgr::PoolSize(std::string_view pool) const
    {
        LootPool const* found = FindPool(pool);
        return found ? static_cast<uint32_t>(found->entries.size()) : 0u;
    }

    uint32_t PDv2LootMgr::RollGear(std::string_view pool, Player const* looter,
                                   uint8_t profile) const
    {
        LootPool const* found = FindPool(pool);
        if (!found || found->entries.empty())
        {
            return 0;
        }

        std::vector<LootPoolEntry const*> candidates;
        // Which rung of the header's ladder actually PAID, for the debug line
        // below: 2 = class and profile, 1 = class only, 0 = the raw pool.
        // Assigned from what came back rather than from what was tried, so a
        // line reading "stage 1" is a line where the profile really was given
        // up - which is the only thing anybody reads this number for.
        int stage = 0;
        if (looter && sPDv2Mgr->GetConfig().lootClassFilter)
        {
            // Built per call and thrown away: the pools top out around 2 200
            // entries, gear is rolled a handful of times per run, and a cache
            // keyed by (pool, class, race, profile) would have to be
            // invalidated by hand the day the pools are regenerated. The gated
            // line below is the measurement that says the choice still holds.
            //
            // ONE LINE PER GEAR ROLL since Round E / R3, so it takes V2.Debug
            // like every other per-roll line: a chest, a boss and the final
            // cache each roll several, per looter, per run. The per-cache INFO
            // in PDv2ChestLoot.cpp is the ungated record of what was PAID -
            // this one is only the filter's own cost.
            uint32 const startMs = getMSTime();
            Collect(*found, looter, PD_LOOT_EXPANSION_ALL, profile, candidates);
            stage = candidates.empty() ? 0 : 2;

            // Round E / WP9, the middle rung: the profile is dropped and the
            // class is KEPT. This is the whole reason the fallback grew one -
            // before WP9 an empty filtered set went straight to the raw pool,
            // which for a chosen stat profile would have meant "ask for
            // strength gear, be handed a wand".
            if (candidates.empty() && profile != PD_STAT_PROFILE_OFF)
            {
                Collect(*found, looter, PD_LOOT_EXPANSION_ALL,
                        PD_STAT_PROFILE_OFF, candidates);
                stage = candidates.empty() ? 0 : 1;
            }

            if (PDv2Debug())
            {
                LOG_INFO(PD_LOG, "PDv2 loot: pool {} filtered to {} of {} "
                                 "entries for class {} at stat profile {} "
                                 "(stage {}) in {} ms", found->name,
                         uint32(candidates.size()),
                         uint32(found->entries.size()),
                         uint32(looter->getClass()), uint32(profile), stage,
                         GetMSTimeDiffToNow(startMs));
            }
        }

        // Stage 3, and also the path a disabled filter takes. An empty
        // filtered set is NOT an error: a pool can legitimately hold nothing
        // for one class, and paying the wrong gear beats paying none (spec
        // D5). By the time control reaches here the profile has already been
        // given up above, so this is the class filter's own last resort and
        // nothing else.
        if (candidates.empty())
        {
            Collect(*found, nullptr, PD_LOOT_EXPANSION_ALL,
                    PD_STAT_PROFILE_OFF, candidates);
        }
        return PickWeighted(candidates);
    }

    uint32_t PDv2LootMgr::RollGearUnion(std::string_view a, std::string_view b,
                                        Player const* looter,
                                        uint8_t profile) const
    {
        LootPool const* poolA = FindPool(a);
        LootPool const* poolB = FindPool(b);
        if (!poolA && !poolB)
        {
            return 0;
        }

        // ONE weighted set over both pools, which is the whole point: picking
        // a pool first and an item second would pay the 158-row pool as often
        // as the 2 235-row one. An item that sat in both pools would carry
        // both weights; the shipped pools are disjoint (script 106 resolves
        // them from disjoint sources and the union pair was measured to share
        // no item), so that is a property of the data, not a rule enforced
        // here.
        std::vector<LootPoolEntry const*> candidates;
        if (looter && sPDv2Mgr->GetConfig().lootClassFilter)
        {
            if (poolA)
            {
                Collect(*poolA, looter, PD_LOOT_EXPANSION_ALL, profile,
                        candidates);
            }
            if (poolB)
            {
                Collect(*poolB, looter, PD_LOOT_EXPANSION_ALL, profile,
                        candidates);
            }

            // WP9 stage 2, and over BOTH pools again rather than over the one
            // that came up empty: the union is one reward tier, so a partial
            // re-collect would quietly turn it back into the pool-first draw
            // this function exists to avoid.
            if (candidates.empty() && profile != PD_STAT_PROFILE_OFF)
            {
                if (poolA)
                {
                    Collect(*poolA, looter, PD_LOOT_EXPANSION_ALL,
                            PD_STAT_PROFILE_OFF, candidates);
                }
                if (poolB)
                {
                    Collect(*poolB, looter, PD_LOOT_EXPANSION_ALL,
                            PD_STAT_PROFILE_OFF, candidates);
                }
                if (PDv2Debug())
                {
                    LOG_INFO(PD_LOG, "PDv2 loot: the union of {} and {} held "
                                     "nothing for class {} at stat profile {} "
                                     "- profile dropped, class kept ({} "
                                     "candidates)", a, b,
                             uint32(looter->getClass()), uint32(profile),
                             uint32(candidates.size()));
                }
            }
        }

        if (candidates.empty())
        {
            if (poolA)
            {
                Collect(*poolA, nullptr, PD_LOOT_EXPANSION_ALL,
                        PD_STAT_PROFILE_OFF, candidates);
            }
            if (poolB)
            {
                Collect(*poolB, nullptr, PD_LOOT_EXPANSION_ALL,
                        PD_STAT_PROFILE_OFF, candidates);
            }
        }
        return PickWeighted(candidates);
    }

    uint32_t PDv2LootMgr::RollMaterial(uint8_t expansionMask) const
    {
        LootPool const* pool = FindPool(PD_LOOT_POOL_MATS);
        if (!pool || pool->entries.empty())
        {
            return 0;
        }

        // No class filter, no STAT profile and no fallback, deliberately on
        // all three counts: a material fits every class and has no stat line
        // to profile, and a mask that matches nothing is a caller asking for
        // a band that does not exist - answering it with some other
        // expansion's material would ignore what F1 asked for.
        std::vector<LootPoolEntry const*> candidates;
        Collect(*pool, nullptr, expansionMask, PD_STAT_PROFILE_OFF, candidates);
        return PickWeighted(candidates);
    }

    std::vector<LootBonusHit> PDv2LootMgr::RollBonus(
        uint8_t difficulty, int roomFactorX100, Player const* looter) const
    {
        std::vector<LootBonusHit> hits;
        if (_bonus.empty() || roomFactorX100 <= 0)
        {
            // roomFactorX100 <= 0 is a run with no rooms at all, which pays
            // nothing by the same D8 rule every other roll obeys.
            return hits;
        }

        uint8_t const armour = looter
            ? GameArmourIndexForClass(looter->getClass())
            : PD_ARMOUR_CLOTH;

        for (LootBonusRow const& row : _bonus)
        {
            if (difficulty < row.minDiff)
            {
                continue;
            }

            // int64 for the product: chance_bp is an INT UNSIGNED and the
            // room factor is itself x100, so a wrap here would turn a 25 %
            // chance into a certainty rather than into a smaller number.
            // No clamp at 10000 is needed - a product past the roll's range
            // simply always hits, which is what a certainty means.
            int64_t const chance =
                static_cast<int64_t>(row.chanceBp) * roomFactorX100 / 100;
            if (chance <= 0)
            {
                continue;
            }

            uint32 const roll =
                urand(1, static_cast<uint32>(PD_GAME_CHANCE_BP_MAX));
            if (static_cast<int64_t>(roll) > chance)
            {
                continue;
            }

            LootBonusHit hit;
            // The four armour variants are consecutive entries starting at
            // the row's item, in the cloth/leather/mail/plate order
            // GameArmourIndexForClass answers in. LoadBonus proved all four
            // exist before this row was kept.
            hit.item = row.item + (row.armorPick ? armour : 0u);
            hit.count = urand(row.minCount, row.maxCount);
            hits.push_back(hit);
        }
        return hits;
    }

    bool PDv2LootMgr::ItemFitsPlayer(ItemTemplate const* proto, uint8_t classId,
                                     uint32_t raceMask)
    {
        // -1 in either column arrives as 0xFFFFFFFF and passes on its own, so
        // the "everyone" convention needs no case of its own here either.
        return proto && (proto->AllowableRace & raceMask) != 0 &&
               FitsClassRaw(proto->AllowableClass,
                            static_cast<uint8_t>(proto->Class),
                            static_cast<uint8_t>(proto->SubClass), classId);
    }

    uint8_t PDv2LootMgr::StatMaskFor(ItemTemplate const* proto)
    {
        if (!proto)
        {
            return 0;
        }

        // Heirlooms and any other scaling item: ItemStat[] is empty and the
        // real stat line comes out of ScalingStatValues.dbc at equip time,
        // scaled to the wearer's level. 0 - "no stat line" - is the only
        // honest answer, and it is also the harmless one: mask 0 fits every
        // profile. The shipped pools contain none (measured: every gear row
        // has ScalingStatDistribution 0), so this is a guard against a pool
        // regenerated from a different item_template rather than a live case.
        if (proto->ScalingStatDistribution != 0)
        {
            return 0;
        }

        // ObjectMgr packs the non-zero stats DENSELY into the front of the
        // array and sets StatsCount to how many it wrote (ObjectMgr.cpp:3399),
        // so 0..StatsCount-1 has no holes and no zero values. std::min all the
        // same: StatsCount is a uint32 field and the array is ten entries, and
        // a walk past it would be undefined behaviour rather than a bad mask.
        uint32 const count =
            std::min<uint32>(proto->StatsCount, MAX_ITEM_PROTO_STATS);

        uint8_t mask = 0;
        for (uint32 i = 0; i < count; ++i)
        {
            switch (proto->ItemStat[i].ItemStatType)
            {
                case ITEM_MOD_STRENGTH:
                    mask |= PD_STAT_STR;
                    break;
                case ITEM_MOD_AGILITY:
                    mask |= PD_STAT_AGI;
                    break;
                case ITEM_MOD_INTELLECT:
                    mask |= PD_STAT_INT;
                    break;
                // Caster evidence. Spirit and mp5 are on healing gear that
                // often names no intellect at all, spell power and spell
                // penetration on the rest of it; 41 and 42 are the deprecated
                // pre-3.0 healing/damage columns, which the legacy pools DO
                // still contain - dropping them would let a classic caster
                // ring through the strength profile.
                case ITEM_MOD_SPIRIT:
                case ITEM_MOD_SPELL_HEALING_DONE:
                case ITEM_MOD_SPELL_DAMAGE_DONE:
                case ITEM_MOD_MANA_REGENERATION:
                case ITEM_MOD_SPELL_POWER:
                case ITEM_MOD_SPELL_PENETRATION:
                    mask |= PD_STAT_CASTER_EVIDENCE;
                    break;
                // Physical evidence, and the TANK ratings are in it on
                // purpose: a ring of defence and dodge is a warrior's or a
                // paladin's, and no caster profile should be offered one.
                case ITEM_MOD_DEFENSE_SKILL_RATING:
                case ITEM_MOD_DODGE_RATING:
                case ITEM_MOD_PARRY_RATING:
                case ITEM_MOD_BLOCK_RATING:
                case ITEM_MOD_EXPERTISE_RATING:
                case ITEM_MOD_ATTACK_POWER:
                case ITEM_MOD_RANGED_ATTACK_POWER:
                case ITEM_MOD_ARMOR_PENETRATION_RATING:
                case ITEM_MOD_BLOCK_VALUE:
                    mask |= PD_STAT_PHYS_EVIDENCE;
                    break;
                // Stamina, hit, crit, haste, resilience, mana, health and the
                // rest: NEUTRAL, so they set no bit. Every profile wants them,
                // and a bit for them could only ever reject something.
                default:
                    break;
            }
        }
        return mask;
    }

    uint8_t PDv2LootMgr::StatProfileFor(Player const* looter)
    {
        // A boss killed by nobody we can resolve to a player, or a caller that
        // has already given up on a looter: profile Off, which is exactly what
        // the class filter does with the same input.
        if (!looter || !looter->GetSession())
        {
            return PD_STAT_PROFILE_OFF;
        }

        // ONE aura walk per injection. The unlock is per character, so this is
        // the looter's OWN node and not the run owner's - the player standing
        // at the chest is the one whose reward this is.
        if (!TaggedAuraAmount(looter, PD_TALENT_TAG_STATFILTER))
        {
            return PD_STAT_PROFILE_OFF;
        }

        // ...and the setting is per account. An account the manager has never
        // loaded answers with a default-constructed state, whose profile is
        // Off - so a looter from a realm-crossing edge case degrades to the
        // pre-WP9 behaviour instead of to an empty window.
        return sPDv2Mgr
            ->GetAccountState(looter->GetSession()->GetAccountId())
            .cfgStatProfile;
    }

    std::string PDv2LootMgr::Describe() const
    {
        std::string out = "pools " + std::to_string(_pools.size());
        for (LootPool const& pool : _pools)
        {
            out += " - " + pool.name + " " +
                   std::to_string(pool.entries.size());
        }
        out += " - bonus " + std::to_string(_bonus.size());
        return out;
    }
}
