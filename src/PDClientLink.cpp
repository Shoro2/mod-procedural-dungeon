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
            {
                std::lock_guard<std::mutex> guard(_lock);
                // Also invalidates earlier readiness - see PDv2LinkState.h.
                _state.ReportVersion(accountId, version);
            }
            LOG_DEBUG(PD_LOG, "PDv2 link: account {} reports DLL version {}", accountId, version);

            // First contact of a (re)initialized, capable client: push the
            // stored dungeon right away. This is what makes a layout survive
            // relogs, client restarts and server restarts without a command.
            if (version > 0 && sPDv2Mgr->GetPlan(accountId))
            {
                std::string error;
                if (PushManifest(player, error))
                {
                    LOG_DEBUG(PD_LOG, "PDv2 link: auto-pushed the stored layout to account {}",
                              accountId);
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
          PLAYERHOOK_ON_LOGIN }) { }

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
        sPDv2Mgr->LoadPlanFromDB(player->GetSession()->GetAccountId());
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
                             MapDifficulty const* /*mapDiff*/, bool /*loginCheck*/) override
    {
        if (!sPDv2Mgr->IsEnabled() || !entry ||
            entry->MapID != sPDv2Mgr->GetConfig().mapId)
        {
            return true;
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

void AddPDClientLinkScripts()
{
    new PDClientLinkPlayerScript();
}
