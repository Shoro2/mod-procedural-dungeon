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

#include "PDv2PackMgr.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "PDDefines.h"
#include "QueryResult.h"
#include "generator/PDRandom.h"
#include "generator/PDv2GameMath.h"

#include <algorithm>

namespace PDungeon
{
    namespace
    {
        // A distinct RNG stream from the layout draw. Mixing the seed with the
        // golden-ratio constant means adding, removing or re-weighting a pack
        // member can never shift the LAYOUT an account already owns - the two
        // draws share a seed but not a sequence.
        uint32_t const SPAWN_STREAM_MIX = 0x9E3779B9u;

        // 01 §8: the band a player picks is one grid step wide, i.e. the five
        // levels [bandMin, bandMin + 4]. Derived rather than restated so the
        // band cannot mean two different things in two files.
        int const BAND_WIDTH = PD_GAME_BAND_STEP - 1;

        void EraseFrom(std::vector<PackMember const*>& pool, PackMember const* m)
        {
            auto it = std::find(pool.begin(), pool.end(), m);
            if (it != pool.end())
            {
                pool.erase(it);
            }
        }

        int EffectiveWeight(PackMember const* m)
        {
            // A zero weight in the table would silently make a member
            // undrawable AND break the running total; treat it as the minimum
            // instead, because "present but never picked" is never what an
            // operator meant by leaving the column at its default.
            return (m && m->weight > 0) ? static_cast<int>(m->weight) : 1;
        }

        PackMember const* WeightedPick(std::vector<PackMember const*> const& pool, PDRandom& rng)
        {
            if (pool.empty())
            {
                return nullptr;
            }

            int total = 0;
            for (PackMember const* m : pool)
            {
                total += EffectiveWeight(m);
            }

            int roll = rng.UniformInt(1, total);
            for (PackMember const* m : pool)
            {
                roll -= EffectiveWeight(m);
                if (roll <= 0)
                {
                    return m;
                }
            }
            return pool.back();
        }

        // Weighted draw WITHOUT replacement: used for the run's creature-type
        // subset, where the same entry must not be drawn twice.
        PackMember const* WeightedTake(std::vector<PackMember const*>& pool, PDRandom& rng)
        {
            PackMember const* picked = WeightedPick(pool, rng);
            EraseFrom(pool, picked);
            return picked;
        }
    }

    PDv2PackMgr* PDv2PackMgr::instance()
    {
        static PDv2PackMgr mgr;
        return &mgr;
    }

    void PDv2PackMgr::LoadFromDB(int theme)
    {
        _packs.clear();

        // The LEFT JOIN on creature_template is the defensive half: pack
        // members point at entries this module does not own, so on any
        // environment without the imported stock some of them simply are not
        // there. ct.entry comes back NULL for those and they are dropped.
        QueryResult result = WorldDatabase.Query(
            "SELECT p.id, p.name, p.level_min, p.level_max, p.unlock_dlvl, "
            "m.entry, m.role, m.casterSpellId, m.weight, ct.entry "
            "FROM pdungeon_packs p "
            "LEFT JOIN pdungeon_pack_members m ON m.packId = p.id "
            "LEFT JOIN creature_template ct ON ct.entry = m.entry "
            "WHERE p.enabled = 1 AND p.theme = {} "
            "ORDER BY p.id, m.entry", theme);
        if (!result)
        {
            LOG_ERROR(PD_LOG, "PDv2: pdungeon_packs has no enabled rows for theme {} - "
                              "mod_pdungeon_packs.sql was not applied, and spawns fall "
                              "back to the placeholder creature", theme);
            return;
        }

        uint32 missing = 0;
        do
        {
            Field* fields = result->Fetch();
            uint32 const packId = fields[0].Get<uint32>();

            if (_packs.empty() || _packs.back().id != packId)
            {
                Pack pack;
                pack.id = packId;
                pack.name = fields[1].Get<std::string>();
                pack.levelMin = fields[2].Get<uint8>();
                pack.levelMax = fields[3].Get<uint8>();
                pack.unlockDlvl = fields[4].Get<uint8>();
                _packs.push_back(pack);
            }

            if (fields[5].IsNull())
            {
                continue;               // a pack with no members yet
            }

            uint32 const entry = fields[5].Get<uint32>();
            if (fields[9].IsNull())
            {
                LOG_ERROR(PD_LOG, "PDv2: pack {} references creature_template {} which does "
                                  "not exist - member dropped", packId, entry);
                ++missing;
                continue;
            }

            PackMember member;
            member.entry = entry;
            member.role = fields[6].Get<uint8>();
            member.casterSpellId = fields[7].Get<uint32>();
            member.weight = fields[8].Get<uint16>();

            // A caster with no spell would stand at range doing nothing at all,
            // which reads in-game as a broken mob rather than a data problem.
            if (member.role == PACK_ROLE_CASTER && !member.casterSpellId)
            {
                LOG_ERROR(PD_LOG, "PDv2: pack {} member {} is a caster with casterSpellId 0 - "
                                  "demoted to melee", packId, entry);
                member.role = PACK_ROLE_MELEE;
            }

            _packs.back().members.push_back(member);
        } while (result->NextRow());

        uint32 members = 0, casters = 0, bosses = 0;
        for (Pack const& p : _packs)
        {
            for (PackMember const& m : p.members)
            {
                ++members;
                if (m.role == PACK_ROLE_CASTER) ++casters;
                else if (m.role == PACK_ROLE_BOSS) ++bosses;
            }
        }

        LOG_INFO(PD_LOG, "PDv2: loaded {} pack(s), {} member(s) ({} caster, {} boss, "
                         "{} dropped as missing) for theme {}",
                 uint32(_packs.size()), members, casters, bosses, missing, theme);

        if (!bosses)
        {
            LOG_WARN(PD_LOG, "PDv2: no role-2 (boss) pack member exists for theme {} - "
                             "boss rooms will be filled with a trash stand-in", theme);
        }
    }

