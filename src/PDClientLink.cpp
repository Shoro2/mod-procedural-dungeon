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

#include "PDClientLink.h"

#include "Chat.h"
#include "Config.h"
#include "DBCStructure.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Opcodes.h"
#include "PDDefines.h"
#include "PDv2Mgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdlib>

namespace PDungeon
{
    namespace
    {
        // Wire prefixes. Different per direction, so the client echo of its
        // own whisper can never be mistaken for a server push.
        char const* const PREFIX_UP = "FLPD\t";     // client -> server
        char const* const PREFIX_DOWN = "FLPDS";    // server -> client

        uint32_t AccountOf(Player* player)
        {
            return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        }

        uint64_t NowMs()
        {
            return static_cast<uint64_t>(GameTime::GetGameTimeMS().count());
        }

        // The same packet shape mod-ale's Player:SendAddonMessage builds - the
        // one transport into the client that is already proven on this server.
        void SendAddonWhisper(Player* player, std::string const& payload)
        {
            std::string const fullmsg = std::string(PREFIX_DOWN) + "\t" + payload;

            WorldPacket data(SMSG_MESSAGECHAT, 100);
            data << uint8(CHAT_MSG_WHISPER);
            data << int32(LANG_ADDON);
            data << player->GetGUID();
            data << uint32(0);
            data << player->GetGUID();
            data << uint32(fullmsg.length() + 1);
            data << fullmsg;
            data << uint8(0);
            player->GetSession()->SendPacket(&data);
        }
    }

    PDClientLink* PDClientLink::instance()
    {
        static PDClientLink link;
        return &link;
    }

    void PDClientLink::LoadConfig()
    {
        _requiredVersion = sConfigMgr->GetOption<int32>(
            "ProceduralDungeon.V2.RequiredDllVersion", 1);
        _ackTimeoutMs = sConfigMgr->GetOption<uint32>(
            "ProceduralDungeon.V2.AckTimeoutMs", 5000);
    }

    void PDClientLink::HandleClientMessage(Player* player, std::string const& body)
    {
        uint32_t const accountId = AccountOf(player);
        if (!accountId)
        {
            return;
        }

        if (body.compare(0, 4, "VER ") == 0)
        {
            int const version = static_cast<int>(std::strtol(body.c_str() + 4, nullptr, 10));

            // NEVER touch the link while the player is standing in the
            // dungeon. A push makes the DLL compose a fresh layout and switch
            // the slot the running client is being served from - measured
            // 2026-08-06: two pushes triggered by the dungeon's own loading
            // screen recycled the live slot and the next terrain read failed
            // (ERROR #134 CMap::LoadWdt). The client in there already has
            // exactly this layout; there is nothing to gain and a crash to
            // lose.
            if (player->GetMapId() == sPDv2Mgr->GetConfig().mapId)
            {
                LOG_DEBUG(PD_LOG, "PDv2 link: ignoring a version report from account {} "
                                  "while inside the dungeon", accountId);
                return;
            }

            {
                std::lock_guard<std::mutex> guard(_lock);
                // Also invalidates earlier readiness - see PDv2LinkState.h.
                _state.ReportVersion(accountId, version);
            }
            LOG_DEBUG(PD_LOG, "PDv2 link: account {} reports DLL version {}", accountId, version);

            // First contact of a (re)initialized client: push the stored
            // dungeon right away - DELIBERATELY even on VER 0. The DLL only
            // sets its Lua globals when it speaks, and after injection
            // nothing makes it speak until something is fed to it; the push
            // IS that kick. A capable client answers the feed (silently),
            // which sets FLPD_DLL_VERSION + FLPD_ACK, the addon relays both,
            // and the arriving VER triggers one more push that settles READY.
            // Without the DLL the feed is an inert Lua comment, no ACK ever
            // arrives, and the gate keeps refusing - exactly right.
            if (sPDv2Mgr->GetPlan(accountId))
            {
                std::string error;
                if (PushManifest(player, error))
                {
                    LOG_DEBUG(PD_LOG, "PDv2 link: auto-pushed the stored layout to account {} "
                                      "(reported version {})", accountId, version);
                }
            }
            return;
        }

        if (body.compare(0, 4, "ACK ") == 0)
        {
            std::string const ack = body.substr(4);
            std::lock_guard<std::mutex> guard(_lock);
            _state.ReportAck(accountId, ack);
            LOG_DEBUG(PD_LOG, "PDv2 link: account {} relayed ack '{}'", accountId, ack);
            return;
        }

        LOG_DEBUG(PD_LOG, "PDv2 link: account {} sent an unknown verb ('{}')", accountId, body);
    }

