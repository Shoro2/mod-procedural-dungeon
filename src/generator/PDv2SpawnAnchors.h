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

#ifndef MOD_PDUNGEON_V2_SPAWN_ANCHORS_H
#define MOD_PDUNGEON_V2_SPAWN_ANCHORS_H

#include "PDv2WorldMath.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// PDv2 typed kit anchors (Round B / B1). The kit publishes, per chunk, the
// JSON 48_gen_t1_blockkit.py writes into `pdungeon_chunk_meta`.`anchors`:
//   {"entry":{u,v,z},"boss":{u,v,z}|null,"chest":{u,v,z}|null,
//    "spawns":[{u,v,z,"role":"melee|caster|elite|patrol"},...],"props":[...]}
// DecodeAnchorList (PDv2DecorPlan.h) flattens every point into one clearance
// list on purpose; this decoder keeps the KIND, which the spawn veto's
// fallback (entry), the chest (chest) and B2's spawn placement (boss, spawns)
// need - until Round C / C5 the altar read `entry` too. A scanner like
// its sibling, for the same reason: one generated writer, no JSON dependency.
// Engine-free: the harness proves it against kit_meta.json.
//
// Round B / B2 added PlanSpawnPoints at the bottom: given one room's anchors
// and the roles of the picks that room drew, WHERE each pick stands. Pure
// geometry - it draws nothing, so it can never move a layout - and it lives
// here rather than in the instance script so the harness can pin it and sweep
// every chunk the kit ships.
namespace PDungeon
{
    struct SpawnAnchor
    {
        double u = 0.0;             // block-local yards, south
        double v = 0.0;             // block-local yards, east
        std::string role;           // spawns only; empty otherwise
    };

    struct RoomAnchors
    {
        bool hasEntry = false;
        SpawnAnchor entry;
        bool hasBoss = false;
        SpawnAnchor boss;
        bool hasChest = false;
        SpawnAnchor chest;
        std::vector<SpawnAnchor> spawns;
    };

    namespace SpawnAnchorDetail
    {
        // Locale-proof fixed-format number reader: optional sign, digits,
        // optional '.', digits. No exponent - the kit writes plain decimals
        // and nothing else. std::strtod would do the same, but its decimal
        // point follows the C locale, and a worldserver whose locale was set
        // elsewhere would then read the entry anchor's 29.166666 as 29 and
        // seat a vetoed spawn a third of a block off. The identical reasoning
        // (and the identical scanner) sits in PDv2DecorPlan.cpp's ReadNumber; this
        // header cannot call it because it must stay engine-free and
        // header-only.
        inline bool ReadNumberAt(std::string const& json, size_t at, size_t limit,
                                 double& out, size_t& after)
        {
            size_t p = at;
            while (p < limit && (json[p] == ' ' || json[p] == '\t' ||
                                 json[p] == '\n' || json[p] == '\r'))
            {
                ++p;
            }

            bool negative = false;
            if (p < limit && (json[p] == '+' || json[p] == '-'))
            {
                negative = json[p] == '-';
                ++p;
            }

            bool anyDigit = false;
            double whole = 0.0;
            while (p < limit && json[p] >= '0' && json[p] <= '9')
            {
                whole = whole * 10.0 + static_cast<double>(json[p] - '0');
                anyDigit = true;
                ++p;
            }

            double frac = 0.0;
            double scale = 1.0;
            if (p < limit && json[p] == '.')
            {
                ++p;
                while (p < limit && json[p] >= '0' && json[p] <= '9')
                {
                    frac = frac * 10.0 + static_cast<double>(json[p] - '0');
                    scale *= 10.0;
                    anyDigit = true;
                    ++p;
                }
            }

            if (!anyDigit)
            {
                return false;
            }

            out = whole + frac / scale;
            if (negative)
            {
                out = -out;
            }
            after = p;
            return true;
        }

        // The number after `key":` between `from` and `limit`; false if absent.
        inline bool ReadNumberAfter(std::string const& json, size_t from, size_t limit,
                                    char const* key, double& out, size_t& after)
        {
            size_t const k = json.find(key, from);
            if (k == std::string::npos || k >= limit)
            {
                return false;
            }
            size_t const colon = json.find(':', k);
            if (colon == std::string::npos || colon >= limit)
            {
                return false;
            }
            // `limit` is the object's closing brace: a number can never run
            // past it, so the scan is bounded by the object it belongs to.
            return ReadNumberAt(json, colon + 1, limit, out, after);
        }

        // One {"u":..,"v":..,"z":..[,"role":".."]} object whose '{' is at `open`.
        inline bool ReadPoint(std::string const& json, size_t open, SpawnAnchor& out, size_t& close)
        {
            if (open == std::string::npos || open >= json.size() || json[open] != '{')
            {
                return false;
            }
            close = json.find('}', open);
            if (close == std::string::npos)
            {
                return false;
            }
            size_t after = 0;
            if (!ReadNumberAfter(json, open, close, "\"u\"", out.u, after) ||
                !ReadNumberAfter(json, open, close, "\"v\"", out.v, after))
            {
                return false;
            }
            out.role.clear();
            size_t const r = json.find("\"role\"", open);
            if (r != std::string::npos && r < close)
            {
                size_t const colon = json.find(':', r);
                size_t const q1 = (colon == std::string::npos) ? std::string::npos : json.find('"', colon + 1);
                size_t const q2 = (q1 == std::string::npos) ? std::string::npos : json.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos && q2 < close)
                {
                    out.role = json.substr(q1 + 1, q2 - q1 - 1);
                }
            }
            return true;
        }

