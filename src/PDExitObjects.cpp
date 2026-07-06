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
#include "PDDefines.h"
#include "PDMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

// Exit portal spawned on completion: brings the user back to their hearth
// location.
class go_pdungeon_exit : public GameObjectScript
{
public:
    go_pdungeon_exit() : GameObjectScript("go_pdungeon_exit") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
        return true;
    }
};

// One-shot shrine: buffs the user, then fades.
class go_pdungeon_shrine : public GameObjectScript
{
public:
    go_pdungeon_shrine() : GameObjectScript("go_pdungeon_shrine") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (uint32 const spellId = sPDMgr->GetConfig().shrineSpellId)
        {
            player->CastSpell(player, spellId, true);
        }
        go->DespawnOrUnsummon(Milliseconds(2000));
        return true;
    }
};

void AddPDExitObjectScripts()
{
    new go_pdungeon_exit();
    new go_pdungeon_shrine();
}
