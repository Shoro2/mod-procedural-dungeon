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

#include "PDv2DecorPlan.h"
#include "PDRandom.h"

#include <algorithm>

namespace PDungeon
{
    namespace
    {
        double const DECOR_PI = 3.14159265358979323846;

        // The block's centre row and column ARE the socket track: every kit
        // corridor is exactly those cells, and every room's doorway stub runs
        // along them. A prop standing there would sit in the one line every
        // player has to walk, so the whole track is off limits.
        int const SOCKET_TRACK = PD_CELLS_PER_BLOCK / 2;

        size_t Index(int row, int col)
        {
            return static_cast<size_t>(row) * PD_CELLS_PER_BLOCK +
                   static_cast<size_t>(col);
        }

        // Fixed neighbour order: N, E, S, W. The FIRST wall a candidate cell
        // touches in this order is the wall it belongs to, so a corner cell
        // with two walls always resolves the same way.
        //
        // `facing` is the world orientation of a prop at that wall foot,
        // turned AWAY from the wall. World +X is north and +Y is west, so
        // north is 0, west is pi/2, south is pi and east is 3pi/2. Both props
        // the first theme ships are radially symmetric and would look the same
        // at any angle; the angle is DERIVED rather than drawn so the decor
        // stream only advances for decisions that can be seen, and so the
        // first wall-mounted prop inherits a correct facing instead of a
        // random one.
        struct WallDir
        {
            int    drow;
            int    dcol;
            double du;
            double dv;
            double facing;
        };

        WallDir const WALL_DIRS[4] = {
            { -1,  0, -DECOR_WALL_NUDGE_YD,  0.0,                  DECOR_PI },
            {  0,  1,  0.0,                  DECOR_WALL_NUDGE_YD,  DECOR_PI * 0.5 },
            {  1,  0,  DECOR_WALL_NUDGE_YD,  0.0,                  0.0 },
            {  0, -1,  0.0,                 -DECOR_WALL_NUDGE_YD,  DECOR_PI * 1.5 }
        };

        struct Candidate
        {
            int row = 0;
            int col = 0;
            int dir = 0;
        };

        // A weight of 0 is read as 1. A rule nobody wants should be deleted
        // from the table, not weighted down to nothing - and a zero total
        // would divide the share computation by zero.
        int RuleWeight(DecorRule const& rule)
        {
            return rule.weight > 0 ? rule.weight : 1;
        }

        double DistanceSq(double au, double av, double bu, double bv)
        {
            double const du = au - bu;
            double const dv = av - bv;
            return du * du + dv * dv;
        }

        // Every WALK cell that touches a WALL cell on one of its four sides,
        // row-major, socket track excluded.
        void CollectWallFeet(std::string const& classes,
                             std::vector<Candidate>& out)
        {
            out.clear();
            for (int row = 0; row < PD_CELLS_PER_BLOCK; ++row)
            {
                for (int col = 0; col < PD_CELLS_PER_BLOCK; ++col)
                {
                    if (row == SOCKET_TRACK || col == SOCKET_TRACK) continue;
                    if (classes[Index(row, col)] != DECOR_CLASS_WALK) continue;

                    for (int d = 0; d < 4; ++d)
                    {
                        int const r = row + WALL_DIRS[d].drow;
                        int const c = col + WALL_DIRS[d].dcol;
                        if (r < 0 || c < 0 ||
                            r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                        {
                            continue;
                        }
                        if (classes[Index(r, c)] != DECOR_CLASS_WALL) continue;

                        Candidate cand;
                        cand.row = row;
                        cand.col = col;
                        cand.dir = d;
                        out.push_back(cand);
                        break;
                    }
                }
            }
        }

        // Locale-proof fixed-format number reader: optional sign, digits,
        // optional '.', digits. std::strtod would do the same, but its decimal
        // point follows the C locale - and a worldserver whose locale was set
        // elsewhere would then read 29.166666 as 29, putting a prop a third of
        // a block from where the kit put its anchor.
        bool ReadNumber(std::string const& text, size_t at, double& value,
                        size_t& end)
        {
            size_t p = at;
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
                                       text[p] == '\n' || text[p] == '\r'))
            {
                ++p;
            }

