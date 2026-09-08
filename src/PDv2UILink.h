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

#ifndef MOD_PDUNGEON_V2_UI_LINK_H
#define MOD_PDUNGEON_V2_UI_LINK_H

#include "ObjectGuid.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>      // std::pair - the K record's {generation, count}

class Map;
class Player;

// PDv2 UI link: the generation panel and the in-run HUD, server side.
//
// THE RULE this whole file exists to enforce: C++ is the only authority. The
// client renders what arrives and sends raw user intents back - it holds no
// formula, no clamp and no constant of a server-owned value. Every band limit,
// every grid step and the loot multiplier are COMPUTED HERE (PDv2GameMath) and
// shipped as numbers, so the panel cannot disagree with the dungeon it
// describes. The cautionary tale is in-house: mod-dungeon-challenge's affix
// display hand-copied a C++ config value into Lua, showed it with an `or 10`
// fallback, and the server had been using 15 for months.
//
// Transport, same channel PDClientLink proved (see PDClientLink.h):
//
//   client -> server   SendAddonMessage("FLPD", "UI <verb> [args]", "WHISPER",
//                      self) - one verb namespace inside the existing prefix,
//                      so the two links share the receive hook and agree on
//                      nothing but where the prefix ends. ~248 bytes per
//                      client message, which every verb fits many times over
//   server -> client   addon whisper, prefix "FLPDU", payload "<kind> <fields>"
//                      C cfg | M map | K cleared rooms | R run tick |
//                      E completion | N notice
//
// Fields are only ever APPENDED to a payload, never reordered or removed: the
// addon reads what it knows and drops the tail, so a server that is ahead of a
// client (or a client of a server) still draws a true, if older, picture. R's
// three gate fields (Round C / C7) are the first use of that rule.
//
// Everything arriving from the panel is untrusted, exactly like any packet: a
// SET is clamped through the 01 §8 math before it can reach an account row,
// and a GEN is refused while the player stands in the dungeon.
namespace PDungeon
{
    struct BlockPlan;
    struct PDv2RunReward;
    class PDv2InstanceScript;

    // What `.pdungeon v2 gen` and the panel's Generate button BOTH did before
    // this existed, in two copies. One implementation, two entry points: the
    // command keeps its GM binding and its ascii dump / dev-fallback extras,
    // the UI path stays lean, and neither can drift from the other.
    struct PDv2GenOutcome
    {
        bool        ok = false;
        bool        pushed = false;     // the manifest reached the client link
        uint32_t    seed = 0;           // the plan's EFFECTIVE seed
        int         theme = 0;          // the plan's theme (frozen gen input)
        uint32_t    blocks = 0;
        uint32_t    rooms = 0;
        std::string error;              // why it failed; empty when ok
        std::string pushError;          // why the push failed; empty when pushed
    };

    struct PDv2EnterOutcome
    {
        bool  ok = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::string error;              // player-facing reason; empty when ok
    };

    // Plans a layout for the player's account, stores it and pushes it to the
    // client link. `seed` 0 rolls one. `outPlan` may be nullptr when the
    // caller does not need the plan itself.
    // themeOverride 0 = the server-configured theme (the panel path); the GM
    // command may pass 1/2 to aim a test generation at a specific kit theme.
    PDv2GenOutcome PDv2DoGenerate(Player* player, uint32_t seed, BlockPlan* outPlan,
                                  int themeOverride = 0);

    // Runs the entry gate and teleports. `skipGate` is the command's dev-only
    // `force`, and the ONLY caller that may pass true - an unready client does
    // not see void on map 760, it crashes.
    PDv2EnterOutcome PDv2DoEnter(Player* player, bool skipGate);

    class PDv2UILink
    {
    public:
        static PDv2UILink* instance();

        // `body` is the addon message with the "FLPD\tUI " prefix stripped.
        void HandleClientVerb(Player* player, std::string const& body);

