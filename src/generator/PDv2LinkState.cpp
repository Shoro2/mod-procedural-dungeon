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

#include "PDv2LinkState.h"

#include <cstdlib>

namespace PDungeon
{
    namespace
    {
        // "READY:42" -> 42; anything unparseable -> 0 (and 0 never matches a
        // pending seq, because BeginPush never hands out 0).
        uint32_t SeqOf(std::string const& ack, size_t prefixLen)
        {
            if (ack.size() <= prefixLen)
            {
                return 0;
            }
            char const* text = ack.c_str() + prefixLen;
            char* end = nullptr;
            unsigned long const value = std::strtoul(text, &end, 10);
            if (end == text || (end && *end != '\0'))
            {
                return 0;
            }
            return static_cast<uint32_t>(value);
        }
    }

    void LinkState::ReportVersion(uint32_t accountId, int version)
    {
        _clients[accountId].dllVersion = version;
    }

    uint32_t LinkState::BeginPush(uint32_t accountId, uint64_t nowMs)
    {
        if (++_seqCounter == 0)
        {
            ++_seqCounter; // seq 0 means "nothing pushed" and is never issued
        }
        LinkClient& client = _clients[accountId];
        client.pendingSeq = _seqCounter;
        client.pushMs = nowMs;
        client.repushSpent = false;
        client.lastNak.clear();
        return _seqCounter;
    }

    void LinkState::ReportAck(uint32_t accountId, std::string const& ack)
    {
        LinkClient& client = _clients[accountId];

        if (ack.compare(0, 6, "READY:") == 0)
        {
            if (uint32_t const seq = SeqOf(ack, 6))
            {
                client.readySeq = seq;
                if (seq == client.pendingSeq)
                {
                    client.lastNak.clear();
                }
            }
            return;
        }

        if (ack.compare(0, 3, "NAK") == 0)
        {
            client.lastNak = ack.size() > 4 && ack[3] == ':'
                                 ? ack.substr(4)
                                 : std::string("unspecified");
            return;
        }

        // RECV:<seq> is receipt, PONG is chatter - neither is readiness, and
        // treating either as such is exactly the mistake the docs warn about.
    }

    bool LinkState::ShouldRepush(uint32_t accountId, uint64_t nowMs, uint64_t timeoutMs)
    {
        auto it = _clients.find(accountId);
        if (it == _clients.end())
        {
            return false;
        }
        LinkClient& client = it->second;
        if (!client.pendingSeq || client.repushSpent ||
            client.readySeq == client.pendingSeq)
        {
            return false;
        }
        if (client.lastNak.empty() && nowMs - client.pushMs < timeoutMs)
        {
            return false;
        }
        client.repushSpent = true;
        return true;
    }

    LinkVerdict LinkState::Verdict(uint32_t accountId, int requiredVersion) const
    {
        auto it = _clients.find(accountId);
        if (it == _clients.end() || it->second.dllVersion < 0)
        {
            return LinkVerdict::NoAddon;
        }
        LinkClient const& client = it->second;
        if (client.dllVersion == 0)
        {
            return LinkVerdict::NoDll;
        }
        if (client.dllVersion < requiredVersion)
        {
            return LinkVerdict::DllTooOld;
        }
        if (!client.pendingSeq)
        {
            return LinkVerdict::NothingPushed;
        }
        if (client.readySeq == client.pendingSeq)
        {
            return LinkVerdict::Ready;
        }
        if (!client.lastNak.empty())
        {
            return LinkVerdict::Nak;
        }
        return LinkVerdict::AwaitingAck;
    }

    LinkClient const* LinkState::Get(uint32_t accountId) const
    {
        auto it = _clients.find(accountId);
        return it == _clients.end() ? nullptr : &it->second;
    }

    char const* LinkState::Describe(LinkVerdict v)
    {
        switch (v)
        {
            case LinkVerdict::Ready:         return "ready";
            case LinkVerdict::NoAddon:       return "addon never reported";
            case LinkVerdict::NoDll:         return "no DLL (version 0)";
            case LinkVerdict::DllTooOld:     return "DLL too old";
            case LinkVerdict::NothingPushed: return "no manifest pushed";
            case LinkVerdict::AwaitingAck:   return "awaiting READY";
            case LinkVerdict::Nak:           return "client rejected the manifest";
        }
        return "unknown";
    }
}
