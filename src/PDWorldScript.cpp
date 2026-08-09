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
#include "DatabaseEnv.h"
#include "PDClientLink.h"
#include "PDDefines.h"
#include "PDMgr.h"
#include "PDPaletteMgr.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

class PDWorldScript : public WorldScript
{
public:
    PDWorldScript() : WorldScript("PDWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        sPDMgr->LoadConfig();
        sPDv2Mgr->LoadConfig();
        sPDClientLink->LoadConfig();
        if (reload && sPDMgr->IsEnabled())
        {
            sPDPaletteMgr->Load(sPDMgr->GetConfig().theme);
        }
    }

    void OnStartup() override
    {
        if (sPDMgr->IsEnabled())
        {
            sPDPaletteMgr->Load(sPDMgr->GetConfig().theme);
        }
        if (sPDv2Mgr->IsEnabled())
        {
            // Startup only, never on `.reload config`: the masks describe the
            // shipped kit, and a kit change means a new client patch anyway -
            // that is a restart. Keeping the table immutable after startup is
            // what lets map threads read it without a lock.
            sPDv2Mgr->LoadChunkMeta();

            // Same startup-only rule as the walk masks above: the packs are
            // read-only after this point, which is what lets map threads draw
            // spawns from them without a lock.
            sPDv2PackMgr->LoadFromDB(sPDv2Mgr->GetConfig().theme);

            // Rescue sweep: a character SAVED inside the composed-only map
            // crashes its client at the character screen (the client loads
            // the map from the DB position before the server can intervene).
            // The logout hook prevents new cases; this catches characters
            // stranded by a crash or kill while the server was down.
            CharacterDatabase.Execute(
                "UPDATE characters c JOIN character_homebind h ON c.guid = h.guid "
                "SET c.map = h.mapId, c.zone = h.zoneId, c.position_x = h.posX, "
                "c.position_y = h.posY, c.position_z = h.posZ, c.instance_id = 0 "
                "WHERE c.map = {}",
                sPDv2Mgr->GetConfig().mapId);
        }
    }
};

class PDPlayerScript : public PlayerScript
{
public:
    PDPlayerScript() : PlayerScript("PDPlayerScript", { PLAYERHOOK_ON_LOGIN }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (sPDMgr->IsEnabled() && sPDMgr->GetConfig().announce)
        {
            ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00mod-procedural-dungeon|r module.");
        }
    }
};

void AddPDWorldScripts()
{
    new PDWorldScript();
    new PDPlayerScript();
}
