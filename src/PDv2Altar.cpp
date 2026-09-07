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

#include "GameObject.h"
#include "PDv2InstanceScript.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace PDungeon;

// Round B / B1: the respawn altar. Binding lives on the instance script;
// this script only carries the click to it. Never despawns on use.
class go_pdungeon_altar : public GameObjectScript
{
public:
    go_pdungeon_altar() : GameObjectScript("go_pdungeon_altar") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (auto* script = dynamic_cast<PDv2InstanceScript*>(go->GetInstanceScript()))
        {
            script->BindAltar(player, go->GetGUID());
        }
        return true;
    }
};

void AddPDv2AltarScripts()
{
    new go_pdungeon_altar();
}
