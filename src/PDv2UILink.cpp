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
#include "PDv2PackMgr.h"
#include "PDv2TaggedAura.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "generator/PDBlockPlan.h"
#include "generator/PDv2GameMath.h"
#include "generator/PDv2WorldMath.h"

#include <sstream>
#include <utility>
#include <vector>

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
        //
        // Round E / WP5: `V` is an EVENT room and is asked BEFORE the role,
        // because an event pocket's role is plain `Room` - it is a pocket in
        // every geometric respect (PDBlockPlan.h). Reading the role alone
        // would paint it exactly like the trash pocket next to it, and the
        // one thing the map owes the player about that room is that it is
        // not one. The letter travels; the colour stays the client's.
        char RoleChar(PlacedBlock const& block)
        {
            if (block.isEvent)
            {
                return 'V';
            }

            switch (block.role)
            {
                case BlockRole::RoomEntrance: return 'E';
                case BlockRole::RoomBoss:     return 'B';
                case BlockRole::Room:         return 'R';
                default:                      return 'c';   // every corridor variant
            }
        }
    }

    PDv2GenOutcome PDv2DoGenerate(Player* player, uint32_t seed, BlockPlan* outPlan,
                                  int themeOverride)
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
        if (!sPDv2Mgr->GeneratePlan(accountId, wanted, plan, themeOverride))
        {
            outcome.error = "generation failed for seed " + std::to_string(wanted);
            return outcome;
        }

        outcome.ok = true;
        outcome.seed = plan.effectiveSeed;
        outcome.theme = plan.config.theme;
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

            // Round C / C7. A link state that MOVED means this client just
            // (re)announced itself - a relog or a /reload, either of which
            // threw the addon's cleared set away with the rest of its Lua
            // state. Forgetting what it was told is what makes the next
            // instance tick restate it. Nothing is sent from here: the map
            // payload those blocks colour has not been re-sent either, and
            // HELLO - which does send both - is one round trip behind.
            _clearedSent.erase(player->GetGUID());

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

    void PDv2UILink::ForgetPlayer(ObjectGuid const& playerGuid)
    {
        std::lock_guard<std::mutex> guard(_lock);
        _clearedSent.erase(playerGuid);
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
        //
        // Round E / R2: curRooms counts ORDINARY rooms - the entrance and the
        // boss halls are both out of it, because curBoss reports the halls on
        // the same line and the pair has to read like the two dials above it.
        // Counting the halls in both places is what made "rooms 14" on the
        // slider and "0/15 rooms" on the HUD describe one dungeon.
        int curRooms = 0, curBoss = 0;
        if (auto const plan = sPDv2Mgr->GetPlan(accountId))
        {
            for (PlacedBlock const& b : plan->blocks)
            {
                if (b.roomId < 0 || b.role == BlockRole::RoomEntrance)
                {
                    continue;
                }
                if (b.role == BlockRole::RoomBoss)
                {
                    ++curBoss;
                    continue;
                }
                ++curRooms;
            }
        }

        // EVERY bound on this line is computed here. The panel is not allowed
        // to know that rooms start at 3, that the difficulty dial runs 1..100
        // or what the loot multiplier is made of - it is told, every time.
        // The XP pair the bar is drawn from: how far INTO the current level the
        // account is, and what that level costs. Both computed here, because
        // the curve is a chain (each level 10 % dearer than the last) and the
        // panel used to divide a LIFETIME dxp by a cumulative threshold - which
        // is why it read "100 / 200 XP" right after the first level-up
        // (operator report 2026-08-08). The lifetime total does not travel any
        // more: nothing on the panel shows it.
        // ONE walk, so the remainder and the cost it is measured against are
        // always the same level's - calling GameDxpIntoLevel and GameDlvlCost
        // separately would let a stale stored dlvl pair a bar with the wrong
        // denominator.
        DlvlWalk const walk = GameWalkDlvl(account.dxp, cfg.xpPerDlvl, cfg.dlvlCap);
        uint32_t xpInto = walk.into;
        uint32_t const xpNeed = walk.cost;

        // At the cap there IS no next level, and the walk keeps counting the
        // overflow (GameDxpIntoLevel says why it must). A full bar is the
        // honest render of "you are done"; a bar reading 12000 / 1745 is not.
        if (walk.dlvl >= cfg.dlvlCap && xpInto > xpNeed)
        {
            xpInto = xpNeed;
        }

        std::ostringstream out;
        out << "C " << account.dlvl
            << ' ' << xpInto
            << ' ' << xpNeed
            << ' ' << cfg.xpPerRoom
            << ' ' << account.cfgRooms
            << ' ' << PD_GAME_ROOMS_MIN
            << ' ' << GameRoomsCap(dlvl)
            << ' ' << account.cfgDifficulty
            << ' ' << PD_GAME_DIFF_MIN
            // Round E / R1 (spec D15). The `diffMax` field is the ACCOUNT's
            // earned cap, not the dial's absolute ceiling: the slider bounds
            // itself at c.diffMax (flpdui.lua), so this is where a cap the
            // player has not earned yet stops being offered. PD_GAME_DIFF_MAX
            // is still the ceiling the cap itself is clamped to (GameClampDiff
            // in RaiseDiffCap), so a maxed account sends exactly what this
            // line used to send unconditionally.
            //
            // The server does NOT rely on this bound: SetAccountCfg re-clamps
            // whatever the panel asks for against the same cap, because a wire
            // field is a hint to a client and never a permission.
            << ' ' << account.diffCap
            << ' ' << PD_GAME_DIFF_STEP
            << ' ' << account.cfgCasterPct
            << ' ' << PD_GAME_CASTER_PCT_MIN
            << ' ' << PD_GAME_CASTER_PCT_MAX
            << ' ' << account.cfgBandMin
            << ' ' << PD_GAME_BAND_MIN
            << ' ' << PD_GAME_BAND_MAX
            << ' ' << PD_GAME_BAND_STEP
            << ' ' << PD_UI_BAND_LOCKED
            << ' ' << GameLootMultX100(account.cfgDifficulty, account.cfgCasterPct)
            << ' ' << curRooms
            << ' ' << curBoss
            // How many affixes the NEXT run would carry at the account's
            // current difficulty. Counted here, from the table the dungeon
            // actually spawns from, for the reason this whole file exists: the
            // cautionary tale in PDv2UILink.h is a hand-copied affix number in
            // Lua that disagreed with the server for months.
            << ' ' << sPDv2PackMgr->AffixCountForDifficulty(account.cfgDifficulty)
            << ' ' << static_cast<int>(verdict)
            // Round E / WP9, fields 25 and 26, and APPENDED - before the
            // free-text tail below, which has to stay last because the addon
            // reads the tail as "everything after the numbers". The rule
            // (PDv2UILink.h): fields are only ever added at the end, so an
            // older panel drops these two and draws a true, if older, picture.
            //
            // The profile the account chose, and whether the character in
            // front of us has earned the right to choose it. The unlock is a
            // live aura read and NOT a stored flag: it costs one walk of this
            // player's dummy auras per panel refresh, and it is right the
            // moment a node is bought or refunded.
            //
            // static_cast<int> is not decoration - cfgStatProfile is a uint8_t
            // and an ostream would write it as a CHARACTER.
            //
            // Like diffMax above, both are a HINT and never a permission: the
            // SET handler asks the same aura again before it lets the value
            // through, because a panel is a thing an untrusted client runs.
            << ' ' << static_cast<int>(account.cfgStatProfile)
            << ' ' << (TaggedAuraAmount(player, PD_TALENT_TAG_STATFILTER) ? 1 : 0)
            // Round F / F1, fields 27 and 28, appended under the same rule as
            // the WP9 pair above: new fields go at the END, in front of the
            // free-text tail, so an older panel simply drops them.
            //
            // The look the NEXT Generate will use (0 = follow the server's
            // V2.Theme) and the highest theme this server has art for. themeMax
            // is what bounds the panel's slider, and it is READ FROM THE KIT
            // (PDv2Mgr::ThemeMax, off the loaded chunk meta) rather than being
            // a constant in either half: a kit that ships a third theme lights
            // the third slider position up with no Lua and no C++ change, and
            // one rolled back to a single theme hides it again.
            //
            // static_cast<int> for the same reason as the profile above -
            // cfgTheme is a uint8_t and an ostream would write it as a
            // CHARACTER. A hint, never a permission: the SET handler checks
            // the id against HasTheme itself.
            << ' ' << static_cast<int>(account.cfgTheme)
            << ' ' << sPDv2Mgr->ThemeMax()
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
            out << (b.bx - minBX) << ',' << (b.by - minBY) << ',' << RoleChar(b)
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

    void PDv2UILink::SendCleared(Player* player, PDv2InstanceScript const* script)
    {
        if (!player)
        {
            return;
        }

        // Not gated on the HUD toggle, and deliberately so: this is the map's
        // second half, and SendMap is not gated either. A client with the HUD
        // hidden pays a few dozen bytes per room clear and has a true map the
        // moment it is shown again.
        auto const plan = sPDv2Mgr->GetPlan(PlanOwnerFor(player));
        if (!plan || plan->blocks.empty())
        {
            return;
        }

        int minBX = 0, minBY = 0, w = 0, h = 0;
        PlanBounds(*plan, minBX, minBY, w, h);

        std::vector<std::pair<int, int>> cleared;
        if (script)
        {
            script->ClearedRoomBlocks(cleared);
        }

        // Recorded BEFORE the budget check below, not after it: the set only
        // ever grows, so a payload that did not fit will not fit next second
        // either, and a retry would do nothing but log the same error once a
        // second for the rest of the run. One error line per room clear is the
        // honest cost of a layout that outgrew the wire.
        //
        // The pair is {run generation, emptied-room count}. Only the second
        // half moves during a run; the first is what makes a REBUILD - same
        // instance, new run, count back to 0 - a change the tick can see
        // (_clearedSent says why the count alone cannot). The EMPTIED count,
        // not the HUD's cleared one: `cleared` above carries the boss halls
        // and the HUD counter no longer does (Round E / R2).
        {
            std::lock_guard<std::mutex> guard(_lock);
            _clearedSent[player->GetGUID()] =
                script ? std::make_pair(script->RunGeneration(), script->RoomsEmptiedCount())
                       : std::make_pair(uint32_t(0), uint32_t(0));
        }

        // The SAME origin SendMap shifts its blocks by. The addon keys its
        // cleared set on the strings the M payload gave it, so a K message in
        // the plan's own frame would paint blocks the party never entered
        // (research c-research-chest-altar-finale-ui.md 4.3).
        std::ostringstream out;
        out << "K ";
        for (std::pair<int, int> const& block : cleared)
        {
            out << (block.first - minBX) << ',' << (block.second - minBY) << ';';
        }

        // Same ceiling, same wire and the same reasoning as SendMap's: the
        // room cap puts the worst case at a fraction of the budget, and
        // "unreachable" is a claim with a date on it.
        std::string const payload = out.str();
        if (payload.size() > static_cast<size_t>(PD_GAME_MANIFEST_BUDGET_B))
        {
            LOG_ERROR(PD_LOG, "PDv2 UI: cleared-room payload for account {} is {} bytes "
                              "(budget {}) - not sent; the HUD map keeps its old colours",
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

        // Round C / C7: the gate, appended AFTER `state` so an addon that
        // predates this field set simply drops the tail (there is no null
        // script to worry about here - SendRunTick returned above without one).
        // All three are zeroed when there is no gate to report, and three zeros
        // is exactly the wire's "no gate".
        //
        // This is the tail-append RULE, not a one-off: every later field set
        // goes on the END of the payload in its own group, and the addon
        // reads the groups it knows and drops the rest (ParseRun in
        // flpdui.lua). Round E / WP5 adds the second group below.
        //
        // Round E / WP8 changed WHICH gate: GateFieldsFor answers for the
        // segment THIS PLAYER is standing in, so the line keeps describing the
        // rooms they are clearing after its barrier has opened instead of
        // jumping to the next segment's 0/n (operator finding 5). That is also
        // why it is called per player rather than once per tick - two members
        // of a party in different segments now get different numbers, which is
        // the whole point. NextClosedBarrier stays for its other callers, and
        // is still the answer for a player who is not in a room.
        uint32 segPlanned = 0;
        uint32 segKilled = 0;
        uint32 segPct = 0;
        bool segOpen = false;
        script->GateFieldsFor(player, segPlanned, segKilled, segPct, segOpen);

        // Round E / WP5: the running event's clock and its host's health, the
        // second appended group. Zeroes mean "no event is running" - which is
        // also what an addon that predates this group reads, because it never
        // looks past segPct.
        //
        // The bool is dropped on purpose: EventHudFields leaves BOTH outputs
        // untouched when it answers false (PDv2InstanceScript.h), so the
        // zeros above are already the right answer and there is no second way
        // to spell "no event" on this wire.
        uint32 eventSecLeft = 0;
        uint32 eventNpcPct = 0;
        script->EventHudFields(eventSecLeft, eventNpcPct);

        // Round E / WP8: the gate's open state, the THIRD appended group and a
        // single field - behind the event pair, not beside the three gate
        // numbers it belongs to, because the rule is append-only and a field
        // inserted in the middle would silently re-number everything after it
        // for every client that has not been redeployed. 0 is what a 15-field
        // server's silence means and what an addon that predates this field
        // assumes anyway: the barrier of the segment on the line is sealed,
        // which is the only state the gate line could ever show before.
        uint32 const segOpenField = segOpen ? 1u : 0u;

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
            << ' ' << state
            << ' ' << segPlanned
            << ' ' << segKilled
            << ' ' << segPct
            << ' ' << eventSecLeft
            << ' ' << eventNpcPct
            << ' ' << segOpenField;

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

        // Round C / C7. Read once, on the map's own update thread like every
        // other line in this function - the instance script is only ever moved
        // from there (OnMobDied), so this needs no more synchronisation than
        // the run state above it does.
        //
        // Both halves of the K record, and both read outside the player loop:
        // the generation changes only when this instance rebuilds, the count
        // only when a room falls, so neither can move between two players of
        // the same tick.
        std::pair<uint32_t, uint32_t> const clearedKey(script->RunGeneration(),
                                                       script->RoomsEmptiedCount());

        Map::PlayerList const& players = script->instance->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player)
            {
                continue;
            }

            SendRunTick(player);

            // The K set is a complete statement, so it is restated only when
            // the thing it describes MOVED: once per room clear per player,
            // not once per second, plus exactly once more after every rebuild.
            // A player with no record - just walked in, or the client link just
            // reset - is told once, which is also what repaints a map that a
            // /reload emptied.
            bool stale = true;
            {
                std::lock_guard<std::mutex> guard(_lock);
                auto const sent = _clearedSent.find(player->GetGUID());
                stale = sent == _clearedSent.end() || sent->second != clearedKey;
            }
            if (stale)
            {
                SendCleared(player, script);
            }
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
            // relog or a /reload lost. Outside the dungeon that is the panel
            // and the layout it previews; inside it is also the run frame,
            // which is the gap the dungeon-challenge HUD never closed.
            //
            // The map is no longer sent only on the dungeon map (Round E / R4):
            // the gen panel draws its layout preview from the same M and K
            // payloads the HUD does, so an account with a STORED plan that
            // opens /pd anywhere else must receive them too - otherwise the
            // preview stays empty until the player presses Generate. Doing it
            // unconditionally costs nothing when there is no plan: both send
            // nothing at all in that case, which is the same silence the map
            // gate used to produce.
            SendCfg(player);
            SendMap(player);
            // Round C / C7, and immediately after the map it colours: a
            // player who walked in halfway through someone else's run has
            // no other way to learn which rooms are already empty, and the
            // tick alone would only ever tell them about the NEXT clear.
            // ScriptFor is nullptr outside the dungeon, and that is the
            // answer rather than a shortcut - no run means no cleared rooms,
            // which is exactly the uncoloured layout the preview wants.
            SendCleared(player, ScriptFor(player));
            if (player->GetMapId() == sPDv2Mgr->GetConfig().mapId)
            {
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
                // Round E / R3, and the same for the five sibling lines below:
                // what arrives here is CLIENT traffic, so its volume is not
                // this module's to bound - a stuck addon, an old panel or a
                // hostile one can send a malformed verb every frame. Behind
                // V2.Debug, where somebody debugging a panel turns one key on
                // and reads them at INFO.
                if (PDv2Debug())
                {
                    LOG_INFO(PD_LOG, "PDv2 UI: account {} sent a SET with no value ('{}')",
                             accountId, body);
                }
                return;
            }

            std::string const key = rest.substr(0, split);
            int value = 0;
            if (!ParseInt(rest.substr(split + 1), value))
            {
                if (PDv2Debug())
                {
                    LOG_INFO(PD_LOG, "PDv2 UI: account {} sent a SET with a bad value ('{}')",
                             accountId, body);
                }
                return;
            }

            PDv2AccountState wanted = sPDv2Mgr->GetAccountState(accountId);
            if (key == "rooms")
            {
                wanted.cfgRooms = value;
            }
            else if (key == "diff")
            {
                // The wire key stays "diff" through the rework: it names the
                // SETTING, not its scale, and renaming it would break every
                // panel that is already loaded in a client.
                wanted.cfgDifficulty = value;
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
                    if (PDv2Debug())
                    {
                        LOG_INFO(PD_LOG, "PDv2 UI: account {} tried to set the locked mob "
                                         "level band", accountId);
                    }
                    return;
                }
                wanted.cfgBandMin = value;
            }
            else if (key == "statprofile")
            {
                // Round E / WP9, and the band branch above is the pattern
                // exactly: a setting the panel is not allowed to show is a
                // setting no panel may move. The difference is only where the
                // permission comes from - a server constant there, the
                // player's own Forgotten Talents node here. Asked ONCE, right
                // here, and never again inside SetAccountCfg: that function
                // clamps, it does not authorise.
                if (!TaggedAuraAmount(player, PD_TALENT_TAG_STATFILTER))
                {
                    if (PDv2Debug())
                    {
                        LOG_INFO(PD_LOG, "PDv2 UI: account {} tried to set the stat profile "
                                         "without owning Discerning Eye", accountId);
                    }
                    return;
                }
                // Clamped HERE as well as in SetAccountCfg, unlike its three
                // int siblings: the field is a uint8_t, so a wire value of
                // 5000 would WRAP on the way into `wanted` and reach the clamp
                // as something else entirely. The clamp downstream is still
                // the one that matters - this is only what keeps the trip
                // through the struct honest.
                wanted.cfgStatProfile = GameClampStatProfile(value);
            }
            else if (key == "theme")
            {
                // Round F / F1 (spec D1). REFUSED rather than clamped, and the
                // band branch above is the pattern once more: a look this
                // server has no kit for is not a number to be bent into the
                // nearest legal one - silently generating a city when the
                // player asked for a mine is exactly the kind of "it worked,
                // just not like that" this module keeps out of the wire.
                //
                // 0 always passes: it is not a theme at all, it is "follow the
                // server's V2.Theme", which is what every account did before
                // this row existed and what a player who changes their mind
                // needs a way back to.
                if (value != 0 && !sPDv2Mgr->HasTheme(value))
                {
                    if (PDv2Debug())
                    {
                        LOG_INFO(PD_LOG, "PDv2 UI: account {} asked for theme {}, which this "
                                         "kit does not carry (max {})",
                                 accountId, value, sPDv2Mgr->ThemeMax());
                    }
                    return;
                }
                // The refusal above IS the range check - HasTheme only answers
                // true for one of the handful of ids the chunk meta carries -
                // so this narrowing cast into the uint8_t field cannot wrap the
                // way a raw wire value would (the trap cfg_stat_profile clamps
                // for, one branch up).
                wanted.cfgTheme = static_cast<uint8_t>(value);
            }
            else
            {
                if (PDv2Debug())
                {
                    LOG_INFO(PD_LOG, "PDv2 UI: account {} sent an unknown SET key ('{}')",
                             accountId, key);
                }
                return;
            }

            // SetAccountCfg clamps EVERY field through the 01 §8 math on the
            // way in (PDv2Mgr.cpp, SetAccountCfg), which is what makes an
            // untrusted panel harmless: a difficulty of 5000 cannot reach an
            // account row no matter what the client typed into the wire.
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

            // Round F / F1 (spec D1). The panel's own theme knob, handed in as
            // the override the GM command has always used - 0 still means
            // "follow ProceduralDungeon.V2.Theme", so an account that never
            // touched the row generates exactly what it generated before.
            //
            // Read HERE and not cached anywhere: the knob is what the account
            // row says at the moment Generate is pressed, and GeneratePlan then
            // freezes it into the layout, which is what makes the choice affect
            // the NEXT dungeon and no dungeon that already exists.
            PDv2AccountState const genState = sPDv2Mgr->GetAccountState(accountId);
            PDv2GenOutcome const outcome =
                PDv2DoGenerate(player, 0, nullptr, static_cast<int>(genState.cfgTheme));
            if (!outcome.ok)
            {
                SendNotice(player, outcome.error);
                return;
            }

            SendCfg(player);
            SendMap(player);
            // A GEN is refused inside the dungeon (above), so there is no run
            // to ask - and nullptr is not a shortcut here, it is the answer: a
            // brand new layout has no cleared rooms, and this empty set is
            // what scrubs the previous run's green off the map beside it.
            SendCleared(player, nullptr);
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
                if (PDv2Debug())
                {
                    LOG_INFO(PD_LOG, "PDv2 UI: account {} sent a bad HUD toggle ('{}')",
                             accountId, body);
                }
                return;
            }

            std::lock_guard<std::mutex> guard(_lock);
            _clients[accountId].hudOff = on == 0;
            return;
        }

        if (PDv2Debug())
        {
            LOG_INFO(PD_LOG, "PDv2 UI: account {} sent an unknown verb ('{}')", accountId, body);
        }
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
// Since Round C / C7 the same hook also drops what this character was last
// told about cleared rooms - a per-GUID map that nothing erased would grow for
// as long as the server runs, and a relogging player is re-told on HELLO.
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
        sPDv2UILink->ForgetPlayer(player->GetGUID());
    }
};

void AddPDv2UILinkScripts()
{
    new PDv2UILinkPlayerScript();
}
