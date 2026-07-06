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

#include "Chat.h"
#include "ChatCommand.h"
#include "GameObject.h"
#include "Map.h"
#include "PDDefines.h"
#include "PDInstanceScript.h"
#include "PDMgr.h"
#include "PDPaletteMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "generator/PDDungeonGenerator.h"
#include <sstream>

using namespace Acore::ChatCommands;
using namespace PDungeon;

class pdungeon_commandscript : public CommandScript
{
public:
    pdungeon_commandscript() : CommandScript("pdungeon_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable pdungeonCommandTable =
        {
            { "gen",      HandlePDungeonGenCommand,      SEC_GAMEMASTER, Console::Yes },
            { "enter",    HandlePDungeonEnterCommand,    SEC_GAMEMASTER, Console::No },
            { "leave",    HandlePDungeonLeaveCommand,    SEC_GAMEMASTER, Console::No },
            { "info",     HandlePDungeonInfoCommand,     SEC_GAMEMASTER, Console::No },
            { "validate", HandlePDungeonValidateCommand, SEC_GAMEMASTER, Console::No }
        };
        static ChatCommandTable commandTable =
        {
            { "pdungeon", pdungeonCommandTable }
        };
        return commandTable;
    }

    // Dry run: generates and prints the layout, no world side effects.
    static bool HandlePDungeonGenCommand(ChatHandler* handler, Optional<uint32> seedArg)
    {
        uint32 const seed = seedArg.value_or(urand(1, 0x7FFFFFFE));
        GenConfig config = sPDMgr->GetConfig().gen;
        config.seed = seed;

        PDLayout layout;
        if (!PDDungeonGenerator::Generate(config, layout))
        {
            handler->PSendSysMessage("pdungeon: generation FAILED for seed {}.", seed);
            return true;
        }

        std::istringstream dump(layout.AsciiDump());
        std::string line;
        while (std::getline(dump, line))
        {
            handler->SendSysMessage(line.c_str());
        }
        handler->PSendSysMessage("pdungeon: seed {} (effective {}): {} rooms, {} doorways, {} decorations, {} mob spawns.",
                                 seed, layout.effectiveSeed, layout.rooms.size(), layout.doorways.size(),
                                 layout.decorations.size(), layout.spawnPoints.size());
        return true;
    }

    static bool HandlePDungeonEnterCommand(ChatHandler* handler, Optional<uint32> seedArg)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            return false;
        }
        uint32 const seed = seedArg.value_or(urand(1, 0x7FFFFFFE));
        if (sPDMgr->StartRun(player, seed, true))
        {
            handler->PSendSysMessage("pdungeon: entering with seed {}.", seed);
        }
        return true;
    }

    static bool HandlePDungeonLeaveCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            return false;
        }
        player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
        return true;
    }

    static bool HandlePDungeonInfoCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            return false;
        }

        PDInstanceScript* script = nullptr;
        if (InstanceMap* map = player->GetMap()->ToInstanceMap())
        {
            script = dynamic_cast<PDInstanceScript*>(map->GetInstanceScript());
        }
        if (!script)
        {
            handler->SendSysMessage("pdungeon: you are not inside a procedural dungeon.");
            return true;
        }

        handler->PSendSysMessage("pdungeon: instance {} seed {} ({}), generated in {}us.",
                                 player->GetMap()->GetInstanceId(), script->GetSeed(),
                                 script->IsCompleted() ? "completed" : "in progress", script->GetGenerationMicros());
        handler->PSendSysMessage("pdungeon: {} objects spawned, {} queued, {} elite rooms until the boss gate opens.",
                                 script->GetSpawnedCount(), script->GetQueuedCount(), script->GetEliteRoomsRemaining());
        return true;
    }

    // Spawns every palette piece in a line in front of the caster and reports
    // whether the display id has a server-side collision model.
    static bool HandlePDungeonValidateCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            return false;
        }
        if (!sPDPaletteMgr->IsLoaded())
        {
            handler->SendSysMessage("pdungeon: palette is not loaded (check pdungeon_palette rows and module enable).");
            return true;
        }

        Map* map = player->GetMap();
        uint32 index = 0;
        for (uint8 role = 0; role < static_cast<uint8>(PaletteRole::Max); ++role)
        {
            for (PalettePiece const& piece : sPDPaletteMgr->GetAll(static_cast<PaletteRole>(role)))
            {
                float const x = player->GetPositionX() + 15.0f + 20.0f * (index % 8);
                float const y = player->GetPositionY() + 25.0f * (index / 8);
                float z = map->GetHeight(x, y, player->GetPositionZ() + 10.0f, true, 60.0f);
                if (z <= INVALID_HEIGHT + 1.0f)
                {
                    z = player->GetPositionZ();
                }

                GameObject* go = map->SummonGameObject(piece.goEntry, x, y, z + piece.zOffset, piece.rotOffset, 0.0f, 0.0f, 0.0f, 0.0f, VALIDATE_DESPAWN_SECS);
                if (!go)
                {
                    handler->PSendSysMessage("|cffff0000entry {}: summon FAILED (gameobject_template missing?)|r", piece.goEntry);
                }
                else if (!go->m_model)
                {
                    handler->PSendSysMessage("|cffff0000entry {} display {}: NO collision model (missing from vmaps/GAMEOBJECT_MODELS)|r", piece.goEntry, go->GetDisplayId());
                }
                else
                {
                    handler->PSendSysMessage("entry {} display {}: collision OK ({})", piece.goEntry, go->GetDisplayId(), go->m_model->IsMapObject() ? "WMO" : "M2");
                }
                ++index;
            }
        }
        handler->PSendSysMessage("pdungeon: {} palette pieces spawned for {} seconds.", index, static_cast<uint32>(VALIDATE_DESPAWN_SECS));
        return true;
    }
};

void AddPDCommandScripts()
{
    new pdungeon_commandscript();
}
