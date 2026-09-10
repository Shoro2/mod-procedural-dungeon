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

#ifndef MOD_PDUNGEON_V2_LOOT_MGR_H
#define MOD_PDUNGEON_V2_LOOT_MGR_H

// Engine-free and header-only, and here rather than only in the .cpp because
// the two gear rolls default their stat profile to PD_STAT_PROFILE_OFF - a
// default argument has to be a value the caller's translation unit can see.
#include "generator/PDv2GameMath.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Player;
struct ItemTemplate;

// PDv2 loot pools: which items a run may ever pay out, and every roll that
// decides one of them.
//
// The two world tables (`pdungeon_loot_pool`, `pdungeon_loot_bonus`, Round E /
// L1) are read ONCE at world startup and READ-ONLY afterwards - the same
// contract PDv2PackMgr's tables have, and the same reason: map threads roll
// loot on every kill and every cache, and an immutable store needs no lock. A
// pool change is a SQL statement plus a restart, never a `.reload config`.
//
// The pools themselves are GENERATED (scripts/106_pd_loot_pools.py resolves
// them out of the world DB); nothing in this file knows which items are in
// them, which is what keeps a retune a data change. The one name the engine
// carries is PD_LOOT_POOL_MATS below, because the material roll has no other
// pool to be told about.
//
// THE DETERMINISM BOUNDARY RUNS THROUGH HERE. Layout and spawn selection go
// through PDRandom, so a stored seed rebuilds the same dungeon; loot must NOT,
// or the same kill on the same seed would drop the same item for ever and
// farming would become a lookup table (PDv2InstanceScript.cpp:609-618 states
// it in full). Every roll below therefore uses the core's urand.
namespace PDungeon
{
    // The material pool, by name. Materials are the one roll whose caller has
    // no pool to pass - it asks for "a material" and F1's expansion mask -
    // so the name lives here rather than being spelled at each call site.
    constexpr std::string_view PD_LOOT_POOL_MATS = "MATS";

    // `expansion` is 0 classic / 1 tbc / 2 wotlk, and RollMaterial takes the
    // set of them as a bitmask. MAX is load-bearing and not decoration: the
    // mask test shifts by the column's value, so a row that carried anything
    // larger would shift past the width of the mask.
    constexpr uint8_t PD_LOOT_EXPANSION_MAX = 2;
    constexpr uint8_t PD_LOOT_EXPANSION_ALL = 0x7;

    // One row of `pdungeon_loot_pool`. `category` is carried because the MATS
    // rows are grouped by it (gem, cloth, herb, ...) and a later filter will
    // want it; the gear pools leave it empty.
    struct LootPoolEntry
    {
        uint32_t item = 0;
        uint16_t weight = 0;
        uint8_t expansion = PD_LOOT_EXPANSION_MAX;
        // Round E / WP9. What the item's stat line says about who it is for,
        // as the five bits of PDStatMaskBits. PRECOMPUTED at Load() and never
        // again: a filtered draw over RAID_N asks this question 2 235 times
        // per roll, and asking it of sObjectMgr each time would trade a byte
        // per row for a template lookup per candidate per roll. 0 means "no
        // stat line", which every profile accepts.
        uint8_t statMask = 0;
        std::string category;
    };

    // One row of `pdungeon_loot_bonus`: an INDEPENDENT roll of the final
    // cache (L5), not a draw from a pool. `chanceBp` is in basis points
    // BEFORE the D8 room factor scales it, `minDiff` gates the row against
    // the run's difficulty dial, and `armorPick` means the row's item is the
    // first of four consecutive entries - cloth, leather, mail, plate - of
    // which the looter gets the one for his class.
    struct LootBonusRow
    {
        uint16_t id = 0;
        uint32_t item = 0;
        uint32_t chanceBp = 0;
        uint16_t minCount = 1;
        uint16_t maxCount = 1;
        uint8_t minDiff = 1;
        bool armorPick = false;
    };

    // What one bonus row paid out, once it has hit and been rolled.
    struct LootBonusHit
    {
        uint32_t item = 0;
        uint32_t count = 0;
    };

    class PDv2LootMgr
    {
    public:
        static PDv2LootMgr* instance();

        // Reads both tables. Rows whose item is unknown to sObjectMgr are
        // dropped loudly rather than handed to a player as an item id the
        // client cannot resolve - the pools reference stock entries this
        // module does not own, so a foreign environment WILL be missing some.
        void Load();

        // How many entries a pool holds. 0 for a pool that does not exist,
        // which is the same answer an empty one gives on purpose: to a caller
        // that is about to roll, "no such pool" and "nothing in it" are the
        // same fact.
        uint32_t PoolSize(std::string_view pool) const;

