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

#include "PDv2PackDraw.h"

#include "PDRandom.h"

namespace PDungeon
{
    namespace
    {
        // A distinct RNG stream from the layout draw. Mixing the seed with the
        // golden-ratio constant means adding, removing or re-weighting a pack
        // member can never shift the LAYOUT an account already owns - the two
        // draws share a seed but not a sequence.
        uint32_t const SPAWN_STREAM_MIX = 0x9E3779B9u;

        int EffectiveWeight(PackMember const& m)
        {
            // A zero weight in the table would silently make a member
            // undrawable AND break the running total; treat it as the minimum
            // instead, because "present but never picked" is never what an
            // operator meant by leaving the column at its default.
            return m.weight > 0 ? static_cast<int>(m.weight) : 1;
        }

        PackMember const* WeightedPick(std::vector<PackMember> const& pool, PDRandom& rng)
        {
            if (pool.empty())
            {
                return nullptr;
            }

            int total = 0;
            for (PackMember const& m : pool)
            {
                total += EffectiveWeight(m);
            }

            int roll = rng.UniformInt(1, total);
            for (PackMember const& m : pool)
            {
                roll -= EffectiveWeight(m);
                if (roll <= 0)
                {
                    return &m;
                }
            }
            return &pool.back();
        }
    }

    std::vector<PackMember> const& PackPools::meleeOf(int packId) const
    {
        static std::vector<PackMember> const empty;
        for (PackGroup const& g : meleeByPack)
        {
            if (g.packId == packId)
            {
                return g.members;
            }
        }
        return empty;
    }

    std::vector<PackMember> const& PackPools::casterOf(int packId) const
    {
        static std::vector<PackMember> const empty;
        for (PackGroup const& g : casterByPack)
        {
            if (g.packId == packId)
            {
                return g.members;
            }
        }
        return empty;
    }

