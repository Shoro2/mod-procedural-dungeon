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

        // Same startup site as the packs: everything the spawner draws from is
        // read once here and immutable afterwards, which is what lets map
        // threads query it without a lock.
        LoadAffixesFromDB();
    }

    void PDv2PackMgr::LoadAffixesFromDB()
    {
        _affixes.clear();

        // No theme column: the affix set is a property of the DIFFICULTY dial,
        // not of the kit a dungeon is built from. Ordered by minDiff so the
        // spells are handed to a creature in the order they unlock, which is
        // the order a reader of the log would expect.
        QueryResult result = WorldDatabase.Query(
            "SELECT id, spellId, minDiff FROM pdungeon_affixes "
            "WHERE enabled = 1 ORDER BY minDiff, id");
        if (!result)
        {
            LOG_WARN(PD_LOG, "PDv2: pdungeon_affixes has no enabled rows - dungeons run "
                             "clean (no affixes). Apply mod_pdungeon_affixes.sql if that "
                             "is not what you wanted");
            return;
        }

        do
        {
            Field* fields = result->Fetch();

            AffixDef affix;
            affix.id = fields[0].Get<uint8>();
            affix.spellId = fields[1].Get<uint32>();
            affix.minDiff = fields[2].Get<uint8>();

            // A row with no spell would cost a roll and then do nothing, which
            // reads in-game as an affix that silently fails rather than as a
            // data problem.
            if (!affix.spellId)
            {
                LOG_ERROR(PD_LOG, "PDv2: affix {} has spellId 0 - row dropped", affix.id);
                continue;
            }
            _affixes.push_back(affix);
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} affix(es); the spells belong to "
                         "mod-dungeon-challenge and are only referenced here",
                 uint32(_affixes.size()));
    }

    std::vector<uint32_t> PDv2PackMgr::AffixSpellsForDifficulty(int difficulty) const
    {
        std::vector<uint32_t> out;
        for (AffixDef const& affix : _affixes)
        {
            if (static_cast<int>(affix.minDiff) <= difficulty)
            {
                out.push_back(affix.spellId);
            }
        }
        return out;
    }

    int PDv2PackMgr::AffixCountForDifficulty(int difficulty) const
    {
        int count = 0;
        for (AffixDef const& affix : _affixes)
        {
            if (static_cast<int>(affix.minDiff) <= difficulty)
            {
                ++count;
            }
        }
        return count;
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
        auto fillPool = [&](bool applyBand)
        {
            trash.clear();
            bosses.clear();
            for (Pack const& p : _packs)
            {
                if (static_cast<int>(p.unlockDlvl) > in.unlockedDlvl)
                {
                    continue;
                }
                if (applyBand &&
                    (static_cast<int>(p.levelMax) < bandLo || static_cast<int>(p.levelMin) > bandHi))
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
        };

        fillPool(true);
        if (trash.empty() && bosses.empty() && !_packs.empty())
        {
            // A DATA state must never produce an empty dungeon. v1's imported
            // stock is single-band (every pack is level 80), so an account row
            // still holding cfg_mob_level_min's column default of 1 asks for a
            // band no pack answers - and a player would walk into a dungeon
            // with nothing in it and no way to tell why.
            // mod_pdungeon_account_bandheal.sql repairs the rows; this repairs
            // the RUN, for every other way a band can end up empty (a pack
            // disabled by hand, a theme whose stock does not cover the grid).
            //
            // Deliberately kept trivially readable and one level deep: this
            // path reads the world DB, so the harness cannot reach it, and a
            // fallback nobody can test has to be a fallback anybody can read.
            LOG_WARN(PD_LOG, "PDv2: band {}..{} matches no pack - falling back to all "
                             "unlocked packs", bandLo, bandHi);
            fillPool(false);
        }

        if (trash.empty() && bosses.empty())
        {
            return false;
        }

        PDRandom rng(seed ^ SPAWN_STREAM_MIX);

        // THE SPAWN STREAM, in the order it is consumed - this list IS the
        // determinism contract, because every draw below shifts every draw
        // after it:
        //
        //   per boss room   one weighted boss pick (no affix roll - bosses are
        //                   never affixed, see the emit below)
        //   per trash slot  one caster/melee Chance, one weighted entry pick,
        //                   then one affix Chance
        //
        // Same seed and same inputs therefore rebuild the identical dungeon,
        // affixed mobs included, across restarts and compilers. The INPUTS are
        // part of that: casterPct, bandMin, the room list and now affixPct all
        // steer the sequence, so changing one re-rolls WHICH creatures a stored
        // seed spawns (the layout itself is untouched - it comes from a
        // different stream). PDRandom::Chance also short-circuits at 0 and 100
        // without drawing, so those two values move the sequence as well.
        // Nothing here is a bug; it is why the pack tables and these knobs are
        // startup/config state rather than something a player can nudge.

        // The two role pools, and they are the WHOLE pool: every trash slot in
        // the dungeon draws from these, independently. There is deliberately no
        // per-run subset any more - one was drawn here until 2026-08-08, and it
        // made a 25-creature stock read in-game as a four-creature one.
        std::vector<PackMember const*> melee;
        std::vector<PackMember const*> casters;
        for (PackMember const* m : trash)
        {
            (m->role == PACK_ROLE_CASTER ? casters : melee).push_back(m);
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
            // Caster or melee by the 01 §8 ratio, then a weighted draw from
            // that role's FULL pool; when the pool for the wanted role is empty
            // the other one answers, because a room short of spawns is worse
            // than a room off-ratio.
            bool const wantCaster = rng.Chance(in.casterPct);
            std::vector<PackMember const*> const& first = wantCaster ? casters : melee;
            std::vector<PackMember const*> const& second = wantCaster ? melee : casters;
            PackMember const* m = WeightedPick(first, rng);
            return m ? m : WeightedPick(second, rng);
        };

        auto emit = [](PackMember const* m, bool affixed, std::vector<SpawnPick>& picks)
        {
            if (!m)
            {
                return;
            }
            SpawnPick pick;
            pick.entry = m->entry;
            pick.role = m->role;
            pick.casterSpellId = m->casterSpellId;
            pick.affixed = affixed;
            picks.push_back(pick);
        };

        // A normal room is `spawnsPerRoom` trash; a boss room is the boss PLUS
        // `bossRoomAdds` trash, not spawnsPerRoom-minus-the-boss. The two are
        // separate knobs since 2026-08-08: normal rooms were asked to grow to
        // five, boss rooms were asked to stay at three (boss + 2 adds).
        int const perRoom = in.spawnsPerRoom > 0 ? in.spawnsPerRoom : 1;
        int const bossAdds = in.bossRoomAdds > 0 ? in.bossRoomAdds : 0;
        out.reserve(in.rooms.size());
        for (RoomRequest const& room : in.rooms)
        {
            RoomSpawns spawns;
            spawns.roomIndex = room.roomIndex;
            int const trashWanted = room.isBoss ? bossAdds : perRoom;
            spawns.picks.reserve(static_cast<size_t>(trashWanted + (room.isBoss ? 1 : 0)));

            if (room.isBoss)
            {
                // Exactly one boss, and it is the room's FIRST pick - the
                // instance script keys run completion on that slot. It is
                // never affixed and never rolls for it: mod-dungeon-challenge
                // excludes bosses from its affix draw outright
                // (AssignAffixesToCreatures skips isWorldBoss / IsDungeonBoss /
                // rank >= 3), and a boss that is also Bigger Boy + Hell Touched
                // on top of the difficulty curve is a wall, not a fight.
                emit(bosses.empty() ? bossStandIn : WeightedPick(bosses, rng), false,
                     spawns.picks);
            }
            for (int i = 0; i < trashWanted; ++i)
            {
                // TWO statements, not one call. C++ leaves the evaluation order
                // of function arguments unspecified, so writing
                // emit(pickTrash(), rng.Chance(...)) would let the compiler
                // decide which draw comes first - and a spawn stream whose
                // order depends on the compiler is not deterministic, which is
                // the one property this whole file exists to keep.
                PackMember const* m = pickTrash();
                bool const affixed = rng.Chance(in.affixPct);
                emit(m, affixed, spawns.picks);
            }

            out.push_back(spawns);
        }
        return true;
    }
}