        // One item out of `pool`, weighted by the rows' `weight`. 0 when the
        // pool is empty or unknown - the caller drops nothing rather than
        // item 0.
        //
        // With V2.Loot.ClassFilter on (D5) the candidates are first narrowed
        // to what `looter` can use, and with `profile` set (Round E / WP9) to
        // what its stat line suits as well. THREE stages, narrowest first,
        // because a run that pays no gear at all is worse than a run that pays
        // the wrong gear:
        //
        //   1. class + race + profile
        //   2. class + race        - the profile is dropped, and only it
        //   3. the raw pool        - the pre-D5 behaviour, last resort
        //
        // The order is the whole design: the profile is the setting the player
        // chose most recently and the one they can change in a second, so it is
        // the first thing given up. Dropping the class instead would hand a
        // rogue a plate helm to satisfy a stat preference nobody has any more.
        uint32_t RollGear(std::string_view pool, Player const* looter,
                          uint8_t profile = PD_STAT_PROFILE_OFF) const;

        // The same draw over two pools at once - HC5_EPIC + RAID_N is one
        // reward tier, not two, so a chest must not first pick a pool (which
        // would pay the 158-row pool as often as the 2 235-row one) and then
        // pick an item. The weight of every entry of both pools competes in
        // ONE walk, and the three stages above are taken over both pools
        // together for the same reason.
        uint32_t RollGearUnion(std::string_view a, std::string_view b,
                               Player const* looter,
                               uint8_t profile = PD_STAT_PROFILE_OFF) const;

        // One material, weighted, restricted to the expansions in the mask
        // (F1 narrows it; the default is all three). No class filter: a
        // material fits everyone by definition.
        uint32_t RollMaterial(
            uint8_t expansionMask = PD_LOOT_EXPANSION_ALL) const;

        // Every bonus row that this run's difficulty unlocks AND that hits
        // its own roll, scaled by the D8 room factor. Independent rolls, so
        // the answer may hold none, one or all of them.
        std::vector<LootBonusHit> RollBonus(uint8_t difficulty,
                                            int roomFactorX100,
                                            Player const* looter) const;

        // D5, the whole class fit: AllowableClass AND AllowableRace (the T9
        // sets exist per faction, so race is a real gate) AND the armour /
        // weapon table. The thinking half is FitsClassRaw in
        // generator/PDv2GameMath.h, where the harness can pin it; this is the
        // two-line wrapper that reads the item.
        static bool ItemFitsPlayer(ItemTemplate const* proto, uint8_t classId,
                                   uint32_t raceMask);

        // WP9's half of the same split, and THE ONLY place in the module that
        // reads ItemTemplate::ItemStat[]. Called once per pool row at Load(),
        // never at roll time; what the rolls see is the byte it returns and
        // FitsProfileRaw in generator/PDv2GameMath.h, where the harness pins
        // the rule.
        static uint8_t StatMaskFor(ItemTemplate const* proto);

        // The effective stat profile of a roll for this looter: the account's
        // chosen profile if the character carries Discerning Eye, and Off if
        // it does not. ONE implementation for all three injection sites (the
        // two caches and the boss), because the unlock rule is exactly the
        // kind of two-line expression that grows a third, subtly different
        // copy - and the SET handler enforcing the same rule server-side is
        // already the second half of it.
        //
        // Note what is per WHAT: the aura is per CHARACTER and the knob is per
        // ACCOUNT, deliberately. The choice is shared across an account; each
        // character earns the right to make it on its own.
        static uint8_t StatProfileFor(Player const* looter);

        // The `.pdungeon v2 info` line: "pools 6 - HC5_EPIC 158 - ... -
        // bonus 9". 0 anywhere in it means the SQL never reached the world DB
        // and that source of loot is silently paying nothing.
        std::string Describe() const;

    private:
        // One pool, in the order the SELECT returned it. A VECTOR and not a
        // map: there are six pools, a linear scan over six short strings is
        // cheaper than hashing one, and the load order is what Describe
        // prints.
        struct LootPool
        {
            std::string name;
            std::vector<LootPoolEntry> entries;
        };

        void LoadPools();
        void LoadBonus();

        LootPool const* FindPool(std::string_view name) const;
        LootPool& PoolFor(std::string const& name);

        // Appends the entries of `pool` a roll may pick to `out`. `filterFor`
        // null means "no class filter" - the whole pool - which is what both
        // the disabled filter and the last-resort fallback pass;
        // `profile` PD_STAT_PROFILE_OFF likewise means "no stat filter", which
        // is what the middle stage passes with the class filter still on.
        // POINTERS, not copies: the union roll draws over two pools as one
        // set, and the filtered roll must not copy a category string per
        // candidate on every kill.
        static void Collect(LootPool const& pool, Player const* filterFor,
                            uint8_t expansionMask, uint8_t profile,
                            std::vector<LootPoolEntry const*>& out);

        // The weighted walk itself: urand(1, total) and step through the
        // candidates until the roll runs out. 0 for an empty set or one whose
        // weights are all 0 (which is also what keeps urand's min <= max).
        static uint32_t PickWeighted(
            std::vector<LootPoolEntry const*> const& candidates);

        std::vector<LootPool> _pools;
        std::vector<LootBonusRow> _bonus;
    };
}

#define sPDv2LootMgr PDungeon::PDv2LootMgr::instance()

#endif
