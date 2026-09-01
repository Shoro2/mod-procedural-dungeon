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

#ifndef MOD_PDUNGEON_V2_PACK_MGR_H
#define MOD_PDUNGEON_V2_PACK_MGR_H

#include "generator/PDv2PackDraw.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// PDv2 creature packs: which existing creature_template entries a run may
// spawn, and which of them each room gets.
//
// Data and selection only - no Creature, no Map, no Unit. The instance script
// turns a SpawnPick into an actual summon; everything up to that point is
// arithmetic over the two pack tables, which is what makes the draw testable
// without a worldserver and safe to call from a map thread.
//
// Loaded once at world startup and READ-ONLY afterwards, exactly like
// PDv2Mgr::LoadChunkMeta - that immutability is what lets map threads query it
// without a lock. Pack changes need a restart, same as a kit change.
//
// PackRole, PackMember, SpawnPick and SpawnSelectInputs live in
// generator/PDv2PackDraw.h, not here: that header is engine-free (no
// AzerothCore include may appear in it), which is what lets
// tests/blockplan_harness.cpp link the actual spawn draw. This file keeps the
// types that stay database-shaped - Pack, MemberSpell, AffixDef - and the
// manager that loads them.
namespace PDungeon
{
    enum MemberSpellSlot : uint8_t
    {
        // The range mob's filler: cast back to back for as long as it holds,
        // with cooldownMs 0 meaning "no artificial gap".
        MEMBER_SPELL_SLOT_FILLER = 0,
        // Woven between fillers by a range mob, cast from melee by everything
        // else, each on its own cooldown.
        MEMBER_SPELL_SLOT_COOLDOWN = 1
    };

    // One row of `pdungeon_member_spells`: what a creature template casts and
    // how often. The SPELLS are stock Spell.dbc entries; this is only the
    // assignment, so a retune is a SQL statement rather than a build - the
    // same split pdungeon_affixes uses.
    //
    // Read once at startup and immutable afterwards, so map threads may query
    // it without a lock.
    struct MemberSpell
    {
        uint32_t spellId = 0;
        uint32_t cooldownMs = 0;
        uint8_t  slot = MEMBER_SPELL_SLOT_COOLDOWN;
        uint8_t  minDiff = 1;   // in the kit when minDiff <= run difficulty
    };

    struct Pack
    {
        uint32_t id = 0;
        std::string name;
        uint8_t levelMin = 1;
        uint8_t levelMax = 80;
        uint8_t unlockDlvl = 0;
        std::vector<PackMember> members;
    };

    // One row of `pdungeon_affixes`: which mod-dungeon-challenge affix spell,
    // and from which point of the difficulty dial it may appear. The SPELLS
    // belong to that module (see mod_pdungeon_affixes.sql); this is only the
    // assignment metadata, so a retune is a SQL statement rather than a build.
    struct AffixDef
    {
        uint8_t  id = 0;
        uint32_t spellId = 0;
        uint8_t  minDiff = 1;
    };

    // One requested room's worth of picks, keyed back to the RoomRequest that
    // asked for it. SelectSpawns rebuilds this grouping from the flat stream
    // PDv2SelectSpawns hands back, using the same per-room slot counts
    // (spawnsPerRoom / bossRoomAdds) it computed the request with, so the
    // boundaries are never ambiguous.
    struct RoomSpawns
    {
        int roomIndex = 0;
        std::vector<SpawnPick> picks;
    };

    class PDv2PackMgr
    {
    public:
        static PDv2PackMgr* instance();

        // Reads both pack tables for `theme`, and the affix table beside them.
        // Members whose creature_template row is missing are dropped loudly
        // rather than spawned into a "creature does not exist" error later -
        // packs reference entries this module does not own, so a foreign
        // environment WILL be missing some.
        void LoadFromDB(int theme);

        size_t PackCount() const { return _packs.size(); }
        bool Empty() const { return _packs.empty(); }

        // The enabled affixes a run at `difficulty` hands to every affixed
        // mob - ALL of them, not one drawn at random, which is how
        // mod-dungeon-challenge does it and what makes the dial's top end feel
        // like a different dungeon rather than a bigger health bar.
        //
        // The ROWS, not the bare spell ids: the spawn site needs the id as well
        // to build a creature's affix mask, and the mask - never the aura - is
        // what every behaviour hook reads (PDv2Affixes.h says why).
        std::vector<AffixDef> AffixesForDifficulty(int difficulty) const;