    bool PDClientLink::PushManifest(Player* player, std::string& error)
    {
        uint32_t const accountId = AccountOf(player);
        BlockPlan const* plan = accountId ? sPDv2Mgr->GetPlan(accountId) : nullptr;
        if (!plan)
        {
            error = "no plan stored for this account";
            return false;
        }

        uint32_t seq = 0;
        {
            std::lock_guard<std::mutex> guard(_lock);
            seq = _state.BeginPush(accountId, NowMs());
        }

        std::string const manifest = EmitManifest(*plan, static_cast<int>(seq));
        SendAddonWhisper(player, "M" + manifest);
        LOG_INFO(PD_LOG, "PDv2 link: pushed manifest seq {} ({} bytes) to account {}",
                 seq, uint32(manifest.size()), accountId);
        return true;
    }

    bool PDClientLink::MayEnter(Player* player, std::string& whyNot)
    {
        uint32_t const accountId = AccountOf(player);
        if (!accountId)
        {
            whyNot = "no session";
            return false;
        }

        LinkVerdict verdict;
        bool repush = false;
        {
            std::lock_guard<std::mutex> guard(_lock);
            verdict = _state.Verdict(accountId, _requiredVersion);
            if (verdict == LinkVerdict::AwaitingAck || verdict == LinkVerdict::Nak)
            {
                repush = _state.ShouldRepush(accountId, NowMs(), _ackTimeoutMs);
            }
        }

        switch (verdict)
        {
            case LinkVerdict::Ready:
                return true;
            case LinkVerdict::NoAddon:
                whyNot = "your client has not reported in - is AIO with the "
                         "FLProcDungeon addon running?";
                return false;
            case LinkVerdict::NoDll:
                whyNot = "FLStream.dll has not spoken - without it there is no "
                         "terrain to stand on";
                return false;
            case LinkVerdict::DllTooOld:
                whyNot = "your FLStream.dll is older than this server requires - "
                         "update the client kit";
                return false;
            case LinkVerdict::NothingPushed:
                whyNot = "no layout was sent to your client yet - generate one first";
                return false;
            case LinkVerdict::Nak:
            case LinkVerdict::AwaitingAck:
            default:
                if (repush)
                {
                    std::string error;
                    if (PushManifest(player, error))
                    {
                        whyNot = "your client had not confirmed the layout - it was "
                                 "sent again, try once more in a moment";
                        return false;
                    }
                }
                whyNot = verdict == LinkVerdict::Nak
                             ? "your client rejected the layout - regenerate it, and "
                               "update the client kit if this repeats"
                             : "your client has not confirmed the layout yet - try "
                               "again in a moment";
                return false;
        }
    }

    std::string PDClientLink::DebugLine(uint32_t accountId)
    {
        std::lock_guard<std::mutex> guard(_lock);
        LinkClient const* client = _state.Get(accountId);
        if (!client)
        {
            return "link: no contact from this account yet";
        }
        std::string line = "link: dll v";
        line += std::to_string(client->dllVersion);
        line += " | pushed seq ";
        line += std::to_string(client->pendingSeq);
        line += " | ready seq ";
        line += std::to_string(client->readySeq);
        line += " | ";
        line += LinkState::Describe(_state.Verdict(accountId, _requiredVersion));
        if (!client->lastNak.empty())
        {
            line += " (nak: " + client->lastNak + ")";
        }
        return line;
    }
}

// The two hooks that make the link work without a core patch: addon whispers
// come through OnPlayerBeforeSendChatMessage (it fires for LANG_ADDON,
// ChatHandler.cpp:364), and the entry gate rides OnPlayerCanEnterMap
// (MapMgr.cpp:164). GMs bypass the latter at MapMgr.cpp:159-160 - which is why
// `.pdungeon v2 enter` runs the same check itself before teleporting.
class PDClientLinkPlayerScript : public PlayerScript
{
public:
    PDClientLinkPlayerScript() : PlayerScript("PDClientLinkPlayerScript",
        { PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE, PLAYERHOOK_CAN_ENTER_MAP,
          PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_BEFORE_LOGOUT }) { }

    // Sends the player home BEFORE the logout save, which is the only way to
    // keep the dungeon position out of the DB row. Two earlier attempts
    // failed and both are worth remembering: a teleport in OnPlayerLogout is
    // too late (LogoutPlayer saves at WorldSession.cpp:735 and calls that
    // hook at :760), and a DirectExecute UPDATE from there RACED - it bypasses
    // the async queue the save is sitting in, lands first, and the save then
    // writes map 760 straight back over it (measured 2026-08-06).
    //
    // Here the core does the work itself: this hook runs at :626, and the
    // far-teleport it triggers is completed at :720 by the loop whose own
    // comment says it exists to "teleport player immediately for correct
    // player save". So the save at :735 already writes the homebind row.
    void OnPlayerBeforeLogout(Player* player) override
    {
        if (!sPDv2Mgr->IsEnabled() || !player ||
            player->GetMapId() != sPDv2Mgr->GetConfig().mapId)
        {
            return;
        }
        player->TeleportTo(player->m_homebindMapId, player->m_homebindX,
                           player->m_homebindY, player->m_homebindZ,
                           player->GetOrientation());
        LOG_INFO(PDungeon::PD_LOG,
                 "PDv2 link: sending {} home before the logout save - a "
                 "character saved inside map {} would crash its own client at "
                 "the character screen", player->GetName(),
                 sPDv2Mgr->GetConfig().mapId);
    }

