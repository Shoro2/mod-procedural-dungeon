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

#include <cstdlib>
#include <string>
#include <vector>

// PDv2 typed kit anchors (Round B / B1). The kit publishes, per chunk, the
// JSON 48_gen_t1_blockkit.py writes into `pdungeon_chunk_meta`.`anchors`:
//   {"entry":{u,v,z},"boss":{u,v,z}|null,"chest":{u,v,z}|null,
//    "spawns":[{u,v,z,"role":"melee|caster|elite|patrol"},...],"props":[...]}
// DecodeAnchorList (PDv2DecorPlan.h) flattens every point into one clearance
// list on purpose; this decoder keeps the KIND, which the altar (entry), the
// chest (chest) and B2's spawn placement (boss, spawns) need. A scanner like
// its sibling, for the same reason: one generated writer, no JSON dependency.
// Engine-free: the harness proves it against kit_meta.json.
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
            char const* begin = json.c_str() + colon + 1;
            char* end = nullptr;
            out = std::strtod(begin, &end);
            if (end == begin)
            {
                return false;
            }
            after = static_cast<size_t>(end - json.c_str());
            return true;
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
}

#endif
