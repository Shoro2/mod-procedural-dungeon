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
#include "PDClientLink.h"
#include "PDDefines.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "PDv2UILink.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "generator/PDBlockPlan.h"

#include <sstream>
#include <string>
#include <vector>

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
            { "gen",    HandleV2GenCommand,    SEC_GAMEMASTER, Console::Yes },
            { "enter",  HandleV2EnterCommand,  SEC_GAMEMASTER, Console::No  },
            { "info",   HandleV2InfoCommand,   SEC_GAMEMASTER, Console::No  },
            // Console::No like its two in-game siblings: the answer is about
            // the dungeon the CALLER stands in, and a console has no instance.
            { "patrol", HandleV2PatrolCommand, SEC_GAMEMASTER, Console::No  }
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

    // Plans a layout, stores it for the account and pushes the manifest.
    //
    // The doing lives in PDv2DoGenerate, which the gen panel's Generate button
    // calls too - ONE implementation, two entry points, so the command and the
    // button can never diverge into two dungeons. What stays here is what only
    // a GM wants: the ascii map and the manifest file the DLL's dev-only LOAD
    // verb reads.
    static bool HandleV2GenCommand(ChatHandler* handler, Optional<uint32> seedArg,
                                   Optional<uint32> themeArg)
    {
        if (!RequireEnabled(handler))
        {
            return true;
        }

        // The theme argument is the GM TEST path - it overrides V2.Theme for
        // this one generation and is then frozen into the account row like any
        // other gen input. Refuse an id the planner has no kit namespace for,
        // or the refusal would surface later as "generation failed".
        int const themeOverride = static_cast<int>(themeArg.value_or(0));
        if (themeOverride != 0 && themeOverride != 1 && themeOverride != 2)
        {
            handler->PSendSysMessage("pdungeon v2: theme {} is unknown (1 = mine, "
                                     "2 = city).", themeOverride);
            return true;
        }

        BlockPlan plan;
        PDv2GenOutcome const outcome =
            PDv2DoGenerate(handler->GetPlayer(), seedArg.value_or(0), &plan,
                           themeOverride);
        if (!outcome.ok)
        {
            handler->PSendSysMessage("pdungeon v2: {}.", outcome.error);
            return true;
        }

        std::istringstream dump(AsciiBlockDump(plan));
        std::string line;
        while (std::getline(dump, line))
        {
            handler->SendSysMessage(line.c_str());
        }

        handler->PSendSysMessage("pdungeon v2: seed {} theme {} -> {} blocks ({} rooms, {} corridors).",
                                 outcome.seed, outcome.theme, outcome.blocks, outcome.rooms,
                                 outcome.blocks - outcome.rooms);

        if (outcome.pushed)
        {
            handler->SendSysMessage("pdungeon v2: layout pushed to your client - "
                                    "`.pdungeon v2 info` shows when it is READY.");
        }
        else
        {
            handler->PSendSysMessage("pdungeon v2: push failed ({}).", outcome.pushError);
        }

        // Dev fallback while the addon path earns its T2: the file the DLL's
        // LOAD verb can read by hand.
        std::string path;
        std::string error;
        if (sPDv2Mgr->WriteManifest(plan, 1, path, error))
        {
            handler->PSendSysMessage("pdungeon v2: (dev fallback) manifest also written "
                                     "to {} for `/run --FLPD:LOAD`", path);
        }
        return true;
    }

    // The gate and the teleport live in PDv2DoEnter, shared with the panel's
    // Enter button. GMs bypass OnPlayerCanEnterMap in the core
    // (MapMgr.cpp:158-159), so that helper runs the same gate itself -
    // otherwise the GM the tests are run on would never exercise it, and
    // entering unready CRASHES the client. `enter force` is the one extra this
    // command keeps: it skips the gate for the old DLL-LOAD dev loop.
    static bool HandleV2EnterCommand(ChatHandler* handler, Optional<std::string> forceArg)
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

        bool const force = forceArg && *forceArg == "force";
        if (force)
        {
            handler->SendSysMessage("pdungeon v2: gate SKIPPED by force - your client "
                                    "better be serving this layout.");
        }

        PDv2EnterOutcome const outcome = PDv2DoEnter(player, force);
        if (!outcome.ok)
        {
            handler->PSendSysMessage("pdungeon v2: NOT entering - {}", outcome.error);
            if (!force)
            {
                handler->SendSysMessage("pdungeon v2: `.pdungeon v2 enter force` skips "
                                        "this check (dev only - an unready client "
                                        "CRASHES on this map).");
            }
            return true;
        }

        handler->PSendSysMessage("pdungeon v2: entering map {} at {:.2f} {:.2f} {:.2f}.",
                                 sPDv2Mgr->GetConfig().mapId, outcome.x, outcome.y, outcome.z);
        handler->SendSysMessage("pdungeon v2: if the terrain is missing, the client has not "
                                "loaded this layout's manifest yet.");
        return true;
    }

    // A SNAPSHOT of every patroller in the dungeon the caller stands in, taken
    // the moment they type it - which is the point: the operator sees a mob
    // somewhere it should not be, types this, and the answer is on screen
    // before the mob has moved again.
    //
    // One line per patroller, formatted by the AI (PDv2MobAI::PatrolStateLine,
    // which documents what each field proves). It is a companion to
    // `ProceduralDungeon.V2.Patrol.Debug`, not a replacement: the config key
    // records what HAPPENED over a whole run in the worldserver log, this
    // command answers what IS true right now without touching the log at all.
    static bool HandleV2PatrolCommand(ChatHandler* handler)
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

        // The v2 instance script, not the map id: a GM standing on map 760
        // outside a generated run has no dungeon to report on, and saying so is
        // more useful than an empty list.
        PDv2InstanceScript const* instance =
            dynamic_cast<PDv2InstanceScript const*>(player->GetInstanceScript());
        if (!instance)
        {
            handler->SendSysMessage("pdungeon v2: you are not standing in a v2 dungeon.");
            return true;
        }

        std::vector<std::string> const lines = instance->PatrolSnapshot();
        if (lines.empty())
        {
            handler->SendSysMessage("pdungeon v2: no patrol member alive in this dungeon "
                                    "(one patrol per corridor between two rooms; they are "
                                    "killable).");
            return true;
        }

        handler->PSendSysMessage("pdungeon v2: {} patrol creature(s):", uint32(lines.size()));
        for (std::string const& line : lines)
        {
            handler->SendSysMessage(line.c_str());
        }
        // Named here rather than in the lines, so the reading of the lines is
        // one lookup away when this is the first time an operator sees them.
        handler->SendSysMessage("pdungeon v2: walkable 0 + spline RUNNING = a spline nobody "
                                "owns; walkable 0 + top CHASE = an unreachable chase; "
                                "walkable 1 + following 1 = the module chose that cell.");
        // Round D / D2. The second field of every line is the role: `leader`
        // with the two GLOBAL beat cells its tag carries, or `follower k of
        // guid N`. A follower with top FOLLOW is in formation; top IDLE on a
        // follower means its leader is gone and the file has dissolved.
        handler->SendSysMessage("pdungeon v2: role `leader beat (x,y)->(x,y)` walks the beat; "
                                "`follower k of guid N` walks behind it - top FOLLOW is in "
                                "formation, top IDLE means its leader is gone.");
        return true;
    }

    static bool HandleV2InfoCommand(ChatHandler* handler)
    {
        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        handler->PSendSysMessage("pdungeon v2: {} | map {} | floorZ {:.2f} | rooms {}+{} | "
                                 "field {} blocks | origin ({},{}) | pockets {} | detour {}%",
                                 cfg.enabled ? "enabled" : "disabled", cfg.mapId, cfg.floorZ,
                                 cfg.rooms, cfg.bossRooms, cfg.fieldBlocks,
                                 cfg.originBX, cfg.originBY, cfg.branches, cfg.detourChancePct);
        // Round B / B3-B5, the run-shaping keys. They are read live, so this
        // line is the only place an operator can confirm that the
        // `.reload config` they just ran actually reached the module.
        // No ambush radius on this line since Round C / C2: the trap fires on
        // the corridor block a player stands in, so there is no distance left
        // for an operator to confirm.
        // Round D / D2 appends the file's size ladder to the patrol field:
        // `x1/2/3@50/75` reads "one creature, two from difficulty 50, three
        // from 75", and the two numbers are the LIVE keys rather than the
        // defaults - which is the whole reason this line exists.
        handler->PSendSysMessage("pdungeon v2: barrier {}% | patrol hp {}% x1/2/3@{}/{} "
                                 "follow {:.1f} yd | ambush {}% x{}",
                                 cfg.barrierPct, cfg.patrolHealthMultPct,
                                 cfg.patrolSize2Diff, cfg.patrolSize3Diff,
                                 cfg.patrolFollowDistYd,
                                 cfg.ambushChancePct, cfg.ambushMobs);
        // 0 here means mod_pdungeon_chunk_meta.sql never reached the world DB
        // - the one failure that makes every mob stand still. The second count
        // is the same rows decoded a second time with their KINDS kept (Round
        // B / B1 - what the cache and the spawns stand on: the loop-room chest
        // on `chest`, B2's placement on `boss`/`spawns`, and a vetoed pick on
        // `entry`). Both maps are filled from one row in one loop, so the two
        // numbers MUST match: a malformed `anchors` blob is logged at load and
        // stored empty rather than dropped, so it never shrinks the second
        // count. They are printed side by side so a later change that splits
        // the two load paths shows up on this line instead of in game as rooms
        // whose mobs quietly stand on the overflow ring.
        handler->PSendSysMessage("pdungeon v2: {} walk mask(s) loaded, {} with typed anchors",
                                 uint32(sPDv2Mgr->WalkMaskCount()),
                                 uint32(sPDv2Mgr->RoomAnchorChunkCount()));
        // The same failure class, one table over: 0 packs means
        // mod_pdungeon_packs.sql never landed and every room falls back to the
        // placeholder creature; 0 affixes means mod_pdungeon_affixes.sql did
        // not, and the dungeon runs clean at every difficulty. Both degrade
        // quietly in play, so this line is where they become visible.
        handler->PSendSysMessage("pdungeon v2: {} pack(s), {} affix(es) loaded",
                                 uint32(sPDv2PackMgr->PackCount()),
                                 uint32(sPDv2PackMgr->AffixCount()));
        // Same failure class again: 0 decor rules means mod_pdungeon_decor_rules.sql
        // never landed and dungeons build with no props; 0 critter rules means
        // mod_pdungeon_critters.sql did not, and dungeons build with no ambient
        // life. Neither is fatal, so neither is visible without this line.
        handler->PSendSysMessage("  {} decor rule(s), {} critter rule(s) loaded",
                                 uint32(sPDv2Mgr->DecorRules().size()),
                                 uint32(sPDv2Mgr->CritterRules().size()));
        handler->PSendSysMessage("pdungeon v2: {}",
                                 sPDClientLink->DebugLine(AccountOf(handler)));
        // The panel's side of the same conversation: whether this account's
        // client has ever opened the UI, and what it last asked to change.
        handler->PSendSysMessage("pdungeon v2: {}",
                                 sPDv2UILink->DebugLine(AccountOf(handler)));

        auto const plan = sPDv2Mgr->GetPlan(AccountOf(handler));
        if (!plan)
        {
            handler->SendSysMessage("pdungeon v2: no plan stored for this account.");
            return true;
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;
        sPDv2Mgr->EntranceWorldPos(*plan, x, y, z);
        handler->PSendSysMessage("pdungeon v2: seed {} theme {} | {} blocks | entrance at {:.2f} {:.2f} {:.2f}",
                                 plan->effectiveSeed, plan->config.theme,
                                 uint32(plan->blocks.size()), x, y, z);
        return true;
    }
};

void AddPDv2CommandScripts()
{
    new pdungeon_v2_commandscript();
}