            bool negative = false;
            if (p < text.size() && (text[p] == '+' || text[p] == '-'))
            {
                negative = text[p] == '-';
                ++p;
            }

            bool anyDigit = false;
            double whole = 0.0;
            while (p < text.size() && text[p] >= '0' && text[p] <= '9')
            {
                whole = whole * 10.0 + static_cast<double>(text[p] - '0');
                anyDigit = true;
                ++p;
            }

            double frac = 0.0;
            double scale = 1.0;
            if (p < text.size() && text[p] == '.')
            {
                ++p;
                while (p < text.size() && text[p] >= '0' && text[p] <= '9')
                {
                    frac = frac * 10.0 + static_cast<double>(text[p] - '0');
                    scale *= 10.0;
                    anyDigit = true;
                    ++p;
                }
            }

            if (!anyDigit)
            {
                return false;
            }

            value = whole + frac / scale;
            if (negative)
            {
                value = -value;
            }
            end = p;
            return true;
        }

        // '"u"' (or '"v"') at `keyAt`, then a colon, then the number.
        bool ReadKeyedNumber(std::string const& text, size_t keyAt,
                             double& value, size_t& end)
        {
            size_t p = keyAt + 3;
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
                                       text[p] == '\n' || text[p] == '\r'))
            {
                ++p;
            }
            if (p >= text.size() || text[p] != ':')
            {
                return false;
            }
            return ReadNumber(text, p + 1, value, end);
        }
    }

    std::string PDv2Classify(uint8_t const* walkMask)
    {
        size_t const cells =
            static_cast<size_t>(PD_CELLS_PER_BLOCK) * PD_CELLS_PER_BLOCK;
        std::string out(cells, DECOR_CLASS_VOID);
        if (!walkMask)
        {
            return out;
        }

        for (int row = 0; row < PD_CELLS_PER_BLOCK; ++row)
        {
            for (int col = 0; col < PD_CELLS_PER_BLOCK; ++col)
            {
                size_t const at = Index(row, col);
                if (walkMask[at])
                {
                    out[at] = DECOR_CLASS_WALK;
                    continue;
                }

                bool touches = false;
                for (int dr = -1; dr <= 1 && !touches; ++dr)
                {
                    for (int dc = -1; dc <= 1 && !touches; ++dc)
                    {
                        if (dr == 0 && dc == 0) continue;
                        int const r = row + dr;
                        int const c = col + dc;
                        if (r < 0 || c < 0 ||
                            r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                        {
                            continue;
                        }
                        touches = walkMask[Index(r, c)] != 0;
                    }
                }
                out[at] = touches ? DECOR_CLASS_WALL : DECOR_CLASS_VOID;
            }
        }
        return out;
    }

    char const* BlockRoleName(BlockRole role)
    {
        switch (role)
        {
            case BlockRole::Room:             return "room";
            case BlockRole::RoomEntrance:     return "room_entrance";
            case BlockRole::RoomBoss:         return "room_boss";
            case BlockRole::CorridorStraight: return "corridor_straight";
            case BlockRole::CorridorCorner:   return "corridor_corner";
            case BlockRole::CorridorT:        return "corridor_t";
            default:                          return "corridor_cross";
        }
    }

    bool DecorRoleMatches(std::string const& roleFilter, char const* roleName)
    {
        if (roleFilter.empty())
        {
            return true;
        }
        if (!roleName)
        {
            return false;
        }
        std::string const name = roleName;
        return name.size() >= roleFilter.size() &&
               name.compare(0, roleFilter.size(), roleFilter) == 0;
    }

    uint32_t DecorSeedFrom(uint32_t layoutSeed)
    {
        return layoutSeed ^ PD_DECOR_SEED_MIX;
    }

    bool DecodeAnchorList(std::string const& json, std::vector<DecorAnchor>& out)
    {
        out.clear();

        size_t at = 0;
        while (true)
        {
            size_t const uKey = json.find("\"u\"", at);
            if (uKey == std::string::npos)
            {
                return true;
            }

            double u = 0.0;
            size_t after = 0;
            if (!ReadKeyedNumber(json, uKey, u, after))
            {
                return false;
            }

            // v always follows u in the kit's own emission order, so the next
            // '"v"' after this '"u"' belongs to the same anchor.
            size_t const vKey = json.find("\"v\"", after);
            if (vKey == std::string::npos)
            {
                return false;
            }

            double v = 0.0;
            if (!ReadKeyedNumber(json, vKey, v, after))
            {
                return false;
            }

            DecorAnchor anchor;
            anchor.u = u;
            anchor.v = v;
            out.push_back(anchor);
            at = after;
        }
    }

    bool DecodePropList(std::string const& json, std::vector<KitProp>& out)
    {
        out.clear();

        size_t at = 0;
        while (true)
        {
            // "e" is the prop entry key and appears nowhere else in the
            // anchors JSON ("entry"'s quote closes after five letters, so the
            // three-byte needle cannot match inside it).
            size_t const eKey = json.find("\"e\"", at);
            if (eKey == std::string::npos)
            {
                return true;
            }

            double entry = 0.0;
            size_t after = 0;
            if (!ReadKeyedNumber(json, eKey, entry, after))
            {
                return false;
            }

            KitProp prop;
            prop.goEntry = static_cast<int>(entry);

            // u, v, z and o follow e in the generator's own emission order.
            char const* keys[4] = { "\"u\"", "\"v\"", "\"z\"", "\"o\"" };
            double vals[4] = { 0.0, 0.0, 0.0, 0.0 };
            bool ok = true;
            for (int k = 0; k < 4; ++k)
            {
                size_t const keyAt = json.find(keys[k], after);
                if (keyAt == std::string::npos ||
                    !ReadKeyedNumber(json, keyAt, vals[k], after))
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
            {
                return false;
            }
            prop.u = vals[0];
            prop.v = vals[1];
            prop.z = vals[2];
            prop.o = vals[3];
            out.push_back(prop);
            at = after;
        }
    }

    std::vector<DecorSpot> BuildDecorPlan(BlockPlan const& plan,
                                          DecorMaskProvider const& maskFor,
                                          DecorAnchorProvider const& anchorsFor,
                                          std::vector<DecorRule> const& rules,
                                          uint32_t layoutSeed)
    {
        std::vector<DecorSpot> out;

        // Rules by ascending id, whatever order the caller stored them in.
        // The SQL loader already asks for that order, but the promise belongs
        // here rather than to a caller who has to remember it.
        std::vector<size_t> byId;
        byId.reserve(rules.size());
        for (size_t i = 0; i < rules.size(); ++i)
        {
            byId.push_back(i);
        }
        std::stable_sort(byId.begin(), byId.end(),
                         [&rules](size_t l, size_t r)
                         {
                             return rules[l].id < rules[r].id;
                         });

        PDRandom rng(DecorSeedFrom(layoutSeed));
        // One warm-up draw. mt19937's first output is a plain function of its
        // seed, so two layouts whose seeds differ in few bits would open with
        // visibly related numbers; discarding one word costs nothing, and the
        // mixing constant plus this discard together are the whole recipe.
        rng.NextUInt32();

        std::vector<Candidate> pool;
        std::vector<size_t> matching;
        std::vector<DecorSpot> placed;

        for (PlacedBlock const& block : plan.blocks)
        {
            uint8_t const* const mask = maskFor ? maskFor(block.chunkId) : nullptr;
            if (!mask)
            {
                // The same degradation the walk grid makes: a chunk the server
                // has no mask for is left undecorated rather than decorated
                // blind. It also costs no draw, so the stream stays a function
                // of the plan and the rules alone.
                continue;
            }

            std::string const classes = PDv2Classify(mask);
            char const* const roleName = BlockRoleName(block.role);
            std::vector<DecorAnchor> const* const anchors =
                anchorsFor ? anchorsFor(block.chunkId) : nullptr;

            CollectWallFeet(classes, pool);

            // Which rules speak for this block, and what their weights add up
            // to. A rule's share of the block's candidate cells is its weight
            // over that total, measured against the pool BEFORE anything was
            // taken - so the shares do not move with the order they are used
            // in, and "the brazier is the lighter rule" means the brazier gets
            // fewer of the room's wall feet than the torch.
            matching.clear();
            int totalWeight = 0;
            for (size_t const i : byId)
            {
                DecorRule const& rule = rules[i];
                // theme 0 = any look, same sentinel the packs use. A
                // rule scoped to one theme left the city with no server
                // decor at all the moment it became the default.
                if (rule.theme != 0 && rule.theme != plan.config.theme)
                    continue;
                if (rule.placement != DECOR_PLACEMENT_WALL_FOOT) continue;
                if (!DecorRoleMatches(rule.roleFilter, roleName)) continue;
                matching.push_back(i);
                totalWeight += RuleWeight(rule);
            }
            if (matching.empty())
            {
                continue;
            }

            int const poolAtStart = static_cast<int>(pool.size());
            for (size_t const i : matching)
            {
                DecorRule const& rule = rules[i];

                // Drawn for every matching rule, pool or no pool, so the
                // stream's position follows the RULES and the plan and not how
                // much wall a particular block happens to have.
                int want = rng.UniformInt(rule.minPerBlock, rule.maxPerBlock);
                if (want < 0)
                {
                    want = 0;
                }
                int const share = (poolAtStart * RuleWeight(rule) +
                                   totalWeight - 1) / totalWeight;
                int const take = want < share ? want : share;

                placed.clear();
                while (static_cast<int>(placed.size()) < take && !pool.empty())
                {
                    int const k =
                        rng.UniformInt(0, static_cast<int>(pool.size()) - 1);
                    Candidate const cand = pool[static_cast<size_t>(k)];

                    // Consumed whether it is used or not, and consumed for
                    // every later rule too. A candidate that came back after a
                    // rejection would make the draw depend on the rejection,
                    // and every prop after it in the block would move.
                    pool.erase(pool.begin() + k);

                    WallDir const& dir = WALL_DIRS[cand.dir];
                    DecorSpot spot;
                    spot.bx = block.bx;
                    spot.by = block.by;
                    spot.ruleId = rule.id;
                    spot.goEntry = rule.goEntry;
                    spot.u = (static_cast<double>(cand.row) + 0.5) *
                             PD_CELL_SIZE_YD + dir.du;
                    spot.v = (static_cast<double>(cand.col) + 0.5) *
                             PD_CELL_SIZE_YD + dir.dv;
                    spot.orientation = dir.facing;

                    bool clear = true;
                    if (anchors)
                    {
                        double const limit =
                            DECOR_ANCHOR_CLEAR_YD * DECOR_ANCHOR_CLEAR_YD;
                        for (DecorAnchor const& anchor : *anchors)
                        {
                            if (DistanceSq(spot.u, spot.v, anchor.u, anchor.v) < limit)
                            {
                                clear = false;
                                break;
                            }
                        }
                    }
                    if (!clear)
                    {
                        continue;
                    }

                    double const spacing = rule.minSpacingYd * rule.minSpacingYd;
                    for (DecorSpot const& other : placed)
                    {
                        if (DistanceSq(spot.u, spot.v, other.u, other.v) < spacing)
                        {
                            clear = false;
                            break;
                        }
                    }
                    if (!clear)
                    {
                        continue;
                    }

                    placed.push_back(spot);
                    out.push_back(spot);
                }
            }
        }

        return out;
    }
}