        // How many of them there are. The panel shows this number, so it is
        // computed here rather than counted in Lua.
        int AffixCountForDifficulty(int difficulty) const;

        size_t AffixCount() const { return _affixes.size(); }

        // Every spell row for a creature template, in the order the AI should
        // consider them: filler first, then the cooldown spells by minDiff.
        // Returns an empty vector for a template with no kit, which is a valid
        // state - such a mob simply auto-attacks.
        //
        // A REFERENCE into the immutable store, not a copy: this is called
        // once per pull from a map thread, and the store never changes after
        // startup (same contract as the pack tables).
        std::vector<MemberSpell> const& MemberSpells(uint32_t entry) const;

        size_t MemberSpellCount() const { return _memberSpellRows; }

        // Fills `out` with one entry per requested room. Deterministic: the
        // same seed and inputs always produce the same spawns on any compiler,
        // because every draw goes through PDRandom (PDRandom.h:26-29 explains
        // why std distributions are banned here).
        //
        // Returns false when no pack survives the pool filter, which is the
        // caller's cue to fall back to its placeholder creature.
        //
        // EVERY ROOM DRAWS ONE PACK (Task 13) and its trash slots prefer that
        // pack's members for their role, falling back to the merged, band-
        // filtered, unlocked pool per SLOT - never per room - only when the
        // pack has nothing of the wanted role. A room therefore reads as one
        // faction instead of a jumble of every unlocked pack's trash side by
        // side. Only packs with at least one non-boss member that survives
        // THIS RUN'S band/unlock filter are ever drawn as a room's theme
        // (PackPools::trashPackIds, re-derived per run from _trashPackIds -
        // see that field's own comment): a boss-only pack, or one this run's
        // band/unlock excludes entirely, could fill nothing and is excluded
        // by construction, not by chance.
        //
        // Before Task 13, every trash slot in the WHOLE RUN rolled
        // independently from that same merged pool (operator directive
        // 2026-08-08, replacing a per-run subset of `creatureTypesCap`
        // distinct entries drawn once and reused for the entire dungeon - the
        // second live test read exactly what that does, "only a few of the
        // available mobs get picked and then only those are used"). That
        // merged pool is still exactly what the per-slot fallback above draws
        // from; only the THEMED preference in front of it is new.
        //
        // Inserting the per-room pack draw moved every downstream draw by one
        // call, which re-rolls WHICH creatures an already-stored seed spawns -
        // accepted, not a bug: the server is not public and character
        // progress is expendable. The boss draw is untouched by any of this:
        // every boss room still gets exactly one role-2 entry, drawn fresh
        // from the pool across ALL packs, so a room's theme never constrains
        // which boss can appear.
        bool SelectSpawns(uint32_t seed, SpawnSelectInputs const& in,
                          std::vector<RoomSpawns>& out) const;

    private:
        void LoadAffixesFromDB();
        void LoadMemberSpellsFromDB();
        // Says out loud, at startup, which range members will fight without a
        // filler. Deliberately here rather than in the AI: the check wants to
        // fire once per template per boot, and doing that from a map thread
        // would need state shared across every creature on the map.
        void ReportFillerlessCasters() const;

        std::vector<Pack> _packs;
        std::vector<AffixDef> _affixes;     // enabled rows only, by minDiff
        std::unordered_map<uint32_t, std::vector<MemberSpell>> _memberSpells;
        size_t _memberSpellRows = 0;

        // Packs with at least one non-boss member for the WHOLE THEME,
        // ascending by id - independent of any run's level band or unlock
        // level. This is the CANDIDATE list only, computed once here because
        // the theme-wide shape never changes between one restart and the
        // next; it is not what SelectSpawns hands the draw. A pack can sit
        // here and still have nothing left to fill a trash slot with once a
        // run's band/unlock filter runs, so SelectSpawns re-derives
        // PackPools::trashPackIds from THIS list filtered against that run's
        // actual meleeByPack/casterByPack groups (FilterEligibleTrashPacks,
        // generator/PDv2PackDraw.cpp) - fixed in the Task 13 fix pass after
        // a review finding: the unfiltered list let a room draw a pack with
        // nothing available, silently falling every slot back to the merged
        // pool.
        std::vector<int> _trashPackIds;
    };
}

#define sPDv2PackMgr PDungeon::PDv2PackMgr::instance()

#endif
