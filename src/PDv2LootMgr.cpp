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
#include "Player.h"
#include "QueryResult.h"
#include "Random.h"
#include "Timer.h"
#include "generator/PDv2GameMath.h"

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
            if (!sObjectMgr->GetItemTemplate(item))
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
                              uint8_t expansionMask,
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

    uint32_t PDv2LootMgr::RollGear(std::string_view pool,
                                   Player const* looter) const
    {
        LootPool const* found = FindPool(pool);
        if (!found || found->entries.empty())
        {
            return 0;
        }

        std::vector<LootPoolEntry const*> candidates;
        if (looter && sPDv2Mgr->GetConfig().lootClassFilter)
        {
            // Built per call and thrown away: the pools top out around 2 200
            // entries, gear is rolled a handful of times per run, and a cache
            // keyed by (pool, class, race) would have to be invalidated by
            // hand the day the pools are regenerated. The debug line below is
            // the measurement that says the choice still holds.
            uint32 const startMs = getMSTime();
            Collect(*found, looter, PD_LOOT_EXPANSION_ALL, candidates);
            LOG_DEBUG(PD_LOG, "PDv2 loot: pool {} filtered to {} of {} "
                              "entries for class {} in {} ms", found->name,
                      uint32(candidates.size()),
                      uint32(found->entries.size()),
                      uint32(looter->getClass()),
                      GetMSTimeDiffToNow(startMs));
        }

        // Also the path a disabled filter takes. An empty filtered set is
        // NOT an error: a pool can legitimately hold nothing for one class,
        // and paying the wrong gear beats paying none (spec D5).
        if (candidates.empty())
        {
            Collect(*found, nullptr, PD_LOOT_EXPANSION_ALL, candidates);
        }
        return PickWeighted(candidates);
    }

    uint32_t PDv2LootMgr::RollGearUnion(std::string_view a, std::string_view b,
                                        Player const* looter) const
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
                Collect(*poolA, looter, PD_LOOT_EXPANSION_ALL, candidates);
            }
            if (poolB)
            {
                Collect(*poolB, looter, PD_LOOT_EXPANSION_ALL, candidates);
            }
        }

        if (candidates.empty())
        {
            if (poolA)
            {
                Collect(*poolA, nullptr, PD_LOOT_EXPANSION_ALL, candidates);
            }
            if (poolB)
            {
                Collect(*poolB, nullptr, PD_LOOT_EXPANSION_ALL, candidates);
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

        // No class filter and no fallback, deliberately on both counts: a
        // material fits every class, and a mask that matches nothing is a
        // caller asking for a band that does not exist - answering it with
        // some other expansion's material would ignore what F1 asked for.
        std::vector<LootPoolEntry const*> candidates;
        Collect(*pool, nullptr, expansionMask, candidates);
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