        // Where the value of a top-level key starts: '{' for an object, 'n'
        // for null, npos when the key is absent.
        inline size_t ValueStart(std::string const& json, char const* key)
        {
            size_t const k = json.find(key);
            if (k == std::string::npos)
            {
                return std::string::npos;
            }
            size_t const colon = json.find(':', k);
            if (colon == std::string::npos)
            {
                return std::string::npos;
            }
            return json.find_first_not_of(" \t\r\n", colon + 1);
        }
    }

    // False only on a malformed blob; an absent or null point simply leaves
    // its has* flag false.
    inline bool DecodeRoomAnchors(std::string const& json, RoomAnchors& out)
    {
        using namespace SpawnAnchorDetail;
        out = RoomAnchors();
        size_t close = 0;
        size_t at = ValueStart(json, "\"entry\"");
        if (at != std::string::npos && json[at] == '{')
        {
            if (!ReadPoint(json, at, out.entry, close)) return false;
            out.hasEntry = true;
        }
        at = ValueStart(json, "\"boss\"");
        if (at != std::string::npos && json[at] == '{')
        {
            if (!ReadPoint(json, at, out.boss, close)) return false;
            out.hasBoss = true;
        }
        at = ValueStart(json, "\"chest\"");
        if (at != std::string::npos && json[at] == '{')
        {
            if (!ReadPoint(json, at, out.chest, close)) return false;
            out.hasChest = true;
        }
        size_t const spawnsKey = json.find("\"spawns\"");
        if (spawnsKey == std::string::npos)
        {
            return true;
        }
        size_t const open = json.find('[', spawnsKey);
        size_t const end = (open == std::string::npos) ? std::string::npos : json.find(']', open);
        if (open == std::string::npos || end == std::string::npos)
        {
            return false;
        }
        size_t p = json.find('{', open);
        while (p != std::string::npos && p < end)
        {
            SpawnAnchor a;
            if (!ReadPoint(json, p, a, close)) return false;
            out.spawns.push_back(a);
            p = json.find('{', close);
        }
        return true;
    }

    // One planned stand, block-local (FLPD-BLOCK-1). The v2 prefix is not
    // decoration: `PDungeon::SpawnPoint` and `PDungeon::PlannedSpawn` are BOTH
    // taken by the v1 tile generator (PDGenTypes.h, PDWorldBuilder.h), and the
    // engine glue includes those headers beside this one, so either plain name
    // is a redefinition that only the worldserver build would catch.
    struct PDv2SpawnPoint
    {
        double u = 0.0;
        double v = 0.0;
    };

    int const SPAWN_ROLE_MELEE = 0;
    int const SPAWN_ROLE_CASTER = 1;
    int const SPAWN_ROLE_BOSS = 2;

    // Where the picks of one room stand (Round B / B2). Boss room: the first
    // pick IS the boss (PDv2PackMgr's contract) and takes the boss anchor.
    // Every other pick takes the first unused spawn anchor whose kit role
    // matches (melee/caster; boss rooms publish "elite", which matches
    // anything), then the first unused anchor of any role, then - overflow
    // only, reachable by raising the conf - the legacy 12 yd circle around
    // the block centre, angle by pick index. Deterministic, no draw.
    //
    // "elite" is a MATCH for any role, not a fallback after one: it is tried in
    // the same pass as the exact role, so on a chunk that published an elite
    // anchor BEFORE a caster one, a caster pick would take the elite and the
    // caster anchor would go to whoever asks next. Unreachable on the shipped
    // kit - across all 244 chunks no chunk mixes "elite" with "melee"/"caster"
    // (ordinary rooms publish 4x melee + 2x caster, boss rooms 4x elite) - and
    // recorded here so a future kit that does mix them is a known case rather
    // than a surprise; the fix would be a two-pass match, exact role first.
    inline std::vector<PDv2SpawnPoint> PlanSpawnPoints(RoomAnchors const& a, bool bossRoom,
                                                       std::vector<int> const& roles)
    {
        std::vector<PDv2SpawnPoint> out;
        std::vector<bool> used(a.spawns.size(), false);
        int const count = static_cast<int>(roles.size());
        for (int i = 0; i < count; ++i)
        {
            if (i == 0 && bossRoom && a.hasBoss)
            {
                out.push_back({ a.boss.u, a.boss.v });
                continue;
            }
            char const* want = roles[static_cast<size_t>(i)] == SPAWN_ROLE_CASTER ? "caster" : "melee";
            int pick = -1;
            for (size_t k = 0; k < a.spawns.size() && pick < 0; ++k)
            {
                if (!used[k] && (a.spawns[k].role == want || a.spawns[k].role == "elite")) pick = static_cast<int>(k);
            }
            for (size_t k = 0; k < a.spawns.size() && pick < 0; ++k)
            {
                if (!used[k]) pick = static_cast<int>(k);
            }
            if (pick >= 0)
            {
                used[static_cast<size_t>(pick)] = true;
                out.push_back({ a.spawns[static_cast<size_t>(pick)].u, a.spawns[static_cast<size_t>(pick)].v });
                continue;
            }
            double const mid = PD_BLOCK_SIZE_YD / 2.0;
            double const angle = 2.0 * 3.14159265358979 * i / count;
            out.push_back({ mid + std::cos(angle) * 12.0, mid + std::sin(angle) * 12.0 });
        }
        return out;
    }
}

#endif
