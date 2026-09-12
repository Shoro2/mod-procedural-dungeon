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
#include "PDv2LootMgr.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "PDv2UILink.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
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
            { "patrol", HandleV2PatrolCommand, SEC_GAMEMASTER, Console::No  },
            // Round E / R1. An RBAC PERMISSION id where its four siblings
            // carry a SEC_* LEVEL, which is legal and deliberate rather than a
            // copy slip: the core reads this field as a permission id when it
            // is >= rbac::RBAC_PERM_COMMAND_RBAC (200) and as a security level
            // below that (ChatCommand.cpp, IsInvokerVisible). MODIFY is the
            // right gate for a command whose whole job is to set a value by
            // hand, and it reaches a GM anyway - the default GM role links it.
            //
            // Console::No because the cap it sets belongs to the INVOKING
            // account, and a console has none.
            { "cap",    HandleV2CapCommand,
              rbac::RBAC_PERM_COMMAND_MODIFY, Console::No  }
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

        // The theme argument is the GM TEST path - it overrides V2.Theme (and
        // the account's own cfg_theme) for this one generation and is then
        // frozen into the account row like any other gen input. Refuse an id
        // the planner has no kit namespace for, or the refusal would surface
        // later as "generation failed".
        //
        // Round F / F1: asked of the KIT rather than of a literal 1-or-2 list.
        // The themes are a property of the chunk meta the kit shipped
        // (PDv2Mgr::HasTheme), the panel's slider is already bounded by the
        // same fact, and a hand-written list here was one more place a third
        // theme would have had to be remembered in.
        //
        // Round F / F3-B is exactly that third theme, and it needed no code
        // change here at all - only the NAMES below, which are prose for a GM
        // and not a gate. Theme 3 becomes generatable on the day the kit ships
        // forest chunk meta (22000+) and not one commit earlier, which is the
        // whole point of asking the kit.
        int const themeOverride = static_cast<int>(themeArg.value_or(0));
        if (themeOverride != 0 && !sPDv2Mgr->HasTheme(themeOverride))
        {
            handler->PSendSysMessage("pdungeon v2: theme {} is unknown - this kit carries "
                                     "1..{} (1 = mine, 2 = city, 3 = forest; 0 or no "
                                     "argument follows the server config).",
                                     themeOverride, sPDv2Mgr->ThemeMax());
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

    // Round E / R1 (spec D15). Sets the INVOKING account's difficulty cap.
    //
    // A TEST TOOL, and the only writer in the module that may LOWER a cap.
    // Gameplay raises it exactly one way - finishing a run, through
    // PDv2Mgr::RaiseDiffCap, which is a ratchet - so testing the bound the
    // ratchet creates ("does the slider really stop at 30?", "does a capped
    // account still get re-clamped?") needs a door that turns both ways. That
    // door is GM-only and must never be wired to anything a player can reach.
    //
    // The account is the CALLER's, not a named one: a cap is per account, and
    // a `.pdungeon v2 cap 30 <someone else>` would need a target resolver, an
    // online check and a push to a session this handler does not have. The
    // caller can log in as the account they want to test.
    static bool HandleV2CapCommand(ChatHandler* handler, Optional<uint32> capArg)
    {
        if (!RequireEnabled(handler))
        {
            return true;
        }

        uint32 const accountId = AccountOf(handler);
        if (!accountId)
        {
            handler->SendSysMessage("pdungeon v2: no account behind this command.");
            return true;
        }

        // The usage line doubles as the help string, and it says "test tool"
        // out loud on purpose: a GM who reads only this line still learns that
        // the number they are about to type is not something a player earned.
        if (!capArg)
        {
            PDv2AccountState const account = sPDv2Mgr->GetAccountState(accountId);
            handler->PSendSysMessage("pdungeon v2: cap now {} (dial {}).",
                                     account.diffCap, account.cfgDifficulty);
            handler->PSendSysMessage("Usage: .pdungeon v2 cap <{}-{}> - TEST TOOL: sets this "
                                     "account's difficulty cap by hand and MAY LOWER it. "
                                     "Play raises it only by finishing runs.",
                                     PD_GAME_DIFF_MIN, PD_GAME_DIFF_MAX);
            return true;
        }

        // Clamped, not refused: 0 and 500 are both a GM saying "floor" and
        // "ceiling", and GameClampDiff is the same clamp every other door into
        // this dial goes through.
        int const cap = sPDv2Mgr->SetDiffCap(accountId, static_cast<int>(*capArg));

        // The echo IS half the command: `c.diffMax` bounds the panel's slider,
        // so without a fresh C payload the GM sets a cap and watches a slider
        // that still stops where it used to. Harmless when the panel was never
        // opened - SendCfg addresses a client that simply ignores it.
        if (Player* player = handler->GetPlayer())
        {
            sPDv2UILink->SendCfg(player);
        }

        PDv2AccountState const account = sPDv2Mgr->GetAccountState(accountId);
        handler->PSendSysMessage("pdungeon v2: cap now {} (dial {}).",
                                 cap, account.cfgDifficulty);
        // Said only when it is true, and it is the one surprise this command
        // has: the dial is NOT retuned here. It is re-clamped the next time it
        // is written (SetAccountCfg, i.e. the panel's next change) or read
        // from the row (LoadAccountState), and a run already spawned keeps the
        // difficulty it froze either way.
        if (account.cfgDifficulty > cap)
        {
            handler->PSendSysMessage("pdungeon v2: the dial is still {} - it is re-clamped to "
                                     "the cap on the next settings change or reload.",
                                     account.cfgDifficulty);
        }
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
        // Round F / D5b: a theme may own its own region (V2.Theme<N>.OriginBX/BY),
        // so the global origin above is only the city's. List the themes that
        // differ - a mine or forest run really sits on those blocks.
        {
            std::string themed;
            for (int theme = 1; theme <= 9; ++theme)
            {
                int bx = 0, by = 0;
                sPDv2Mgr->ThemeOriginBlock(theme, bx, by);
                if (bx != cfg.originBX || by != cfg.originBY)
                    themed += Acore::StringFormat("{}theme {} ({},{})", themed.empty() ? "" : ", ", theme, bx, by);
            }
            handler->PSendSysMessage("pdungeon v2: theme origins: {}",
                                     themed.empty() ? std::string("all themes share the global origin") : themed);
        }
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
        // Round E / L1, and the same failure class a fourth time: a pool at 0
        // means mod_pdungeon_loot_pools.sql never landed, and the run's
        // chests, bosses and final cache pay nothing at all - which looks
        // like bad luck in play and like nothing whatsoever in the log.
        handler->PSendSysMessage("loot: {}", sPDv2LootMgr->Describe());
        handler->PSendSysMessage("pdungeon v2: {}",
                                 sPDClientLink->DebugLine(AccountOf(handler)));
        // The panel's side of the same conversation: whether this account's
        // client has ever opened the UI, and what it last asked to change.
        handler->PSendSysMessage("pdungeon v2: {}",
                                 sPDv2UILink->DebugLine(AccountOf(handler)));

        // Round E / R1. The caller's own progression, and the reason it is
        // ABOVE the plan section: everything below returns early when the
        // account has no stored layout, and the cap is exactly what an
        // operator wants to read after `.pdungeon v2 cap` - a run that has not
        // been generated yet is no reason to hide it. `dial` and `cap`
        // together, because the pair is the whole rule: the dial is what the
        // next run is played at, the cap is the highest the dial may be set
        // to, and a dial ABOVE the cap is the one state worth seeing (a
        // hand-set cap, re-clamped on the next write - see `.pdungeon v2 cap`).
        // The unlock keys are printed beside them so the effect of the next
        // clear can be read off this one line.
        PDv2AccountState const account = sPDv2Mgr->GetAccountState(AccountOf(handler));
        handler->PSendSysMessage("pdungeon v2: dlvl {} | dial {} | cap {} | "
                                 "unlock +{} clean / +{} after a death",
                                 account.dlvl, account.cfgDifficulty, account.diffCap,
                                 cfg.capCleanUnlock, cfg.capDeathUnlock);

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
