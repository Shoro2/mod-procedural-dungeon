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

#ifndef MOD_PDUNGEON_DEFINES_H
#define MOD_PDUNGEON_DEFINES_H

#include "Define.h"

namespace PDungeon
{
    // Reserved project id blocks (registered in share-public 06-custom-ids.md):
    // gameobject_template 910000-910099, creature_template 910500-910549 (v1)
    // and 910550-910599 (the v2 sub-block, opened by C8's Chromie).
    enum PDGameObjectEntries : uint32
    {
        GO_WALL_LONG     = 910000,  // 3 tiles
        GO_WALL_ALT      = 910001,  // 3 tiles, visual variant
        GO_WALL_SHORT    = 910002,  // 2 tiles
        GO_WALL_END      = 910003,  // 1 tile
        GO_GATE          = 910010,
        GO_TORCH         = 910020,
        GO_BRAZIER       = 910021,
        GO_CHEST         = 910030,
        GO_SHRINE        = 910031,
        GO_EXIT_PORTAL   = 910032,
        GO_ENTRANCE_DECO = 910033,
        // Round B (2026-09-03): gameplay objects in the 910050+ band that
        // mod_pdungeon_templates_fix.sql owns. 910059's row lands with B3.
        // 910058 (B1's 'Altar of Return') has no constant any more: Round C /
        // C5 deleted the script and the spawns, and the template row survives
        // unspawned so the id stays reserved. Nothing here may reuse it.
        GO_BARRIER       = 910059,  // B3: boss-room barrier
        // Round C / C8 (2026-09-08): the finale's two objects, summoned when
        // the last boss dies and torn down with the run. Rows live in
        // mod_pdungeon_templates_fix.sql; the cache's loot (910068) is in
        // mod_pdungeon_chromie.sql.
        //
        // The Azealia in the NAME is C8's and stays: it is the registered
        // ScriptName in gameobject_template, so renaming it would be an SQL
        // change and a client cache bump for nothing. Since Round E / WP10 the
        // destination is whatever game_tele row V2.Finale.TeleName picks
        // (default `flcapital`), and Azealia is only the fallback.
        GO_AZEALIA_PORTAL = 910067, // C8: type 10, click teleports the clicker
        GO_REWARD_CHEST   = 910068, // C8: "Chromie's Cache", lock 57
        // Round E / WP8 (2026-09-10): the small chest a WON event room leaves
        // on the pilgrim's own square when he walks away. A THIRD chest entry
        // and not a second spawn of GO_CHEST, because the operator asked for a
        // reward that reads as the small one (display 10 Chest01 against 259
        // TreasureChest01) - and PDv2ChestLoot keys its injection on the ENTRY,
        // so a distinct look has to be a distinct id. 910034 is the first free
        // id of the reserved 910034-910039 gap; its template row lives in
        // mod_pdungeon_templates_fix.sql - the only module file that sorts
        // after mod_pdungeon_templates.sql's wide DELETE of 910000-910099 -
        // and its loot rows in mod_pdungeon_event.sql, the same split 910068
        // already has with mod_pdungeon_chromie.sql.
        GO_EVENT_CHEST    = 910034
    };

    enum PDCreatureEntries : uint32
    {
        NPC_TRASH_MELEE  = 910500,
        NPC_TRASH_CASTER = 910501,
        NPC_ELITE        = 910502,
        NPC_BOSS         = 910503,
        NPC_ENTRANCE     = 910510,
        // Round C / C8: the finale's speaker. First entry of the 910550-910599
        // sub-block, so mod_pdungeon_templates.sql's DELETE ... BETWEEN 910500
        // AND 910549 never reaches her; her row lives in its own file,
        // mod_pdungeon_chromie.sql. No ScriptName - PDv2InstanceScript's
        // _finale holds her GUID and speaks her lines.
        NPC_CHROMIE      = 910550,
        // Round E / WP5: the event room's host, the "Weary Pilgrim" a party
        // defends while waves walk in. Second entry of the 910550-910599
        // sub-block; his row lives in its own file, mod_pdungeon_event.sql,
        // for the same reason hers does. ScriptName 'npc_pdungeon_event'
        // (src/PDv2EventNPC.cpp) supplies both his gossip and his AI, and
        // PDv2CreatureAIBinder yields on this entry so he can never be handed
        // PDv2MobAI - unlike Chromie he is attackable, so that AI's proximity
        // aggro would make him hunt players.
        NPC_EVENT_HOST   = 910551
    };

