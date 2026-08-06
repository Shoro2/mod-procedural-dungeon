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

#ifndef MOD_PDUNGEON_CLIENT_LINK_H
#define MOD_PDUNGEON_CLIENT_LINK_H

#include "generator/PDv2LinkState.h"

#include <mutex>
#include <string>

class Player;

// PDv2 client link: pushes manifests to the FLProcDungeon addon and gates
// entry to map 760 on the DLL's READY answer.
//
// Transport, chosen over AIO handlers on purpose: ALL protocol logic lives in
// C++ with the gate, because a Lua copy of any of it would drift (the
// dungeon-challenge affix display is the in-house cautionary tale). AIO's only
// job is delivering the addon code; the messages themselves ride the plain
// addon channel:
//
//   client -> server   SendAddonMessage("FLPD", "VER <n>" | "ACK <text>",
//                      "WHISPER", self); received in
//                      PlayerScript::OnPlayerBeforeSendChatMessage, which
//                      fires for LANG_ADDON whispers (ChatHandler.cpp:364)
//   server -> client   one SMSG_MESSAGECHAT addon whisper, prefix "FLPDS",
//                      payload "M<manifest>" - the manifest budget is 2 KB
//                      and the client accepts 2560 per packet (AIO's own
//                      number), so it always fits ONE packet, unfragmented
//
// Everything client-supplied is spoofable; see PDv2LinkState.h for why that
// is acceptable (the gate is crash protection, not anti-cheat).
namespace PDungeon
{
    class PDClientLink
    {
    public:
        static PDClientLink* instance();

        void LoadConfig();

        // `body` is the addon message with the "FLPD\t" prefix stripped.
        void HandleClientMessage(Player* player, std::string const& body);

        // Emits the account's stored plan as a fresh-seq manifest and whispers
        // it to the client. Fails when no plan is stored.
        bool PushManifest(Player* player, std::string& error);

        // The gate. On a pending-but-unanswered push this also spends the one
        // automatic re-push before saying no.
        bool MayEnter(Player* player, std::string& whyNot);

        // One line for `.pdungeon v2 info`.
        std::string DebugLine(uint32_t accountId);

    private:
        LinkState _state;
        std::mutex _lock;
        int _requiredVersion = 1;
        uint64_t _ackTimeoutMs = 5000;
    };
}

#define sPDClientLink PDungeon::PDClientLink::instance()

#endif
