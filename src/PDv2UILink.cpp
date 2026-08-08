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

#include "PDv2UILink.h"

#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "PDClientLink.h"
#include "PDDefines.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "generator/PDBlockPlan.h"
#include "generator/PDv2GameMath.h"
#include "generator/PDv2WorldMath.h"

#include <sstream>

namespace PDungeon
{
    namespace
    {
        // Its own prefix, not PDClientLink's: a manifest and a panel frame must
        // never be able to arrive at each other's parser.
        char const* const PREFIX_UI_DOWN = "FLPDU";

        // v1 ships ONE pack band - the imported stock is native level 80 - so
        // the band row would be a slider with a single legal position. The
        // panel builds it anyway and hides it on this flag, which is what lets
        // a future multi-band pack set light the row up by flipping a server
        // constant instead of shipping new client code. The band LIMITS travel
        // with it in the C payload for exactly that reason.
        constexpr int PD_UI_BAND_LOCKED = 1;

        bool BandRowLocked()
        {
            return PD_UI_BAND_LOCKED != 0;
        }

        // +2 yards so the arrival is above the floor plane rather than in it.
        // The server has no height data for this map, so nothing would catch a
        // player placed below it.
        constexpr float PD_UI_ENTRY_LIFT_YD = 2.0f;

        // The panel colours its link line on "code 0 means go" and reads
        // nothing else out of the verdict. Pin the contract here rather than
        // leave it to whoever next reorders the enum: this turns a silent
        // client-side lie into a build failure.
        static_assert(static_cast<int>(LinkVerdict::Ready) == 0,
                      "the UI verdict code contract is 'ready == 0'");

        uint32_t AccountOf(Player* player)
        {
            return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        }

        uint64_t NowMs()
        {
            return static_cast<uint64_t>(GameTime::GetGameTimeMS().count());
        }

        // The addon channel splits a message at its FIRST tab, and the client
        // parses our payloads by whitespace - so a stray tab would eat the
        // prefix and a stray newline would smear a field. Server-authored text
        // is clean today; this makes it stay clean when someone adds a string.
        std::string Sanitize(std::string text)
        {
            for (char& c : text)
            {
                if (c == '\t' || c == '\n' || c == '\r')
                {
                    c = ' ';
                }
            }
            return text;
        }

        // Strict on purpose: the panel is untrusted input, so no exceptions, no
        // partial parses ("12abc" is not 12) and no silent overflow. A verb the
        // server cannot read whole is a verb it does not act on.
        bool ParseInt(std::string const& text, int& out)
        {
            if (text.empty() || text.size() > 11)
            {
                return false;
            }

            size_t i = 0;
            bool negative = false;
            if (text[0] == '-')
            {
                negative = true;
                i = 1;
                if (text.size() == 1)
                {
                    return false;
                }
            }

            int64_t value = 0;
            for (; i < text.size(); ++i)
            {
                if (text[i] < '0' || text[i] > '9')
                {
                    return false;
                }
                value = value * 10 + (text[i] - '0');
                if (value > 2000000000)
                {
                    return false;
                }
            }

            out = static_cast<int>(negative ? -value : value);
            return true;
        }

        // The PDv2 instance the player is standing in, or nullptr anywhere else.
        PDv2InstanceScript* ScriptFor(Player* player)
        {
            if (!player || !player->IsInWorld())
            {
                return nullptr;
            }
            Map* map = player->GetMap();
            if (!map || map->GetId() != sPDv2Mgr->GetConfig().mapId)
            {
                return nullptr;
            }
            InstanceMap* instanceMap = map->ToInstanceMap();
            return instanceMap
                       ? dynamic_cast<PDv2InstanceScript*>(instanceMap->GetInstanceScript())
                       : nullptr;
        }

        // Whose dungeon the player is looking at. Inside a run that is the
        // INSTANCE's owner, not the viewer: an instance belongs to the account
        // that first walked into it, and a guest in a friend's run has to see
        // the map under their feet rather than their own stored one.
        uint32_t PlanOwnerFor(Player* player)
        {
            if (PDv2InstanceScript* script = ScriptFor(player))
            {
                if (uint32_t const owner = script->GetAccountId())
                {
                    return owner;
                }
            }
            return AccountOf(player);
        }