    enum PDSpells : uint32
    {
        SPELL_SHADOW_BOLT = 20791,  // generic mob shadow bolt
        SPELL_WAR_STOMP   = 24375,  // boss aoe stomp

        // The damage half of the Swarming Shadows void zone: aura 71267 on the
        // (unselectable) carrier 38163 triggers this every second - school
        // damage, base 2925 shadow, 4 yd around the carrier (Spell.dbc,
        // measured 2026-08-10). The instance deals it itself, deduplicated;
        // the carriers stay friendly and never tick on their own.
        SPELL_SWARMING_SHADOWS_DMG = 71268
    };

    enum PDMisc : uint32
    {
        WAYPOINT_MOVE_ID_BASE = 910000,  // MovePoint ids used by the module AI
        VALIDATE_DESPAWN_SECS = 15
    };

    // Opening delay a PDv2 kit spell draws at the start of every fight, so a
    // pull does not arrive as one synchronised volley (PDv2CreatureAI.cpp
    // explains why, operator verdict 2026-08-10). Deliberately constants and
    // not conf keys: the requirement is a feel, not a knob anyone tunes per
    // realm. If that changes, they become two more V2.* options.
    enum PDKitOpening : uint32
    {
        PD_KIT_OPENING_MIN_MS = 1000,
        PD_KIT_OPENING_MAX_MS = 2000
    };

    // Round B / B4-B5. The roomIndex of a mob that belongs to no room - the
    // patrol and the ambush. Deliberately a value no dense room index can
    // reach, so every `roomIndex < _roomAlive.size()` guard in the instance
    // script rejects it on its own; the tag's 0 default would have decremented
    // room 0 instead.
    uint32 const PD_ROOM_NONE = 0xFFFFFFFFu;

    // Round E / WP6. The EffectMiscValue of the Forgotten Talents "Restless
    // Echoes" node's passive dummy aura - and the WHOLE contract between the
    // two modules. Deliberately a TAG and not a spell id: mod-forgotten-talents
    // synthesises the spell records for its extension nodes and its id map is
    // sticky rather than frozen, so an id written down over here would be a
    // promise the other module never made. What it does promise is this
    // number in EffectMiscValue_1 and the rank's value in the aura's amount.
    //
    // Nothing in this module includes an FT header for it - the reader is
    // PDv2TaggedAura.h, ten lines of our own - and the tag band 76001-76004 is
    // registered in share-public 06-custom-ids.md.
    //
    // int32 rather than the uint32 above because AuraEffect::GetMiscValue()
    // is signed and this constant exists to be compared against it.
    int32 const PD_TALENT_TAG_RESPAWN = 76001;

    // Round E / WP9, the same contract for the "Discerning Eye" node: owning
    // it UNLOCKS the stat-profile row in the /pd panel and makes the account's
    // chosen profile bite on every gear roll of the run. One rank, so the
    // amount is only ever 0 or 1 and the whole question is "does he own it".
    //
    // The unlock is a per-CHARACTER aura and the profile it unlocks is a
    // per-ACCOUNT column, which is deliberate rather than an oversight: every
    // other knob on this panel is per account, so the CHOICE is shared, while
    // each character has to buy its own way to make it.
    //
    // 76002 and 76003 are Forgotten Talents' own (the two nodes
    // mod-paragon-itemgen reads), which is why this one is 76004 and not the
    // next number after 76001.
    int32 const PD_TALENT_TAG_STATFILTER = 76004;

    char const* const PD_LOG = "module.pdungeon";
}

#endif
