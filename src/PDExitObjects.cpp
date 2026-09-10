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
#include "Log.h"
#include "ObjectMgr.h"
#include "PDDefines.h"
#include "PDMgr.h"
#include "PDv2Mgr.h"
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
// would cost a cmake re-configure for a couple of dozen lines.
//
// A GameObjectScript rather than the FL-wide Data2/`event_scripts` recipe the
// stock Azealia portals use (777000, 222000, 223000): that recipe needs a
// world-DB script row per portal, and this one needs no row at all.
//
// The class name says Azealia because C8's destination was Azealia and because
// the string IS the ScriptName gameobject_template 910067 registers - renaming
// it would be an SQL change plus a client cache bump. Since Round E / WP10 the
// destination is a game_tele row named by V2.Finale.TeleName; read the hook,
// not the name.
class go_pdungeon_azealia_portal : public GameObjectScript
{
public:
    go_pdungeon_azealia_portal() : GameObjectScript("go_pdungeon_azealia_portal") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        // Round E / WP10: WHERE this portal goes is world data, not a literal.
        // The destination is one row of acore_world.game_tele, named by
        // V2.Finale.TeleName and looked up at click time - so the arrival spot
        // is the same thing `.tele <name>` reaches, moves when an operator
        // moves that row, and needs no rebuild to retarget.
        //
        // exactSearch, deliberately: GetGameTele's forgiving mode returns the
        // FIRST substring match it walks into over an unordered_map, so a
        // partial name (and an empty one, which every row contains) would pick
        // an arbitrary destination that could differ between two starts of the
        // same worldserver. A name is either a row or it is a mistake.
        std::string const& teleName = sPDv2Mgr->GetConfig().finaleTeleName;
        if (GameTele const* tele = sObjectMgr->GetGameTele(teleName, true))
        {
            player->TeleportTo(tele->mapId, tele->position_x, tele->position_y,
                               tele->position_z, tele->orientation);
            return true;
        }

        // Deliberately NOT behind PDv2Debug and deliberately once per click: a
        // mis-set destination is an operator mistake that otherwise shows up
        // as "the portal drops me somewhere else" weeks later, and the player
        // still has to get out of the dungeon in the meantime. The fallback is
        // the C8 destination this portal shipped with - game_tele 20040
        // `flraidazealia`, map 727 = Azealia - which is where all nine FL
        // dungeon exits already land.
        LOG_WARN(PDungeon::PD_LOG,
                 "PDv2: the finale portal's game_tele \"{}\" "
                 "(ProceduralDungeon.V2.Finale.TeleName) does not exist - "
                 "sending the player to flraidazealia instead", teleName);
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