    bool PDv2PackMgr::SelectSpawns(uint32_t seed, SpawnSelectInputs const& in,
                                   std::vector<RoomSpawns>& out) const
    {
        out.clear();

        // Pool: unlocked, and overlapping the level band the player chose. The
        // band is a filter on the PACK, not on the individual creature, so a
        // mixed-level pack is in or out as a whole.
        int const bandLo = in.bandMin;
        int const bandHi = in.bandMin + BAND_WIDTH;

        std::vector<PackMember const*> trash;
        std::vector<PackMember const*> bosses;
        for (Pack const& p : _packs)
        {
            if (static_cast<int>(p.unlockDlvl) > in.unlockedDlvl)
            {
                continue;
            }
            if (static_cast<int>(p.levelMax) < bandLo || static_cast<int>(p.levelMin) > bandHi)
            {
                continue;
            }
            for (PackMember const& m : p.members)
            {
                if (m.role == PACK_ROLE_BOSS)
                {
                    bosses.push_back(&m);
                }
                else
                {
                    trash.push_back(&m);
                }
            }
        }

        if (trash.empty() && bosses.empty())
        {
            return false;
        }

        PDRandom rng(seed ^ SPAWN_STREAM_MIX);

        // The run's creature-type subset: `creatureTypesCap` DISTINCT trash
        // entries, drawn by weight without replacement.
        std::vector<PackMember const*> melee;
        std::vector<PackMember const*> casters;
        for (PackMember const* m : trash)
        {
            (m->role == PACK_ROLE_CASTER ? casters : melee).push_back(m);
        }

        int cap = in.creatureTypesCap;
        if (cap < 1)
        {
            cap = 1;
        }
        if (cap > static_cast<int>(trash.size()))
        {
            cap = static_cast<int>(trash.size());
        }

        std::vector<PackMember const*> subsetMelee;
        std::vector<PackMember const*> subsetCasters;
        std::vector<PackMember const*> remaining = trash;

        // Seed the subset with one of each role before filling it freely. Left
        // to a pure weighted draw, a small cap (dlvl 0-2 allows exactly one
        // creature type) would routinely produce an all-melee or all-caster
        // subset and quietly make the caster ratio a no-op for that run.
        if (cap >= 2 && !melee.empty() && !casters.empty())
        {
            std::vector<PackMember const*> pool = melee;
            if (PackMember const* m = WeightedTake(pool, rng))
            {
                subsetMelee.push_back(m);
                EraseFrom(remaining, m);
            }
            pool = casters;
            if (PackMember const* c = WeightedTake(pool, rng))
            {
                subsetCasters.push_back(c);
                EraseFrom(remaining, c);
            }
        }

        while (static_cast<int>(subsetMelee.size() + subsetCasters.size()) < cap &&
               !remaining.empty())
        {
            PackMember const* m = WeightedTake(remaining, rng);
            if (!m)
            {
                break;
            }
            (m->role == PACK_ROLE_CASTER ? subsetCasters : subsetMelee).push_back(m);
        }

        // A boss room with no boss to put in it is a data problem, not a
        // reason to leave the room empty: the highest-weight melee stands in,
        // once per selection so a full dungeon does not spam the log.
        PackMember const* bossStandIn = nullptr;
        if (bosses.empty() && !trash.empty())
        {
            for (PackMember const* m : trash)
            {
                if (!bossStandIn || EffectiveWeight(m) > EffectiveWeight(bossStandIn))
                {
                    bossStandIn = m;
                }
            }
            LOG_WARN(PD_LOG, "PDv2: no boss in the unlocked packs for band {}..{} - "
                             "creature {} stands in for every boss room",
                     bandLo, bandHi, bossStandIn ? bossStandIn->entry : 0);
        }

        auto pickTrash = [&]() -> PackMember const*
        {
            // Caster or melee by the 01 §8 ratio; when the subset has none of
            // the wanted role the other one answers, because a room short of
            // spawns is worse than a room off-ratio.
            bool const wantCaster = rng.Chance(in.casterPct);
            std::vector<PackMember const*> const& first = wantCaster ? subsetCasters : subsetMelee;
            std::vector<PackMember const*> const& second = wantCaster ? subsetMelee : subsetCasters;
            PackMember const* m = WeightedPick(first, rng);
            return m ? m : WeightedPick(second, rng);
        };

        auto emit = [](PackMember const* m, std::vector<SpawnPick>& picks)
        {
            if (!m)
            {
                return;
            }
            SpawnPick pick;
            pick.entry = m->entry;
            pick.role = m->role;
            pick.casterSpellId = m->casterSpellId;
            picks.push_back(pick);
        };

        int const perRoom = in.spawnsPerRoom > 0 ? in.spawnsPerRoom : 1;
        out.reserve(in.rooms.size());
        for (RoomRequest const& room : in.rooms)
        {
            RoomSpawns spawns;
            spawns.roomIndex = room.roomIndex;
            spawns.picks.reserve(static_cast<size_t>(perRoom));

            int trashWanted = perRoom;
            if (room.isBoss)
            {
                // Exactly one boss, then trash for the remaining slots.
                emit(bosses.empty() ? bossStandIn : WeightedPick(bosses, rng), spawns.picks);
                --trashWanted;
            }
            for (int i = 0; i < trashWanted; ++i)
            {
                emit(pickTrash(), spawns.picks);
            }

            out.push_back(spawns);
        }
        return true;
    }
}