        // The plan's bounding box in block coordinates. This is the frame the
        // whole map protocol lives in, and it is deliberately the SAME origin
        // BuildWalkGrid uses (PDv2WalkGrid.cpp:62-63) - so the player dot and
        // the blocks under it are placed by one definition, not two.
        void PlanBounds(BlockPlan const& plan, int& minBX, int& minBY, int& w, int& h)
        {
            minBX = 0;
            minBY = 0;
            w = 0;
            h = 0;
            if (plan.blocks.empty())
            {
                return;
            }

            int maxBX = plan.blocks[0].bx;
            int maxBY = plan.blocks[0].by;
            minBX = maxBX;
            minBY = maxBY;
            for (PlacedBlock const& b : plan.blocks)
            {
                minBX = b.bx < minBX ? b.bx : minBX;
                minBY = b.by < minBY ? b.by : minBY;
                maxBX = b.bx > maxBX ? b.bx : maxBX;
                maxBY = b.by > maxBY ? b.by : maxBY;
            }
            w = maxBX - minBX + 1;
            h = maxBY - minBY + 1;
        }

        // One letter per block, because the client only ever colours by it.
        char RoleChar(BlockRole role)
        {
            switch (role)
            {
                case BlockRole::RoomEntrance: return 'E';
                case BlockRole::RoomBoss:     return 'B';
                case BlockRole::Room:         return 'R';
                default:                      return 'c';   // every corridor variant
            }
        }
    }

