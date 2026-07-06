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

#ifndef MOD_PDUNGEON_PALETTE_MGR_H
#define MOD_PDUNGEON_PALETTE_MGR_H

#include "Define.h"
#include <array>
#include <string>
#include <vector>

namespace PDungeon
{
    enum class PaletteRole : uint8
    {
        Wall = 0,
        Gate,
        Torch,
        Brazier,
        Chest,
        Shrine,
        ExitPortal,
        EntranceDeco,
        Max
    };

    struct PalettePiece
    {
        uint32 goEntry = 0;
        uint8 lenTiles = 1;
        float rotOffset = 0.0f;
        float zOffset = 0.0f;
    };

    // World-DB driven tile set (table pdungeon_palette). Pieces can be swapped
    // per theme without rebuilding; variety among same-role pieces is chosen
    // deterministically by a caller-provided salt.
    class PDPaletteMgr
    {
    public:
        static PDPaletteMgr* instance();

        void Load(std::string const& theme);
        bool IsLoaded() const { return _loaded; }
        uint32 GetTotalCount() const;

        // Longest wall piece with lenTiles <= maxLen, variant picked by salt.
        PalettePiece const* GetWallPiece(uint8 maxLen, uint32 salt) const;
        PalettePiece const* GetPiece(PaletteRole role, uint32 salt) const;

        std::vector<PalettePiece> const& GetAll(PaletteRole role) const
        {
            return _pieces[static_cast<size_t>(role)];
        }

    private:
        bool _loaded = false;
        std::array<std::vector<PalettePiece>, static_cast<size_t>(PaletteRole::Max)> _pieces;
    };
}

#define sPDPaletteMgr PDungeon::PDPaletteMgr::instance()

#endif
