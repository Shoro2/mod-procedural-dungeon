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
#include "generator/PDv2GameMath.h"
#include "generator/PDv2PackDraw.h"

namespace PDungeon
{
    namespace
    {
        // 01 §8: the band a player picks is one grid step wide, i.e. the five
        // levels [bandMin, bandMin + 4]. Derived rather than restated so the
        // band cannot mean two different things in two files.
        int const BAND_WIDTH = PD_GAME_BAND_STEP - 1;

        // Groups a flat, already band-filtered role pool by the packId each
        // member carries, in the pool's own order - what PackPools::meleeOf/
        // casterOf search. A linear build over a linear store: pack counts
        // are single digits, so this is not the place an unordered_map would
        // buy anything, and PDv2PackDraw.h's determinism rule keeps one out
        // of the generator layer entirely.
        std::vector<PackPools::PackGroup> GroupByPack(std::vector<PackMember> const& flat)
        {
            std::vector<PackPools::PackGroup> groups;
            for (PackMember const& m : flat)
            {
                PackPools::PackGroup* g = nullptr;
                for (PackPools::PackGroup& existing : groups)
                {
                    if (existing.packId == m.packId)
                    {
                        g = &existing;
                        break;
                    }
                }
                if (!g)
                {
                    groups.push_back(PackPools::PackGroup{ m.packId, {} });
                    g = &groups.back();
                }
                g->members.push_back(m);
            }
            return groups;
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

        // FIRST, not last: the pack query below returns early on an environment
        // with no packs, and hanging the affix load off the end of it would
        // make one missing SQL file silently take a second, unrelated feature
        // with it.
        LoadAffixesFromDB();

        // Before the packs, because the member loop below asks the kits
        // whether a role-1 member has a filler of its own.
        LoadMemberSpellsFromDB();

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
            // theme 0 = usable under ANY look; see the column comment in
            // mod_pdungeon_packs.sql. Scoping every pack to one theme meant a
            // new look shipped with nothing but the placeholder creature.
            "WHERE p.enabled = 1 AND p.theme IN (0, {}) "
            "ORDER BY p.id, m.entry", theme);
        if (!result)
        {
            // Say which of the two it actually is. The old message asserted
            // the SQL had not been applied, which was wrong the first time it
            // fired: the file was applied, it simply held no row for the
            // theme that had just become the default.
            QueryResult any = WorldDatabase.Query(
                "SELECT COUNT(*), COALESCE(GROUP_CONCAT(DISTINCT theme), '-') "
                "FROM pdungeon_packs WHERE enabled = 1");
            uint32 const enabled = any ? (*any)[0].Get<uint32>() : 0;
            std::string const themes = any ? (*any)[1].Get<std::string>() : "-";
            if (enabled)
                LOG_ERROR(PD_LOG, "PDv2: pdungeon_packs holds {} enabled pack(s) but "
                                  "none usable for theme {} (they carry theme(s) {}; "
                                  "0 means any) - spawns fall back to the placeholder "
                                  "creature", enabled, theme, themes);
            else
                LOG_ERROR(PD_LOG, "PDv2: pdungeon_packs has no enabled rows at all - "
                                  "mod_pdungeon_packs.sql was not applied, and spawns "
                                  "fall back to the placeholder creature");
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
            member.packId = static_cast<int>(packId);
            member.entry = entry;
            member.role = fields[6].Get<uint8>();
            member.casterSpellId = fields[7].Get<uint32>();
            member.weight = fields[8].Get<uint16>();

            // A range mob with nothing to cast would stand at range doing
            // nothing at all, which reads in-game as a broken mob rather than
            // as a data problem. Both sources have to be empty before that is
            // true: pdungeon_member_spells is the truth for the filler and the
            // casterSpellId column is only its fallback.
            if (member.role == PACK_ROLE_CASTER && !member.casterSpellId &&
                MemberSpells(entry).empty())
            {
                LOG_ERROR(PD_LOG, "PDv2: pack {} member {} is a range mob with casterSpellId 0 "
                                  "and no pdungeon_member_spells rows - demoted to melee",
                          packId, entry);
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

        // _trashPackIds: every pack with at least one non-boss member,
        // ascending, for the WHOLE THEME - band- and unlock-INDEPENDENT.
        // This is the load-time CANDIDATE list only, not PackPools::
        // trashPackIds itself: SelectSpawns filters it down, per run,
        // against that run's actual band/unlock-filtered melee/caster
        // groups before handing it to the draw (FilterEligibleTrashPacks,
        // generator/PDv2PackDraw.cpp - Task 13 fix-pass review finding: a
        // pack can sit in this list and still have nothing left once the
        // band/unlock filter runs). Built here rather than per SelectSpawns
        // call because computing the theme-wide shape does not need to
        // repeat on every draw - only the per-run FILTER does, and that part
        // lives with the draw it feeds. _packs is already ascending by id
        // (the loader's own "ORDER BY p.id, m.entry"), so no sort is needed
        // to keep this list ascending too.
        _trashPackIds.clear();
        for (Pack const& p : _packs)
        {
            for (PackMember const& m : p.members)
            {
                if (m.role != PACK_ROLE_BOSS)
                {
                    _trashPackIds.push_back(static_cast<int>(p.id));
                    break;
                }
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

        ReportFillerlessCasters();
    }

    void PDv2PackMgr::LoadMemberSpellsFromDB()
    {
        _memberSpells.clear();
        _memberSpellRows = 0;

        // ORDER BY is part of the contract, not decoration: the AI casts the
        // FIRST ready row it finds, so the order this comes back in IS the
        // priority order a fight plays out in. slot puts the filler at the
        // front, minDiff makes the always-on ability outrank the gated ones,
        // and spellId breaks the remaining ties so two servers with the same
        // table behave identically.
        QueryResult result = WorldDatabase.Query(
            "SELECT entry, spellId, slot, cooldownMs, minDiff FROM pdungeon_member_spells "
            "WHERE enabled = 1 ORDER BY entry, slot, minDiff, spellId");
        if (!result)
        {
            LOG_WARN(PD_LOG, "PDv2: pdungeon_member_spells has no enabled rows - every mob "
                             "fights with auto-attacks only (range mobs fall back to their "
                             "pack casterSpellId). Apply mod_pdungeon_member_spells.sql if "
                             "that is not what you wanted");
            return;
        }

        uint32 fillers = 0, cooldowns = 0;
        do
        {
            Field* fields = result->Fetch();

            uint32 const entry = fields[0].Get<uint32>();
            MemberSpell spell;
            spell.spellId = fields[1].Get<uint32>();
            spell.slot = fields[2].Get<uint8>();
            spell.cooldownMs = fields[3].Get<uint32>();
            spell.minDiff = fields[4].Get<uint8>();

            // A row with no spell would take a slot in the rotation and then
            // do nothing, which reads in-game as a mob that stutters rather
            // than as a data problem.
            if (!spell.spellId)
            {
                LOG_ERROR(PD_LOG, "PDv2: creature {} has a pdungeon_member_spells row with "
                                  "spellId 0 - row dropped", entry);
                continue;
            }

            // A cooldown spell with no cooldown is a contradiction the AI
            // cannot resolve: it takes the first ready row, so a zero would
            // fire every tick and starve every row behind it - the filler
            // included.
            if (spell.slot != MEMBER_SPELL_SLOT_FILLER && !spell.cooldownMs)
            {
                LOG_ERROR(PD_LOG, "PDv2: creature {} spell {} is a slot-{} row with "
                                  "cooldownMs 0 - row dropped (only the slot-0 filler may "
                                  "have no cooldown)", entry, spell.spellId, spell.slot);
                continue;
            }

            std::vector<MemberSpell>& kit = _memberSpells[entry];
            if (spell.slot == MEMBER_SPELL_SLOT_FILLER)
            {
                // Exactly one filler. A second one could never fire - the AI
                // takes the first slot-0 row and spams it - so say so instead
                // of letting the extra spell quietly not exist.
                if (!kit.empty() && kit.front().slot == MEMBER_SPELL_SLOT_FILLER)
                {
                    LOG_ERROR(PD_LOG, "PDv2: creature {} has more than one slot-0 filler; "
                                      "spell {} dropped, {} keeps the slot",
                              entry, spell.spellId, kit.front().spellId);
                    continue;
                }
                ++fillers;
            }
            else
            {
                ++cooldowns;
            }

            kit.push_back(spell);
            ++_memberSpellRows;
        } while (result->NextRow());

        LOG_INFO(PD_LOG, "PDv2: loaded {} member spell(s) for {} creature(s) - {} filler, "
                         "{} cooldown; the spells are stock Spell.dbc and only referenced here",
                 uint32(_memberSpellRows), uint32(_memberSpells.size()), fillers, cooldowns);
    }

    std::vector<MemberSpell> const& PDv2PackMgr::MemberSpells(uint32_t entry) const
    {
        static std::vector<MemberSpell> const empty;
        auto const it = _memberSpells.find(entry);
        return it != _memberSpells.end() ? it->second : empty;
    }

    void PDv2PackMgr::ReportFillerlessCasters() const
    {
        for (Pack const& pack : _packs)
        {
            for (PackMember const& member : pack.members)
            {
                if (member.role != PACK_ROLE_CASTER)
                {
                    continue;
                }

                std::vector<MemberSpell> const& kit = MemberSpells(member.entry);
                if (!kit.empty() && kit.front().slot == MEMBER_SPELL_SLOT_FILLER)
                {
                    continue;
                }

                // The fallback still gives it something to spam, so this is a
                // warning about the DATA being half applied, not about a mob
                // that is already broken. Without either it IS broken, and the
                // member loop above has already demoted it.
                LOG_WARN(PD_LOG, "PDv2: range member {} has no slot-0 filler in "
                                 "pdungeon_member_spells - falling back to the pack's "
                                 "casterSpellId {}", member.entry, member.casterSpellId);
            }
        }
    }

    void PDv2PackMgr::LoadAffixesFromDB()
    {
        _affixes.clear();

        // Read once at startup and immutable afterwards, exactly like the pack
        // tables - that is what lets map threads query it without a lock.
        //
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

    std::vector<AffixDef> PDv2PackMgr::AffixesForDifficulty(int difficulty) const
    {
        std::vector<AffixDef> out;
        for (AffixDef const& affix : _affixes)
        {
            if (static_cast<int>(affix.minDiff) <= difficulty)
            {
                out.push_back(affix);
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

        // Everything above this line is pool ASSEMBLY: which packs qualify
        // for this run, given the band and the unlock level. Everything the
        // actual draw needs from that assembly is a role-split view, so build
        // one - PackPools - and hand it to PDv2SelectSpawns
        // (generator/PDv2PackDraw.cpp), which does not know a Pack, a level
        // band or a WorldDatabase query exists.
        std::vector<PackMember const*> melee;
        std::vector<PackMember const*> casters;
        for (PackMember const* m : trash)
        {
            (m->role == PACK_ROLE_CASTER ? casters : melee).push_back(m);
        }

        PackPools pools;
        pools.melee.reserve(melee.size());
        for (PackMember const* m : melee)
        {
            pools.melee.push_back(*m);
        }
        pools.caster.reserve(casters.size());
        for (PackMember const* m : casters)
        {
            pools.caster.push_back(*m);
        }
        pools.boss.reserve(bosses.size());
        for (PackMember const* m : bosses)
        {
            pools.boss.push_back(*m);
        }
        // `trash` above is already melee and caster INTERLEAVED in loader
        // order (built straight off `_packs`, which is itself "ORDER BY
        // p.id, m.entry" - see LoadFromDB) - the very members pools.melee
        // and pools.caster hold, just not yet split by role. Carry that
        // order across as pools.trash instead of re-deriving it from the two
        // split vectors: PDv2SelectSpawns's boss-standin tie-break needs the
        // real interleave to reproduce the pre-refactor scan (PDv2PackDraw.h
        // explains why melee-then-caster is not the same order).
        pools.trash.reserve(trash.size());
        for (PackMember const* m : trash)
        {
            pools.trash.push_back(*m);
        }
        pools.meleeByPack = GroupByPack(pools.melee);
        pools.casterByPack = GroupByPack(pools.caster);

        // Review finding (Task 13 fix pass): _trashPackIds is the theme-wide
        // candidate list from LoadFromDB, independent of the band/unlock
        // filter fillPool just applied above - handing it through as-is
        // could draw a pack absent from meleeByPack/casterByPack, and every
        // trash slot in that room would then fall back to the merged pool,
        // silently re-creating the jumble Task 13 removes. Filter it down to
        // this run's actually-fillable packs, from the SAME groups the draw
        // itself reads via meleeOf/casterOf - not a second, hand-rolled
        // filter that could drift from them. It does not bite today (every
        // shipped pack is level_min = level_max = 80, unlock_dlvl = 0, so
        // the filtered and unfiltered lists are identical), but level_min/
        // level_max/unlock_dlvl are real columns the content design intends
        // to use (mod_pdungeon_packs.sql).
        pools.trashPackIds = FilterEligibleTrashPacks(_trashPackIds, pools);

        // The boss-standin fallback runs inside PDv2SelectSpawns, on the
        // seeded stream, at the exact point the original ternary did - that
        // file's own comment says why the pick cannot move here without
        // shifting every draw after it. PDv2PackDraw.cpp is engine-free and
        // cannot log, so it hands the pick it made back through
        // outBossStandIn and THIS function logs it - one scan, one place,
        // rather than a second copy kept in sync by hand (which had already
        // drifted from the original once before this fix).
        std::vector<SpawnPick> flat;
        PackMember const* bossStandIn = nullptr;
        if (!PDv2SelectSpawns(seed, in, pools, flat, &bossStandIn))
        {
            return false;
        }
        if (bossStandIn)
        {
            LOG_WARN(PD_LOG, "PDv2: no boss in the unlocked packs for band {}..{} - "
                             "creature {} stands in for every boss room",
                     bandLo, bandHi, bossStandIn->entry);
        }

        // PDv2SelectSpawns hands back one flat stream, room after room in
        // `in.rooms` order (that grouping is the caller's job now, per its
        // own header comment). A room's slot count is `trashWanted +
        // (isBoss ? 1 : 0)` UNLESS a role that room needs has nothing to draw
        // from, in which case PDv2SelectSpawns's own `emit` silently skips
        // that slot rather than push a null pick - so the count actually
        // consumed is computed the same way PDv2SelectSpawns decides whether
        // to draw at all, not assumed to always match the request.
        bool const trashAvailable = !(pools.melee.empty() && pools.caster.empty());
        // There used to be a `bossAvailable = !pools.boss.empty() ||
        // trashAvailable` gating bossGot below, but it is provably always
        // true here: the guard at the top of this function
        // (`if (trash.empty() && bosses.empty()) return false;`) already
        // rules out the one case where neither a real boss (pools.boss) nor
        // a stand-in (trashAvailable, which PDv2SelectSpawns falls back to -
        // see PDv2PackDraw.cpp) exists. Every boss room therefore always
        // gets its one slot filled.
        int const perRoom = in.spawnsPerRoom > 0 ? in.spawnsPerRoom : 1;
        int const bossAdds = in.bossRoomAdds > 0 ? in.bossRoomAdds : 0;
        out.reserve(in.rooms.size());
        size_t cursor = 0;
        for (RoomRequest const& room : in.rooms)
        {
            int const trashWanted = room.isBoss ? bossAdds : perRoom;
            size_t const trashGot = trashAvailable ? static_cast<size_t>(trashWanted) : 0;
            size_t const bossGot = room.isBoss ? 1 : 0;

            RoomSpawns spawns;
            spawns.roomIndex = room.roomIndex;
            spawns.picks.reserve(trashGot + bossGot);
            for (size_t i = 0; i < trashGot + bossGot && cursor < flat.size(); ++i, ++cursor)
            {
                spawns.picks.push_back(flat[cursor]);
            }
            out.push_back(spawns);
        }
        return true;
    }
}
