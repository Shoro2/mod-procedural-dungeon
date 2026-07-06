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

#include "PDPaletteMgr.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "PDDefines.h"
#include "QueryResult.h"

namespace PDungeon
{
    namespace
    {
        bool ParseRole(std::string const& text, PaletteRole& role)
        {
            if (text == "WALL")
            {
                role = PaletteRole::Wall;
            }
            else if (text == "GATE")
            {
                role = PaletteRole::Gate;
            }
            else if (text == "TORCH")
            {
                role = PaletteRole::Torch;
            }
            else if (text == "BRAZIER")
            {
                role = PaletteRole::Brazier;
            }
            else if (text == "CHEST")
            {
                role = PaletteRole::Chest;
            }
            else if (text == "SHRINE")
            {
                role = PaletteRole::Shrine;
            }
            else if (text == "EXIT")
            {
                role = PaletteRole::ExitPortal;
            }
            else if (text == "ENTRANCE")
            {
                role = PaletteRole::EntranceDeco;
            }
            else
            {
                return false;
            }
            return true;
        }
    }

    PDPaletteMgr* PDPaletteMgr::instance()
    {
        static PDPaletteMgr instance;
        return &instance;
    }

    void PDPaletteMgr::Load(std::string const& theme)
    {
        for (auto& list : _pieces)
        {
            list.clear();
        }
        _loaded = false;

        QueryResult result = WorldDatabase.Query("SELECT role, go_entry, len_tiles, rot_offset, z_offset FROM pdungeon_palette WHERE theme = '{}' ORDER BY id", theme);
        if (!result)
        {
            LOG_ERROR(PD_LOG, "pdungeon_palette has no rows for theme '{}' - dungeons cannot be built", theme);
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            PaletteRole role = PaletteRole::Wall;
            std::string const roleText = fields[0].Get<std::string>();
            if (!ParseRole(roleText, role))
            {
                LOG_ERROR(PD_LOG, "pdungeon_palette: unknown role '{}' skipped", roleText);
                continue;
            }

            PalettePiece piece;
            piece.goEntry = fields[1].Get<uint32>();
            piece.lenTiles = fields[2].Get<uint8>();
            piece.rotOffset = fields[3].Get<float>();
            piece.zOffset = fields[4].Get<float>();
            _pieces[static_cast<size_t>(role)].push_back(piece);
        } while (result->NextRow());

        bool complete = true;
        PaletteRole const required[] = { PaletteRole::Wall, PaletteRole::Gate };
        for (PaletteRole role : required)
        {
            if (_pieces[static_cast<size_t>(role)].empty())
            {
                LOG_ERROR(PD_LOG, "pdungeon_palette: theme '{}' is missing required role {}", theme, static_cast<uint32>(role));
                complete = false;
            }
        }

        _loaded = complete;
        LOG_INFO(PD_LOG, "Loaded {} palette pieces for theme '{}'", GetTotalCount(), theme);
    }

    uint32 PDPaletteMgr::GetTotalCount() const
    {
        uint32 count = 0;
        for (auto const& list : _pieces)
        {
            count += static_cast<uint32>(list.size());
        }
        return count;
    }

    PalettePiece const* PDPaletteMgr::GetWallPiece(uint8 maxLen, uint32 salt) const
    {
        std::vector<PalettePiece> const& walls = _pieces[static_cast<size_t>(PaletteRole::Wall)];
        uint8 bestLen = 0;
        for (PalettePiece const& piece : walls)
        {
            if (piece.lenTiles <= maxLen && piece.lenTiles > bestLen)
            {
                bestLen = piece.lenTiles;
            }
        }
        if (!bestLen)
        {
            return nullptr;
        }

        std::vector<PalettePiece const*> variants;
        for (PalettePiece const& piece : walls)
        {
            if (piece.lenTiles == bestLen)
            {
                variants.push_back(&piece);
            }
        }
        return variants[salt % variants.size()];
    }

    PalettePiece const* PDPaletteMgr::GetPiece(PaletteRole role, uint32 salt) const
    {
        std::vector<PalettePiece> const& list = _pieces[static_cast<size_t>(role)];
        if (list.empty())
        {
            return nullptr;
        }
        return &list[salt % list.size()];
    }
}