    bool PDv2SelectSpawns(uint32_t seed, SpawnSelectInputs const& in,
                          PackPools const& pools, std::vector<SpawnPick>& out,
                          PackMember const** outBossStandIn)
    {
        out.clear();
        if (outBossStandIn)
        {
            *outBossStandIn = nullptr;
        }

        // Aliases, not copies: `pools` outlives this call, so referencing its
        // three role vectors under the names the draw has always used keeps
        // everything below readable against the function this replaced.
        std::vector<PackMember> const& melee = pools.melee;
        std::vector<PackMember> const& casters = pools.caster;
        std::vector<PackMember> const& bosses = pools.boss;

        if (melee.empty() && casters.empty() && bosses.empty())
        {
            // Mirrors what an empty band-filtered pool did before this draw
            // took `pools` as a parameter instead of reading `_packs`
            // directly: nothing survived whatever filter the caller applied
            // while building `pools`, so there is nothing here to pick from.
            return false;
        }

        PDRandom rng(seed ^ SPAWN_STREAM_MIX);

        // THE SPAWN STREAM, in the order it is consumed - this list IS the
        // determinism contract, because every draw below shifts every draw
        // after it:
        //
        //   per room        one PACK draw (Task 13: which pack themes the
        //                   room, uniform over pools.trashPackIds - see the
        //                   draw itself below for why only packs with a
        //                   non-boss member are eligible), THEN one carrier
        //                   Chance + one slot draw (the at-most-one-affix-
        //                   per-room rule, drawn hit or miss so the stream
        //                   stays aligned)
        //   per boss room   additionally one weighted boss pick (no affix -
        //                   bosses are never carriers, see the emit below).
        //                   The boss pick ignores the room's pack: it always
        //                   draws from the role-2 pool across ALL packs, so
        //                   a room's theme never constrains which boss can
        //                   appear
        //   per trash slot  one caster/melee Chance, one weighted entry pick
        //                   - from the room's pack if it has a member of the
        //                   wanted role, from the merged pool otherwise (the
        //                   fallback is per SLOT, never per room)
        //
        // Same seed and same inputs therefore rebuild the identical dungeon,
        // affixed mobs included, across restarts and compilers. The INPUTS are
        // part of that: casterPct, bandMin, the room list and now affixPct all
        // steer the sequence, so changing one re-rolls WHICH creatures a stored
        // seed spawns (the layout itself is untouched - it comes from a
        // different stream). PDRandom::Chance also short-circuits at 0 and 100
        // without drawing, and PDRandom::UniformInt short-circuits the same way
        // whenever lo >= hi - the pack draw hits exactly that case when zero or
        // one pack is eligible, so the pack table's own shape moves the
        // sequence too. Nothing here is a bug; it is why the pack tables and
        // these knobs are startup/config state rather than something a player
        // can nudge.
        //
        // TASK 13 moved this list by inserting the pack draw at the front of
        // every room: every downstream draw now sits one call later than it
        // used to, which re-rolls WHICH creatures an ALREADY-STORED seed
        // spawns. That is accepted, not a bug to work around - the server is
        // not public and character progress is expendable - which is why
        // PD_SPAWN_DRAW_PIN was re-captured by running the draw, not by
        // reasoning about what it should say, in the same commit as this
        // comment.

        // A boss room with no boss to put in it is a data problem, not a
        // reason to leave the room empty: the highest-weight member stands
        // in, once per selection so a full dungeon does not spam the log.
        // Ties go to whichever member loaded FIRST (strict `>`, so a later
        // equal weight never displaces the current stand-in) - which is why
        // this scans pools.trash, the melee/caster INTERLEAVE in loader
        // order, rather than melee then caster: every shipped pack member
        // weighs 100 (mod_pdungeon_packs.sql), so this tie-break is not an
        // edge case, it is the whole rule, and it has to reproduce the
        // pre-refactor single-pool scan (git 862ace7) bit for bit or a
        // stored dungeon's spawns silently move. See PackPools::trash's own
        // comment (PDv2PackDraw.h) for why that order cannot be re-derived
        // from melee and caster once they are split.
        //
        // PDv2PackMgr::SelectSpawns reports this pick via outBossStandIn to
        // LOG_WARN which entry it is about to use - this file is engine-free
        // and cannot log. The pick itself still has to happen HERE, on the
        // seeded stream: if the caller instead substituted a one-element boss
        // pool for the empty case, the boss-room emit below would call
        // WeightedPick on it and consume a draw the empty-pool path never
        // did (PDRandom::UniformInt(lo, hi) only skips drawing when
        // lo >= hi, which a weight other than exactly 1 will not satisfy),
        // desynchronising every draw after it from what this seed produced
        // before the fallback ever existed.
        PackMember const* bossStandIn = nullptr;
        if (bosses.empty() && !(melee.empty() && casters.empty()))
        {
            for (PackMember const& m : pools.trash)
            {
                if (!bossStandIn || EffectiveWeight(m) > EffectiveWeight(*bossStandIn))
                {
                    bossStandIn = &m;
                }
            }
        }
        if (outBossStandIn)
        {
            *outBossStandIn = bossStandIn;
        }

        auto pickTrash = [&](int roomPackId) -> PackMember const*
        {
            // Caster or melee by the 01 §8 ratio, then a weighted draw from
            // that role's pool.
            bool const wantCaster = rng.Chance(in.casterPct);
            // Themed if the room's pack can fill this role, merged if it
            // cannot. Falling back per SLOT keeps the rest of the room
            // themed when a pack happens to have no caster.
            std::vector<PackMember> const& themed =
                wantCaster ? pools.casterOf(roomPackId) : pools.meleeOf(roomPackId);
            std::vector<PackMember> const& pool =
                themed.empty() ? (wantCaster ? pools.caster : pools.melee) : themed;
            // Last resort, unrelated to theming: when even the merged pool for
            // the wanted role is empty, the OTHER role answers, because a room
            // short of spawns is worse than a room off-ratio.
            std::vector<PackMember> const& second = wantCaster ? melee : casters;
            PackMember const* m = WeightedPick(pool, rng);
            return m ? m : WeightedPick(second, rng);
        };

        auto emit = [](PackMember const* m, bool affixed, int packId, std::vector<SpawnPick>& picks)
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
            pick.packId = packId;
            picks.push_back(pick);
        };

        // A normal room is `spawnsPerRoom` trash; a boss room is the boss PLUS
        // `bossRoomAdds` trash, not spawnsPerRoom-minus-the-boss. The two are
        // separate knobs since 2026-08-08: normal rooms were asked to grow to
        // five, boss rooms were asked to stay at three (boss + 2 adds).
        int const perRoom = in.spawnsPerRoom > 0 ? in.spawnsPerRoom : 1;
        int const bossAdds = in.bossRoomAdds > 0 ? in.bossRoomAdds : 0;

