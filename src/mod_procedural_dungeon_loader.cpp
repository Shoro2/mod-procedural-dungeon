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

void AddPDWorldScripts();
void AddPDMapScripts();
void AddPDCreatureScripts();
void AddPDEntranceNPCScripts();
void AddPDExitObjectScripts();
void AddPDCommandScripts();
void AddPDv2CommandScripts();
void AddPDv2MapScripts();
void AddPDv2CreatureScripts();
// Round E / WP5. Registers ScriptName 'npc_pdungeon_event' for the event
// room's host (creature 910551). A ScriptName a creature_template row names
// but no script registers is a startup LOG_ERROR, so this call and
// data/sql/db-world/mod_pdungeon_event.sql belong together.
void AddPDv2EventNPCScripts();
void AddPDv2ScalingScripts();
void AddPDv2LootScripts();
void AddPDClientLinkScripts();
void AddPDv2UILinkScripts();

// Name contract with the CMake module loader: mod-procedural-dungeon ->
// Addmod_procedural_dungeonScripts().
void Addmod_procedural_dungeonScripts()
{
    AddPDWorldScripts();
    AddPDMapScripts();
    AddPDCreatureScripts();
    AddPDEntranceNPCScripts();
    AddPDExitObjectScripts();
    AddPDCommandScripts();
    AddPDv2CommandScripts();
    AddPDv2MapScripts();
    AddPDv2CreatureScripts();
    AddPDv2EventNPCScripts();
    AddPDv2ScalingScripts();
    AddPDv2LootScripts();
    AddPDClientLinkScripts();
    AddPDv2UILinkScripts();
}
