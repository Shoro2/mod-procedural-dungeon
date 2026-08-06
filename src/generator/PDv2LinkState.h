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

#ifndef MOD_PDUNGEON_V2_LINK_STATE_H
#define MOD_PDUNGEON_V2_LINK_STATE_H

#include <cstdint>
#include <string>
#include <unordered_map>

// The client-link handshake, per account, engine-free.
//
// Entering map 760 without a composed layout in the client CRASHES the client
// (measured 2026-08-06) - the entry gate exists to make that impossible, which
// makes this state machine safety logic, not bookkeeping. It is engine-free so
// the whole verdict matrix can be proven by the harness before a build; the
// engine glue (PDClientLink) only feeds it messages and asks for verdicts.
//
// Inputs, as relayed by the FLProcDungeon addon over the addon channel:
//   VER <n>      the DLL bridge version (0 = addon present, DLL absent/silent)
//   ACK <text>   the DLL's FLPD_ACK global whenever it changes; the values
//                that matter are READY:<seq> and NAK[:<detail>]. RECV:<seq>
//                acknowledges receipt only and never satisfies the gate.
//
// A note on trust: every input here is client-supplied and therefore
// spoofable. That is acceptable by design - the gate is crash PROTECTION for
// the player's own client, not anti-cheat; a player who fakes READY without
// the DLL crashes nobody but themselves.
namespace PDungeon
{
    struct LinkClient
    {
        int         dllVersion = -1;    // -1 = addon never reported
        uint32_t    pendingSeq = 0;     // last manifest seq pushed; 0 = none
        uint64_t    pushMs = 0;         // when pendingSeq went out
        bool        repushSpent = false;
        uint32_t    readySeq = 0;       // last READY:<seq> the client relayed
        std::string lastNak;            // last NAK detail; empty when none
    };

    enum class LinkVerdict : uint8_t
    {
        Ready,          // READY for exactly the pending seq - safe to enter
        NoAddon,        // the addon never spoke - AIO missing or not loaded
        NoDll,          // addon reports version 0 - DLL absent or still silent
        DllTooOld,      // version below the configured requirement
        NothingPushed,  // no manifest was ever pushed for this account
        AwaitingAck,    // pushed, no READY yet
        Nak             // the DLL rejected the manifest
    };

    // Pure state, no locking: the engine wrapper serialises access, and the
    // harness drives it single-threaded.
    class LinkState
    {
    public:
        // A version report also INVALIDATES any earlier readiness: the addon
        // re-reports after every loading screen and every injection, so a VER
        // means "the client (re)initialized" - and a client restart empties
        // the DLL's composed slots while the old READY would still satisfy
        // the gate. Stale readiness here is the crash the gate exists to
        // prevent, so it is dropped on every report and the caller re-pushes.
        void ReportVersion(uint32_t accountId, int version);

        // Allocates the next manifest seq for a push and arms the timeout /
        // re-push accounting. Seqs are globally monotonic and never 0, so a
        // stale READY can never match a fresh push by accident.
        uint32_t BeginPush(uint32_t accountId, uint64_t nowMs);

        void ReportAck(uint32_t accountId, std::string const& ack);

        // True exactly once per push, when a re-push is warranted: the client
        // NAKed, or the ack timed out. The caller re-pushes; a second failure
        // stays failed until the next BeginPush.
        bool ShouldRepush(uint32_t accountId, uint64_t nowMs, uint64_t timeoutMs);

        LinkVerdict Verdict(uint32_t accountId, int requiredVersion) const;

        LinkClient const* Get(uint32_t accountId) const;

        static char const* Describe(LinkVerdict v);

    private:
        std::unordered_map<uint32_t, LinkClient> _clients;
        uint32_t _seqCounter = 0;
    };
}

#endif
