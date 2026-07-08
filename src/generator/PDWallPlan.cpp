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

#include "PDWallPlan.h"

namespace PDungeon
{
    WallAxis WallAxisOf(PDLayout const& layout, int x, int y)
    {
        if (layout.At(x, y) != TileType::Wall)
        {
            return WallAxis::None;
        }

        bool const L = layout.At(x - 1, y) == TileType::Wall;
        bool const R = layout.At(x + 1, y) == TileType::Wall;
        bool const U = layout.At(x, y - 1) == TileType::Wall;
        bool const D = layout.At(x, y + 1) == TileType::Wall;

        bool const horizPair = L && R;   // straight-through horizontally
        bool const vertPair = U && D;     // straight-through vertically

        // A straight vertical line passes through; only then does the tile take
        // the vertical axis. A horizontal pair (incl. a + cross) keeps the
        // continuous horizontal line whole and lets the vertical split around it.
        if (vertPair && !horizPair)
        {
            return WallAxis::Vertical;
        }
        if (horizPair)
        {
            return WallAxis::Horizontal;
        }

        bool const horizAny = L || R;
        bool const vertAny = U || D;
        if (horizAny && !vertAny)
        {
            return WallAxis::Horizontal;   // horizontal stub / end
        }
        if (vertAny && !horizAny)
        {
            return WallAxis::Vertical;      // vertical stub / end
        }
        if (horizAny && vertAny)
        {
            return WallAxis::Horizontal;    // L-corner: fixed tiebreak -> horizontal owns it
        }
        return WallAxis::Isolated;          // lone pillar
    }

    std::vector<WallRun> BuildWallRuns(PDLayout const& layout)
    {
        int const W = layout.width;
        int const H = layout.height;

        std::vector<WallAxis> axis(static_cast<size_t>(W) * H, WallAxis::None);
        for (int y = 0; y < H; ++y)          // fixed y-then-x scan
        {
            for (int x = 0; x < W; ++x)
            {
                axis[static_cast<size_t>(y) * W + x] = WallAxisOf(layout, x, y);
            }
        }

        std::vector<bool> consumed(static_cast<size_t>(W) * H, false);
        auto axisAt = [&](int x, int y)
        {
            return layout.InBounds(x, y) ? axis[static_cast<size_t>(y) * W + x] : WallAxis::None;
        };
        auto freeH = [&](int x, int y)
        {
            return axisAt(x, y) == WallAxis::Horizontal && !consumed[static_cast<size_t>(y) * W + x];
        };
        auto freeV = [&](int x, int y)
        {
            return axisAt(x, y) == WallAxis::Vertical && !consumed[static_cast<size_t>(y) * W + x];
        };
        auto consume = [&](int x, int y)
        {
            consumed[static_cast<size_t>(y) * W + x] = true;
        };
        auto isWall = [&](int x, int y)
        {
            return layout.At(x, y) == TileType::Wall;
        };

        std::vector<WallRun> runs;

        // Horizontal runs (only Horizontal-axis tiles, so a vertical wall can
        // never be eaten horizontally).
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                if (!freeH(x, y))
                {
                    continue;
                }
                int length = 0;
                while (freeH(x + length, y))
                {
                    consume(x + length, y);
                    ++length;
                }
                WallRun run;
                run.startX = x;
                run.startY = y;
                run.length = length;
                run.horizontal = true;
                run.abutLow = isWall(x - 1, y);
                run.abutHigh = isWall(x + length, y);
                runs.push_back(run);
                x += length;
            }
        }

        // Vertical runs (only Vertical-axis tiles).
        for (int x = 0; x < W; ++x)
        {
            for (int y = 0; y < H; ++y)
            {
                if (!freeV(x, y))
                {
                    continue;
                }
                int length = 0;
                while (freeV(x, y + length))
                {
                    consume(x, y + length);
                    ++length;
                }
                WallRun run;
                run.startX = x;
                run.startY = y;
                run.length = length;
                run.horizontal = false;
                run.abutLow = isWall(x, y - 1);
                run.abutHigh = isWall(x, y + length);
                runs.push_back(run);
                y += length;
            }
        }

        // Isolated pillars (single tiles with no wall neighbour).
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                if (axisAt(x, y) != WallAxis::Isolated || consumed[static_cast<size_t>(y) * W + x])
                {
                    continue;
                }
                consume(x, y);
                WallRun run;
                run.startX = x;
                run.startY = y;
                run.length = 1;
                run.horizontal = true;
                run.abutLow = false;
                run.abutHigh = false;
                runs.push_back(run);
            }
        }

        return runs;
    }

    GateOpening FindGateOpening(PDLayout const& layout, Doorway const& doorway)
    {
        auto isRoom = [&](int x, int y)
        {
            return layout.At(x, y) == TileType::RoomFloor;
        };
        auto isPath = [&](int x, int y)
        {
            TileType const t = layout.At(x, y);
            return t == TileType::Corridor || t == TileType::Doorway;
        };
        auto isBlocked = [&](int x, int y)
        {
            TileType const t = layout.At(x, y);
            return t == TileType::Wall || t == TileType::Void;
        };
        auto isDoor = [&](int x, int y)
        {
            return layout.At(x, y) == TileType::Doorway;
        };

        // Fixed candidate order: room to N, S, W, E. dir<2 => through-axis is Y
        // (gate spans X); dir>=2 => through-axis is X (gate spans Y). The first
        // doorway tile (tiles are pre-sorted y-then-x) with a wall-capped run
        // wins => deterministic.
        for (TilePos const& t : doorway.tiles)
        {
            for (int dir = 0; dir < 4; ++dir)
            {
                bool const spanX = dir < 2;
                int const roomDX = (dir == 2) ? -1 : (dir == 3) ? 1 : 0;
                int const roomDY = (dir == 0) ? -1 : (dir == 1) ? 1 : 0;
                int const spanDX = spanX ? 1 : 0;
                int const spanDY = spanX ? 0 : 1;

                // through-axis: RoomFloor on one side, a corridor exactly opposite.
                if (!isRoom(t.x + roomDX, t.y + roomDY) || !isPath(t.x - roomDX, t.y - roomDY))
                {
                    continue;
                }

                auto sameOpening = [&](int x, int y)
                {
                    return isDoor(x, y) && isRoom(x + roomDX, y + roomDY) && isPath(x - roomDX, y - roomDY);
                };

                int loX = t.x;
                int loY = t.y;
                while (sameOpening(loX - spanDX, loY - spanDY))
                {
                    loX -= spanDX;
                    loY -= spanDY;
                }
                int hiX = t.x;
                int hiY = t.y;
                while (sameOpening(hiX + spanDX, hiY + spanDY))
                {
                    hiX += spanDX;
                    hiY += spanDY;
                }

                // Genuine opening only: capped by wall/void at BOTH span ends. A
                // corridor hugging a room edge continues as path here and is
                // rejected -> no gate in the middle of a passage.
                if (!isBlocked(loX - spanDX, loY - spanDY) || !isBlocked(hiX + spanDX, hiY + spanDY))
                {
                    continue;
                }

                GateOpening opening;
                opening.valid = true;
                opening.spanAlongX = spanX;
                opening.spanTiles = spanX ? (hiX - loX + 1) : (hiY - loY + 1);
                opening.centerTileX = (loX + hiX) * 0.5f + 0.5f;
                opening.centerTileY = (loY + hiY) * 0.5f + 0.5f;
                opening.anchorX = loX;
                opening.anchorY = loY;
                return opening;
            }
        }
        return GateOpening{};
    }
}