        // The five pushes. Each one is a complete statement of its subject:
        // there is no delta protocol, because a client that missed a delta
        // would be wrong for ever and would never know it.
        void SendCfg(Player* player);
        void SendMap(Player* player);
        void SendRunTick(Player* player);
        void SendNotice(Player* player, std::string const& text);

        // Round C / C7. Which room blocks the party has emptied: "K bx,by;..."
        // in the M payload's own frame, the WHOLE set every time. `script` may
        // be nullptr - outside a run, and after a re-roll, the honest answer is
        // the empty set, and sending it is what wipes the last run's green off
        // a map that is about to be redrawn.
        void SendCleared(Player* player, PDv2InstanceScript const* script);

        // Completion, once per run, to everyone standing in it.
        void SendEnd(Map* map, PDv2RunReward const& reward);

        // Called by PDClientLink whenever a VER or ACK moved the link state.
        // The panel's verdict line (and the Enter button hanging off it) is
        // only ever as fresh as the last C payload - without this, a READY
        // that arrived AFTER the generate-triggered push stayed invisible
        // until the next HELLO or SET (operator report, first live test).
        // Quiet for accounts whose panel never said HELLO this session.
        void OnLinkStateChanged(Player* player);

        // Called from PDv2InstanceScript::Update's one-second block. Sends a
        // run frame to every player on the map while there is something to
        // say; a finished or unstarted run with no counter change is silent.
        void OnInstanceTick(PDv2InstanceScript* script);

        // Session state dies with the session: the HUD toggle is deliberately
        // not persisted, so it must not outlive the login that set it.
        void ForgetAccount(uint32_t accountId);

        // The character-scoped half of the same duty (Round C / C7): what this
        // client was last told about cleared rooms. Called from the same
        // logout hook, because a map keyed by GUID that nothing ever erased
        // would grow for as long as the server runs.
        void ForgetPlayer(ObjectGuid const& playerGuid);

        // One line for `.pdungeon v2 info`.
        std::string DebugLine(uint32_t accountId);

    private:
        // What the server remembers about one account's panel. Nothing here is
        // gameplay - the account row owns that - so none of it is persisted.
        struct PanelClient
        {
            uint64_t    helloMs = 0;    // last "UI HELLO", 0 = never
            uint64_t    setMs = 0;      // last accepted "UI SET", 0 = never
            std::string lastSet;        // "diff 125", for the info command
            bool        hudOff = false;
        };

        bool HudEnabled(uint32_t accountId);

        std::mutex _lock;
        std::unordered_map<uint32_t, PanelClient> _clients;

        // Round C / C7. What each client was last told, as the pair
        // {run generation, cleared-room COUNT}, so the 1 Hz tick can restate
        // the K set exactly once per room clear instead of once per second. A
        // missing entry means "never told", which sends - so dropping one is
        // how a resend is forced.
        //
        // The COUNT alone would not be enough, and the corner is real: the
        // count is _run.roomsCleared, which only counts rooms emptied BY
        // KILLS, while the K payload also carries rooms that spawned nothing.
        // A rebuild resets the count to 0, so a player whose record was
        // already 0 - told before the first room fell - would keep the
        // previous layout's green blocks until the next kill. The generation
        // is bumped by the rebuild itself, so a new run ALWAYS resends
        // (C7 Task 1 review, minor 2). It starts at 1 in the script, which is
        // why the "no script" record of {0, 0} can never collide with a real
        // one.
        //
        // Keyed by the CHARACTER, not by the account like _clients above: this
        // is a fact about one client's map rather than about a panel, the tick
        // has the Player in hand, and it is the only per-character thing the
        // link remembers - which is why it is its own map and not a field in
        // PanelClient.
        std::unordered_map<ObjectGuid, std::pair<uint32_t, uint32_t>> _clearedSent;
    };
}

#define sPDv2UILink PDungeon::PDv2UILink::instance()

#endif
