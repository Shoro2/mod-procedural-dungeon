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

// Round C / C8: the way home after the last boss. Click, not automatic - the
// cache beside Chromie is looted first (operator decision 2026-09-08).
//
// PDv2's portal, spawned by PDv2InstanceScript's finale rather than by a
// world-DB spawn, and unrelated to go_pdungeon_exit above (that one is v1's,
// GO 910032, and goes to the player's hearth). It lives in this file because
// this is where the module's GameObject click scripts live and adding a .cpp
// would cost a cmake re-configure for eleven lines.
//
// A GameObjectScript rather than the FL-wide Data2/`event_scripts` recipe the
// stock Azealia portals use (777000, 222000, 223000): that recipe needs a
// world-DB script row per portal, and this one needs no row at all.
class go_pdungeon_azealia_portal : public GameObjectScript
{
public:
    go_pdungeon_azealia_portal() : GameObjectScript("go_pdungeon_azealia_portal") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        // game_tele 20040 `flraidazealia` (acore_world), map 727 = Azealia.
        // The same arrival spot all nine FL dungeon exits already use, so a
        // player leaving the Depths lands where they would from anywhere else.
        player->TeleportTo(727, 13611.1f, 13655.7f, 9.87548f, 4.7776f);
        return true;
    }
};

void AddPDExitObjectScripts()
{
    new go_pdungeon_exit();
    new go_pdungeon_shrine();
    new go_pdungeon_azealia_portal();
}
