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
#include "PDDefines.h"
#include "PDv2Mgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"

#include <sstream>

using namespace Acore::ChatCommands;
using namespace PDungeon;

// `.pdungeon v2 …` — a separate subtree from v1's commands on purpose. v1 has to
// keep working until its engine glue is replaced, and mixing the two under one
// verb would make it easy to run the wrong one by accident.
//
// GM-only, because there is no entry gate yet: a player without the DLL would
// enter map 760 and find nothing but void.
class pdungeon_v2_commandscript : public CommandScript
{
public:
    pdungeon_v2_commandscript() : CommandScript("pdungeon_v2_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable v2Table =
        {
            { "gen",   HandleV2GenCommand,   SEC_GAMEMASTER, Console::Yes },
            { "enter", HandleV2EnterCommand, SEC_GAMEMASTER, Console::No  },
            { "info",  HandleV2InfoCommand,  SEC_GAMEMASTER, Console::No  }
        };
        static ChatCommandTable pdungeonTable =
        {
            { "v2", v2Table }
        };
        static ChatCommandTable commandTable =
        {
            { "pdungeon", pdungeonTable }
        };
        return commandTable;
    }

private:
    static bool RequireEnabled(ChatHandler* handler)
    {
        if (sPDv2Mgr->IsEnabled())
        {
            return true;
        }
        handler->PSendSysMessage("pdungeon v2: disabled (ProceduralDungeon.V2.Enable = 0).");
        return false;
    }

    static uint32 AccountOf(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    // Plans a layout, stores it for the account and writes the manifest. Prints
    // the LOAD line the operator needs, because until PDClientLink exists the
    // manifest reaches the client by hand.
    static bool HandleV2GenCommand(ChatHandler* handler, Optional<uint32> seedArg)
    {
        if (!RequireEnabled(handler))
        {
            return true;
        }

        uint32 const accountId = AccountOf(handler);
        if (!accountId)
        {
            handler->SendSysMessage("pdungeon v2: needs a logged-in character (plans are per account).");
            return true;
        }

        uint32 const seed = seedArg.value_or(urand(1, 0x7FFFFFFE));
        BlockPlan plan;
        if (!sPDv2Mgr->GeneratePlan(accountId, seed, plan))
        {
            handler->PSendSysMessage("pdungeon v2: generation FAILED for seed {}.", seed);
            return true;
        }

        std::istringstream dump(AsciiBlockDump(plan));
        std::string line;
        while (std::getline(dump, line))
        {
            handler->SendSysMessage(line.c_str());
        }

        uint32 rooms = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId >= 0)
            {
                ++rooms;
            }
        }
        handler->PSendSysMessage("pdungeon v2: seed {} -> {} blocks ({} rooms, {} corridors).",
                                 plan.effectiveSeed, uint32(plan.blocks.size()), rooms,
                                 uint32(plan.blocks.size()) - rooms);

        std::string path;
        std::string error;
        if (sPDv2Mgr->WriteManifest(plan, 1, path, error))
        {
            handler->PSendSysMessage("pdungeon v2: manifest written to {}", path);
            handler->PSendSysMessage("pdungeon v2: in the client, run:  /run --FLPD:LOAD {}",
                                     path);
        }
        else
        {
            handler->PSendSysMessage("pdungeon v2: manifest NOT written ({}).", error);
        }
        return true;
    }

    static bool HandleV2EnterCommand(ChatHandler* handler)
    {
        if (!RequireEnabled(handler))
        {
            return true;
        }

        Player* player = handler->GetPlayer();
        if (!player)
        {
            return false;
        }

        BlockPlan const* plan = sPDv2Mgr->GetPlan(AccountOf(handler));
        if (!plan)
        {
            handler->SendSysMessage("pdungeon v2: no plan for this account yet - run `.pdungeon v2 gen` first.");
            return true;
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!sPDv2Mgr->EntranceWorldPos(*plan, x, y, z))
        {
            handler->SendSysMessage("pdungeon v2: the stored plan has no entrance block.");
            return true;
        }

        // +2 yards so the arrival is above the floor plane rather than in it.
        // The server has no height data for this map, so nothing would catch a
        // spawn placed below it.
        uint32 const mapId = sPDv2Mgr->GetConfig().mapId;
        if (!player->TeleportTo(mapId, x, y, z + 2.0f, 0.0f, TELE_TO_GM_MODE))
        {
            handler->PSendSysMessage("pdungeon v2: teleport to map {} failed - check the "
                                     "instance_template and map_dbc rows.", mapId);
            return true;
        }

        handler->PSendSysMessage("pdungeon v2: entering map {} at {:.2f} {:.2f} {:.2f}.",
                                 mapId, x, y, z + 2.0f);
        handler->SendSysMessage("pdungeon v2: if the terrain is missing, the client has not "
                                "loaded this layout's manifest yet.");
        return true;
    }

    static bool HandleV2InfoCommand(ChatHandler* handler)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        handler->PSendSysMessage("pdungeon v2: {} | map {} | floorZ {:.2f} | rooms {}+{} | "
                                 "field {} blocks | origin ({},{})",
                                 cfg.enabled ? "enabled" : "disabled", cfg.mapId, cfg.floorZ,
                                 cfg.rooms, cfg.bossRooms, cfg.fieldBlocks,
                                 cfg.originBX, cfg.originBY);
        // 0 here means mod_pdungeon_chunk_meta.sql never reached the world DB
        // - the one failure that makes every mob stand still.
        handler->PSendSysMessage("pdungeon v2: {} walk mask(s) loaded | leash {:.0f} yd",
                                 uint32(sPDv2Mgr->WalkMaskCount()), cfg.leashYd);

        BlockPlan const* plan = sPDv2Mgr->GetPlan(AccountOf(handler));
        if (!plan)
        {
            handler->SendSysMessage("pdungeon v2: no plan stored for this account.");
            return true;
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;
        sPDv2Mgr->EntranceWorldPos(*plan, x, y, z);
        handler->PSendSysMessage("pdungeon v2: seed {} | {} blocks | entrance at {:.2f} {:.2f} {:.2f}",
                                 plan->effectiveSeed, uint32(plan->blocks.size()), x, y, z);
        return true;
    }
};

void AddPDv2CommandScripts()
{
    new pdungeon_v2_commandscript();
}