    PDv2GenOutcome PDv2DoGenerate(Player* player, uint32_t seed, BlockPlan* outPlan)
    {
        PDv2GenOutcome outcome;

        if (!sPDv2Mgr->IsEnabled())
        {
            outcome.error = "the procedural dungeon is disabled on this server";
            return outcome;
        }

        uint32_t const accountId = AccountOf(player);
        if (!accountId)
        {
            outcome.error = "needs a logged-in character (layouts are per account)";
            return outcome;
        }

        uint32_t const wanted = seed ? seed : urand(1, 0x7FFFFFFE);

        BlockPlan local;
        BlockPlan& plan = outPlan ? *outPlan : local;
        if (!sPDv2Mgr->GeneratePlan(accountId, wanted, plan))
        {
            outcome.error = "generation failed for seed " + std::to_string(wanted);
            return outcome;
        }

        outcome.ok = true;
        outcome.seed = plan.effectiveSeed;
        outcome.blocks = static_cast<uint32_t>(plan.blocks.size());
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId >= 0)
            {
                ++outcome.rooms;
            }
        }

        // The push is part of generating, not an extra step a caller may
        // forget: a layout the client never received is a layout the gate will
        // refuse, and the player would have no way to tell why.
        outcome.pushed = sPDClientLink->PushManifest(player, outcome.pushError);
        return outcome;
    }

    PDv2EnterOutcome PDv2DoEnter(Player* player, bool skipGate)
    {
        PDv2EnterOutcome outcome;

        if (!sPDv2Mgr->IsEnabled())
        {
            outcome.error = "the procedural dungeon is disabled on this server";
            return outcome;
        }

        uint32_t const accountId = AccountOf(player);
        auto const plan = accountId ? sPDv2Mgr->GetPlan(accountId) : nullptr;
        if (!plan)
        {
            outcome.error = "no layout for this account yet - generate one first";
            return outcome;
        }

        if (!skipGate)
        {
            std::string whyNot;
            if (!sPDClientLink->MayEnter(player, whyNot))
            {
                outcome.error = whyNot;
                return outcome;
            }
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!sPDv2Mgr->EntranceWorldPos(*plan, x, y, z))
        {
            outcome.error = "the stored layout has no entrance block";
            return outcome;
        }

        // TELE_TO_GM_MODE is the FORCED path's flag and only its own: it makes
        // TeleportTo skip MapMgr::PlayerCannotEnter (Player.cpp:1515), which is
        // exactly what `force` means and exactly what a normal entry must not
        // do - that check is where the core's own instance rules live, and it
        // re-runs this module's gate through OnPlayerCanEnterMap. Running the
        // gate twice is free: MayEnter only has side effects when it says no.
        uint32 const mapId = sPDv2Mgr->GetConfig().mapId;
        if (!player->TeleportTo(mapId, x, y, z + PD_UI_ENTRY_LIFT_YD, 0.0f,
                                skipGate ? TELE_TO_GM_MODE : 0))
        {
            outcome.error = "the teleport to map " + std::to_string(mapId) +
                            " failed - check the instance_template and map_dbc rows";
            return outcome;
        }

        outcome.ok = true;
        outcome.x = x;
        outcome.y = y;
        outcome.z = z + PD_UI_ENTRY_LIFT_YD;
        return outcome;
    }

    PDv2UILink* PDv2UILink::instance()
    {
        static PDv2UILink link;
        return &link;
    }

    bool PDv2UILink::HudEnabled(uint32_t accountId)
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _clients.find(accountId);
        return it == _clients.end() || !it->second.hudOff;
    }

    void PDv2UILink::OnLinkStateChanged(Player* player)
    {
        uint32_t const accountId = AccountOf(player);
        if (!accountId)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(_lock);
            auto it = _clients.find(accountId);
            if (it == _clients.end() || !it->second.helloMs)
            {
                return;     // no panel this session - nothing is listening
            }
        }
        SendCfg(player);
    }

    void PDv2UILink::ForgetAccount(uint32_t accountId)
    {
        std::lock_guard<std::mutex> guard(_lock);
        _clients.erase(accountId);
    }

    void PDv2UILink::SendCfg(Player* player)
    {
        uint32_t const accountId = AccountOf(player);
        if (!accountId)
        {
            return;
        }

        PDv2Config const& cfg = sPDv2Mgr->GetConfig();
        PDv2AccountState const account = sPDv2Mgr->GetAccountState(accountId);
        int const dlvl = static_cast<int>(account.dlvl);
        LinkVerdict const verdict = sPDClientLink->CurrentVerdict(accountId);

        // What the account's CURRENT layout actually contains - distinct from
        // the cfg_* knobs, which only shape the NEXT roll. A restored plan can
        // be larger than today's band allows (its gen inputs are frozen by
        // design), and the first in-game test proved a panel that shows only
        // the next roll's bounds reads as a bug when the live dungeon differs.
        int curRooms = 0, curBoss = 0;
        if (auto const plan = sPDv2Mgr->GetPlan(accountId))
        {
            for (PlacedBlock const& b : plan->blocks)
            {
                if (b.roomId < 0 || b.role == BlockRole::RoomEntrance)
                {
                    continue;
                }
                ++curRooms;
                if (b.role == BlockRole::RoomBoss)
                {
                    ++curBoss;
                }
            }
        }

        // EVERY bound on this line is computed here. The panel is not allowed
        // to know that rooms start at 3, that difficulty moves in quarters or
        // what the loot multiplier is made of - it is told, every time.
        std::ostringstream out;
        out << "C " << account.dlvl
            << ' ' << account.dxp
            << ' ' << cfg.xpPerDlvl
            << ' ' << cfg.xpPerRoom
            << ' ' << account.cfgRooms
            << ' ' << PD_GAME_ROOMS_MIN
            << ' ' << GameRoomsCap(dlvl)
            << ' ' << account.cfgDiffX100
            << ' ' << GameDiffMinX100()
            << ' ' << GameDiffMaxX100(dlvl)
            << ' ' << PD_GAME_DIFF_STEP_X100
            << ' ' << account.cfgCasterPct
            << ' ' << PD_GAME_CASTER_PCT_MIN
            << ' ' << PD_GAME_CASTER_PCT_MAX
            << ' ' << account.cfgBandMin
            << ' ' << PD_GAME_BAND_MIN
            << ' ' << PD_GAME_BAND_MAX
            << ' ' << PD_GAME_BAND_STEP
            << ' ' << PD_UI_BAND_LOCKED
            << ' ' << GameLootMultX100(account.cfgDiffX100, account.cfgCasterPct)
            << ' ' << curRooms
            << ' ' << curBoss
            << ' ' << static_cast<int>(verdict)
            << ' ' << Sanitize(LinkState::Describe(verdict));

        SendAddonWhisper(player, PREFIX_UI_DOWN, out.str());
    }

    void PDv2UILink::SendMap(Player* player)
    {
        auto const plan = sPDv2Mgr->GetPlan(PlanOwnerFor(player));
        if (!plan || plan->blocks.empty())
        {
            return;
        }

        int minBX = 0, minBY = 0, w = 0, h = 0;
        PlanBounds(*plan, minBX, minBY, w, h);

        // PD_CELLS_PER_BLOCK travels on the wire rather than living in the
        // addon: the player dot is placed at CELL resolution on a canvas the
        // client only knows the BLOCK size of, and a Lua copy of the kit's cell
        // count is precisely the drift this module refuses to have.
        std::ostringstream out;
        out << "M " << w << ' ' << h << ' ' << PD_CELLS_PER_BLOCK;

        int ex = -1, ey = -1;
        if (plan->entranceIndex >= 0 &&
            plan->entranceIndex < static_cast<int>(plan->blocks.size()))
        {
            PlacedBlock const& e = plan->blocks[static_cast<size_t>(plan->entranceIndex)];
            ex = e.bx - minBX;
            ey = e.by - minBY;
        }
        out << ' ' << ex << ' ' << ey << ' ';

        for (PlacedBlock const& b : plan->blocks)
        {
            // The socket mask travels with every block (N=1 E=2 S=4 W=8, the
            // kit's own bit values) so the map can draw a corridor as a thin
            // bar along its REAL connections. Full-cell corridors suggested
            // connections that did not exist - adjacency on the map is not
            // adjacency in the dungeon, only a shared open socket is (operator
            // report, first in-game test 2026-08-07).
            out << (b.bx - minBX) << ',' << (b.by - minBY) << ',' << RoleChar(b.role)
                << ',' << b.socketMask << ';';
        }

        // A layout that outgrew one packet would arrive truncated and the
        // client would draw a dungeon that is not there. The room cap makes
        // this unreachable (measured worst case is a fraction of the budget);
        // it is checked anyway, because "unreachable" is a claim with a date
        // on it. Same ceiling the manifest is measured against - same wire.
        std::string const payload = out.str();
        if (payload.size() > static_cast<size_t>(PD_GAME_MANIFEST_BUDGET_B))
        {
            LOG_ERROR(PD_LOG, "PDv2 UI: map payload for account {} is {} bytes (budget {}) - "
                              "not sent; the HUD map stays empty",
                      PlanOwnerFor(player), uint32(payload.size()), PD_GAME_MANIFEST_BUDGET_B);
            return;
        }

        SendAddonWhisper(player, PREFIX_UI_DOWN, payload);
    }

    void PDv2UILink::SendRunTick(Player* player)
    {
        uint32_t const accountId = AccountOf(player);
        if (!accountId || !HudEnabled(accountId))
        {
            return;
        }

        PDv2InstanceScript* script = ScriptFor(player);
        if (!script)
        {
            return;
        }

        PDv2RunState const& run = script->GetRunState();

        // The dot, in the map payload's own frame. -1 means "do not draw":
        // there is no plan, or the player walked outside its bounding box
        // (which on this map means they are falling, and the fall catcher is
        // about to move them anyway).
        int px = -1, py = -1;
        if (auto const plan = sPDv2Mgr->GetPlan(script->GetAccountId()))
        {
            int minBX = 0, minBY = 0, w = 0, h = 0;
            PlanBounds(*plan, minBX, minBY, w, h);

            int gcx = 0, gcy = 0;
            WorldToCell(player->GetPositionX(), player->GetPositionY(), gcx, gcy);
            int const cx = gcx - minBX * PD_CELLS_PER_BLOCK;
            int const cy = gcy - minBY * PD_CELLS_PER_BLOCK;
            if (cx >= 0 && cy >= 0 &&
                cx < w * PD_CELLS_PER_BLOCK && cy < h * PD_CELLS_PER_BLOCK)
            {
                px = cx;
                py = cy;
            }
        }

        int const state = run.complete ? 2 : (run.started ? 1 : 0);

        std::ostringstream out;
        out << "R " << run.elapsedSec
            << ' ' << run.killed
            << ' ' << run.total
            << ' ' << static_cast<uint32>(run.bossKilled)
            << ' ' << static_cast<uint32>(run.bossTotal)
            << ' ' << static_cast<uint32>(run.roomsCleared)
            << ' ' << static_cast<uint32>(run.roomsTotal)
            << ' ' << px
            << ' ' << py
            << ' ' << state;

        SendAddonWhisper(player, PREFIX_UI_DOWN, out.str());
    }

    void PDv2UILink::SendNotice(Player* player, std::string const& text)
    {
        SendAddonWhisper(player, PREFIX_UI_DOWN, "N " + Sanitize(text));
    }

    void PDv2UILink::SendEnd(Map* map, PDv2RunReward const& reward)
    {
        if (!map)
        {
            return;
        }

        std::string const payload = "E " + std::to_string(reward.dxpGained) + ' ' +
                                    std::to_string(reward.newDlvl) + ' ' +
                                    (reward.leveledUp ? "1" : "0");

        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            SendAddonWhisper(it->GetSource(), PREFIX_UI_DOWN, payload);
        }
    }

    void PDv2UILink::OnInstanceTick(PDv2InstanceScript* script)
    {
        if (!script || !script->instance)
        {
            return;
        }

        // Consumed unconditionally, and first: the flag is an EDGE, and one
        // left standing would make the next second report a change that has
        // already been on the wire.
        bool const dirty = script->ConsumeRunDirty();
        PDv2RunState const& run = script->GetRunState();

        // A run that has not started and a run that is over both have a frozen
        // clock. With no counter change there is nothing new to say, so the
        // wire stays quiet - the last frame sent is still true.
        if (!dirty && (!run.started || run.complete))
        {
            return;
        }

        Map::PlayerList const& players = script->instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            SendRunTick(it->GetSource());
        }
    }

    void PDv2UILink::HandleClientVerb(Player* player, std::string const& body)
    {
        uint32_t const accountId = AccountOf(player);
        if (!accountId)
        {
            return;
        }

        if (body == "HELLO")
        {
            {
                std::lock_guard<std::mutex> guard(_lock);
                _clients[accountId].helloMs = NowMs();
            }

            // The whole point of HELLO: one round trip restores everything a
            // relog or a /reload lost. Outside the dungeon that is the panel;
            // inside it is also the map and the run frame, which is the gap
            // the dungeon-challenge HUD never closed.
            SendCfg(player);
            if (player->GetMapId() == sPDv2Mgr->GetConfig().mapId)
            {
                SendMap(player);
                SendRunTick(player);
            }
            return;
        }

        if (body.compare(0, 4, "SET ") == 0)
        {
            std::string const rest = body.substr(4);
            size_t const split = rest.find(' ');
            if (split == std::string::npos)
            {
                LOG_DEBUG(PD_LOG, "PDv2 UI: account {} sent a SET with no value ('{}')",
                          accountId, body);
                return;
            }

            std::string const key = rest.substr(0, split);
            int value = 0;
            if (!ParseInt(rest.substr(split + 1), value))
            {
                LOG_DEBUG(PD_LOG, "PDv2 UI: account {} sent a SET with a bad value ('{}')",
                          accountId, body);
                return;
            }

            PDv2AccountState wanted = sPDv2Mgr->GetAccountState(accountId);
            if (key == "rooms")
            {
                wanted.cfgRooms = value;
            }
            else if (key == "diff")
            {
                wanted.cfgDiffX100 = value;
            }
            else if (key == "caster")
            {
                wanted.cfgCasterPct = value;
            }
            else if (key == "band")
            {
                // Refused while the row is locked rather than clamped: a
                // setting the panel is not allowed to show is a setting no
                // panel may move, and a hostile one is still just a panel.
                if (BandRowLocked())
                {
                    LOG_DEBUG(PD_LOG, "PDv2 UI: account {} tried to set the locked mob "
                                      "level band", accountId);
                    return;
                }
                wanted.cfgBandMin = value;
            }
            else
            {
                LOG_DEBUG(PD_LOG, "PDv2 UI: account {} sent an unknown SET key ('{}')",
                          accountId, key);
                return;
            }

            // SetAccountCfg clamps EVERY field through the 01 §8 math on the
            // way in (PDv2Mgr.cpp:198-209), which is what makes an untrusted
            // panel harmless: an off-grid difficulty cannot reach an account
            // row no matter what the client typed into the wire.
            sPDv2Mgr->SetAccountCfg(accountId, wanted);
            sPDv2Mgr->SaveAccountCfg(accountId);

            {
                std::lock_guard<std::mutex> guard(_lock);
                PanelClient& client = _clients[accountId];
                client.setMs = NowMs();
                client.lastSet = key + " " + std::to_string(value);
            }

            // The echo IS the truth. The panel moved a widget optimistically;
            // this is what the server actually stored, and the widget is reset
            // from it - so a value the clamp changed visibly snaps back.
            SendCfg(player);
            return;
        }

        if (body == "GEN")
        {
            // Refused inside for the same reason the client link ignores a
            // version report inside (PDClientLink.cpp:100-107): a new layout
            // makes the DLL recompose and switch the slot the running client
            // is being served from, and the next terrain read then fails. The
            // instance script CAN rebuild on a seed change - that is what makes
            // this a policy rather than a limitation - but v1 keeps the whole
            // re-roll outside, where no client is standing on the old terrain.
            if (player->GetMapId() == sPDv2Mgr->GetConfig().mapId)
            {
                SendNotice(player, "Not while you are standing in it - leave the depths "
                                   "first, then roll a new one.");
                return;
            }

            PDv2GenOutcome const outcome = PDv2DoGenerate(player, 0, nullptr);
            if (!outcome.ok)
            {
                SendNotice(player, outcome.error);
                return;
            }

            SendCfg(player);
            SendMap(player);
            if (!outcome.pushed)
            {
                SendNotice(player, "the layout could not be sent to your client (" +
                                       outcome.pushError + ")");
            }
            return;
        }

        if (body == "ENTER")
        {
            PDv2EnterOutcome const outcome = PDv2DoEnter(player, false);
            if (!outcome.ok)
            {
                SendNotice(player, outcome.error);
            }
            // Nothing on success: the loading screen is the answer, and a push
            // aimed at one dies on the way (measured 2026-08-06).
            return;
        }

        if (body.compare(0, 4, "HUD ") == 0)
        {
            int on = 0;
            if (!ParseInt(body.substr(4), on))
            {
                LOG_DEBUG(PD_LOG, "PDv2 UI: account {} sent a bad HUD toggle ('{}')",
                          accountId, body);
                return;
            }

            std::lock_guard<std::mutex> guard(_lock);
            _clients[accountId].hudOff = on == 0;
            return;
        }

        LOG_DEBUG(PD_LOG, "PDv2 UI: account {} sent an unknown verb ('{}')", accountId, body);
    }

    std::string PDv2UILink::DebugLine(uint32_t accountId)
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _clients.find(accountId);
        if (it == _clients.end())
        {
            return "ui: no panel contact from this account yet";
        }

        PanelClient const& client = it->second;
        uint64_t const now = NowMs();
        std::string line = "ui: hello ";
        line += client.helloMs ? std::to_string((now - client.helloMs) / 1000) + "s ago"
                               : std::string("never");
        line += " | last set ";
        line += client.setMs
                    ? "'" + client.lastSet + "' " + std::to_string((now - client.setMs) / 1000) +
                          "s ago"
                    : std::string("none");
        line += " | hud ";
        line += client.hudOff ? "off" : "on";
        return line;
    }
}

// The UI link has no hooks of its own - the panel rides PDClientLink's addon
// receive hook and the HUD rides the instance script's own tick. What it does
// need is an end for its session state: the HUD toggle is deliberately NOT
// persisted, so it has to die with the login that set it, or a player would
// find their HUD missing after a relog with nothing anywhere to explain why.
class PDv2UILinkPlayerScript : public PlayerScript
{
public:
    PDv2UILinkPlayerScript() : PlayerScript("PDv2UILinkPlayerScript",
        { PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogout(Player* player) override
    {
        if (!player || !player->GetSession())
        {
            return;
        }
        sPDv2UILink->ForgetAccount(player->GetSession()->GetAccountId());
    }
};

void AddPDv2UILinkScripts()
{
    new PDv2UILinkPlayerScript();
}