    // A character must never be SAVED inside map 760: the client reads the
    // position from the DB at the character screen and starts loading that
    // map before the server can say anything - and without an injected DLL,
    // CMap::LoadWdt() on the composed-only map is a hard client crash
    // (measured 2026-08-06, the login-boundary's final shape). Send them
    // home before the logout save; the startup sweep in PDWorldScript covers
    // characters stranded by a crash.
    // Login restores the account's persisted layout (regenerated from its
    // stored seed); the addon's VER report that follows moments later then
    // auto-pushes it. Together those are what "your dungeon survives a
    // restart" means.
    void OnPlayerLogin(Player* player) override
    {
        if (!sPDv2Mgr->IsEnabled() || !player || !player->GetSession())
        {
            return;
        }
        uint32 const accountId = player->GetSession()->GetAccountId();
        // Progression and the cfg_* knobs first: GeneratePlan reads them to
        // decide how many rooms the account's dungeon has, so a reroll that
        // beat this load would size the dungeon off stale defaults.
        sPDv2Mgr->LoadAccountState(accountId);
        sPDv2Mgr->LoadPlanFromDB(accountId);
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& lang,
                                       std::string& msg) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || !sPDv2Mgr->IsEnabled())
        {
            return;
        }
        if (msg.compare(0, 5, "FLPD\t") != 0)
        {
            return;
        }
        sPDClientLink->HandleClientMessage(player, msg.substr(5));
    }

    bool OnPlayerCanEnterMap(Player* player, MapEntry const* entry,
                             InstanceTemplate const* /*instance*/,
                             MapDifficulty const* /*mapDiff*/, bool loginCheck) override
    {
        if (!sPDv2Mgr->IsEnabled() || !entry ||
            entry->MapID != sPDv2Mgr->GetConfig().mapId)
        {
            return true;
        }

        // A character logging in INSIDE the map is the one case the handshake
        // cannot vouch for: a READY earned by the PREVIOUS client session may
        // outlive a client restart that emptied the DLL's composed slots, and
        // the login map load happens before any addon or DLL can speak.
        // Entering blind CRASHES that client (measured 2026-08-06, third
        // shape of the same boundary). Deny every login-time check - the core
        // relocates the character to its homebind, and the normal flow
        // (auto-push, READY, enter) brings them back in.
        if (loginCheck)
        {
            LOG_INFO(PDungeon::PD_LOG,
                     "PDv2 link: relocating {} out of map {} at login - a fresh "
                     "client cannot prove readiness this early",
                     player->GetName(), entry->MapID);
            return false;
        }

        std::string whyNot;
        if (sPDClientLink->MayEnter(player, whyNot))
        {
            return true;
        }

        // Denying here is what keeps the client alive: entering this map with
        // nothing composed does not show void, it CRASHES (measured
        // 2026-08-06). This also covers login-time checks, so a character
        // parked inside the dungeon relogs to safety instead of into a crash.
        if (player->GetSession())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "The Forgotten Depths: {}", whyNot);
        }
        LOG_INFO(PDungeon::PD_LOG, "PDv2 link: denied map entry for {} ({})",
                 player->GetName(), whyNot);
        return false;
    }
};

// The last line of defence against the login crash-loop. Whatever path left a
// character SAVED on the composed-only map - a periodic save before a client
// crash, a logout teleport that never got to run because the server died too -
// this heals the DB at ACCOUNT login. The hook fires at auth
// (WorldSocket.cpp:703), before the character enum, and the update runs
// synchronously, so the client can never even SEE a character standing on map
// 760 - and a player can therefore never be trapped in a crash-on-login loop,
// no matter how their character got there.
class PDClientLinkAccountScript : public AccountScript
{
public:
    PDClientLinkAccountScript() : AccountScript("PDClientLinkAccountScript",
        { ACCOUNTHOOK_ON_ACCOUNT_LOGIN }) { }

    void OnAccountLogin(uint32 accountId) override
    {
        if (!sPDv2Mgr->IsEnabled())
        {
            return;
        }
        CharacterDatabase.DirectExecute(
            Acore::StringFormat(
                "UPDATE characters c JOIN character_homebind h ON c.guid = h.guid "
                "SET c.map = h.mapId, c.zone = h.zoneId, c.position_x = h.posX, "
                "c.position_y = h.posY, c.position_z = h.posZ, c.instance_id = 0 "
                "WHERE c.account = {} AND c.map = {}",
                accountId, sPDv2Mgr->GetConfig().mapId).c_str());
    }
};

void AddPDClientLinkScripts()
{
    new PDClientLinkPlayerScript();
    new PDClientLinkAccountScript();
}