        // One flat stream of picks, room after room in `in.rooms` order - the
        // per-room grouping RoomSpawns used to carry is now the caller's job
        // (PDv2PackMgr::SelectSpawns rebuilds it from the same room list, so
        // the boundaries are never ambiguous).
        size_t wanted = 0;
        for (RoomRequest const& room : in.rooms)
        {
            wanted += static_cast<size_t>((room.isBoss ? bossAdds : perRoom) + (room.isBoss ? 1 : 0));
        }
        out.reserve(wanted);

        for (RoomRequest const& room : in.rooms)
        {
            int const trashWanted = room.isBoss ? bossAdds : perRoom;

            // ---- draw 1 of the room: its pack -------------------------------
            // One theme per room. Drawn FIRST, before the boss pick and before
            // the affix roll, so a room's faction is decided before anything
            // that depends on it.
            //
            // Only packs with at least one NON-BOSS member are eligible, and
            // that filter is part of the contract, not an optimisation: with
            // the live tables a uniform draw would send a fifth of all rooms
            // to a pack that has no trash at all and could fill nothing.
            //
            // The draw is consumed UNCONDITIONALLY, hit or miss, exactly like
            // the dead affix chance below it - a room that falls back to the
            // merged pool must move the stream by as much as one that does
            // not, or the two desynchronise.
            int roomPackId = 0;
            {
                int const eligible = static_cast<int>(pools.trashPackIds.size());
                int const k = rng.UniformInt(0, eligible > 0 ? eligible - 1 : 0);
                if (eligible > 0)
                {
                    roomPackId = pools.trashPackIds[static_cast<size_t>(k)];
                }
            }

            if (room.isBoss)
            {
                // Exactly one boss, and it is the room's FIRST pick - the
                // instance script keys run completion on that slot.
                //
                // THE BOSS NOW CARRIES THE ROOM'S AFFIX (operator verdict
                // 2026-08-10, after the first live run on the host). This
                // REVERSES the earlier rule, whose reasoning is kept because it
                // is the thing to re-read if boss fights start feeling like
                // walls: mod-dungeon-challenge excludes bosses from its own
                // affix draw outright (AssignAffixesToCreatures skips
                // isWorldBoss / IsDungeonBoss / rank >= 3), and the worry was
                // that a boss which is also Bigger Boy + Hell Touched ON TOP of
                // the difficulty curve stops being a fight. The counter-argument
                // that won: one affix per platform is the rule players can read,
                // and the boss platform's affix mob should obviously be the
                // boss. The difficulty dial remains the knob if it bites.
                //
                // packId 0, not roomPackId: the boss slot is exempt from
                // theming (see this task's own comment above the pack draw),
                // so it never carries the room's pack id.
                emit(bosses.empty() ? bossStandIn : WeightedPick(bosses, rng), true, 0, out);
            }
            // EXACTLY ONE carrier per room, always - no longer a percentage.
            //
            // History, because the value moved twice: the first implementation
            // rolled per mob and clustered (a 5-mob room could carry 0..5 and
            // read as lopsided), so 2026-08-09 made it "at most one, gated by
            // V2.Affix.Percentage". The live run on 2026-08-10 showed the gate
            // itself is the problem - at 40 % most platforms carry nothing and
            // the affix stops being a thing players learn to look for. Now every
            // platform has its one carrier: a trash slot in a normal room, and
            // the BOSS in a boss room (above), which is why boss rooms no longer
            // draw a carrier among their adds.
            //
            // `V2.Affix.Percentage` is consequently DEAD as a gate. The draw is
            // still consumed so the RNG stream keeps its shape - a constant draw
            // count per room is what makes a seed reproduce the same spawns
            // however the knobs move, and skipping it here would silently
            // re-roll every existing seed.
            (void)rng.Chance(in.affixPct);
            bool const roomHasCarrier = !room.isBoss && trashWanted > 0;
            int const carrierSlot = rng.UniformInt(0, trashWanted > 0 ? trashWanted - 1 : 0);

            for (int i = 0; i < trashWanted; ++i)
            {
                // TWO statements, not one call. C++ leaves the evaluation order
                // of function arguments unspecified, so writing
                // emit(pickTrash(), ...) with a second draw inline would let
                // the compiler decide which draw comes first - and a spawn
                // stream whose order depends on the compiler is not
                // deterministic, which is the one property this whole file
                // exists to keep.
                PackMember const* m = pickTrash(roomPackId);
                bool const affixed = roomHasCarrier && i == carrierSlot;
                emit(m, affixed, roomPackId, out);
            }
        }
        return true;
    }
}
