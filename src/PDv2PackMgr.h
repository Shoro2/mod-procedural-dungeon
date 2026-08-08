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

#include <cstdint>
#include <string>
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
namespace PDungeon
{
    enum PackRole : uint8_t
    {
        PACK_ROLE_MELEE = 0,
        PACK_ROLE_CASTER = 1,
        PACK_ROLE_BOSS = 2
    };

    struct PackMember
    {
        uint32_t entry = 0;
        uint8_t  role = PACK_ROLE_MELEE;
        uint32_t casterSpellId = 0;     // 0 for anything that is not a caster
        uint16_t weight = 100;
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

    struct SpawnPick
    {
        uint32_t entry = 0;
        uint8_t  role = PACK_ROLE_MELEE;
        uint32_t casterSpellId = 0;
    };

    // One room the caller wants filled. `roomIndex` is opaque here and simply
    // handed back, so the instance script can key it however it likes.
    struct RoomRequest
    {
        int  roomIndex = 0;
        bool isBoss = false;
    };

    struct RoomSpawns
    {
        int roomIndex = 0;
        std::vector<SpawnPick> picks;
    };

    struct SpawnSelectInputs
    {
        std::vector<RoomRequest> rooms;
        int spawnsPerRoom = 3;
        int casterPct = 60;             // 01 §8 caster ratio, already clamped
        int bandMin = 76;               // band is [bandMin, bandMin + 4]
        int unlockedDlvl = 0;
    };

    class PDv2PackMgr
    {
    public:
        static PDv2PackMgr* instance();

        // Reads both pack tables for `theme`. Members whose creature_template
        // row is missing are dropped loudly rather than spawned into a
        // "creature does not exist" error later - packs reference entries this
        // module does not own, so a foreign environment WILL be missing some.
        void LoadFromDB(int theme);

        size_t PackCount() const { return _packs.size(); }
        bool Empty() const { return _packs.empty(); }

        // Fills `out` with one entry per requested room. Deterministic: the
        // same seed and inputs always produce the same spawns on any compiler,
        // because every draw goes through PDRandom (PDRandom.h:26-29 explains
        // why std distributions are banned here).
        //
        // Returns false when no pack survives the pool filter, which is the
        // caller's cue to fall back to its placeholder creature.
        //
        // EVERY TRASH SLOT ROLLS INDEPENDENTLY from the whole band-filtered,
        // unlocked pool (operator directive 2026-08-08). There used to be a
        // per-run subset of `creatureTypesCap` distinct entries drawn once and
        // reused for the entire dungeon; the second live test read exactly what
        // that does - "only a few of the available mobs get picked and then
        // only those are used". The boss draw is separate as before: every boss
        // room gets exactly one role-2 entry, drawn fresh.
        bool SelectSpawns(uint32_t seed, SpawnSelectInputs const& in,
                          std::vector<RoomSpawns>& out) const;

    private:
        std::vector<Pack> _packs;
    };
}

#define sPDv2PackMgr PDungeon::PDv2PackMgr::instance()

#endif
