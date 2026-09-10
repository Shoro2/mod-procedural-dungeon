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

#ifndef MOD_PDUNGEON_V2_MGR_H
#define MOD_PDUNGEON_V2_MGR_H

#include "generator/PDBlockPlan.h"
#include "generator/PDv2DecorPlan.h"
#include "generator/PDv2GameMath.h"
#include "generator/PDv2SpawnAnchors.h"
#include "generator/PDv2WalkGrid.h"
#include "generator/PDv2WorldMath.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

// PDv2 engine glue, first slice.
//
// v1 rasterises a layout and spawns it as GameObjects. v2 does neither: the
// server decides which kit block sits where, says so in an FLPD2 manifest, and
// the client composes the terrain from that. So this manager's whole job on the
// world side is to hold a plan per account and to answer "where in the world is
// that block".
//
// What this slice deliberately does NOT do yet, so each piece can be built and
// tested before the next is written:
//   * no AIO push - the manifest is written to a file and the DLL's dev-only
//     LOAD verb picks it up. PDClientLink replaces that.
//   * no entry gate - OnPlayerCanEnterMap is not hooked, so a player without
//     the DLL would enter and see void. GM-only commands keep that contained.
//   * no creatures, no walk grid, no kill plane. The server still has no idea
//     where the floor is (`GroundZ -100000`); only the client does.
namespace PDungeon
{
    struct PDv2Config
    {
        bool        enabled = false;
        uint32_t    mapId = 760;
        float       floorZ = 50.0f;      // must match the kit's floor plane
        int         rooms = 5;
        int         bossRooms = 1;
        int         fieldBlocks = 8;
        int         originBX = 256;      // 256/8 = tile 32
        int         originBY = 256;
        int         detourChancePct = 33;  // Round B (B0b): chance per boss segment of a loop room (V2.DetourChance)
        int         branches = 2;        // Round B: pocket rooms per layout (V2.Branches)
        int         theme = 1;
        std::string manifestPath;        // where `v2 gen` writes the manifest

        // 01 §8 gameplay knobs.
        int         xpPerRoom = 10;
        int         xpPerDlvl = 100;
        int         dlvlCap = 30;
        // Trash in a NORMAL room, and trash beside the boss in a boss room.
        // Two knobs because one cannot say "rooms got fuller, boss rooms did
        // not" - which is exactly what the operator asked for on 2026-08-08.
        int         spawnsPerRoom = 5;
        int         bossRoomAdds = 2;
        int         lootBonusRollPct = 15;
        float       castRangeYd = 25.0f;
        float       aggroRangeYd = 20.0f;
        // No cast-pacing knob lives here. Every cooldown a mob has is a
        // per-spell column in pdungeon_member_spells, including the filler's
        // - one server-wide number could never say "spam the Frostbolt but
        //   not the knockback".

        // The difficulty curve, per point of the 1..100 dial. Percent of the
        // creature's own numbers, added linearly, exactly like
        // mod-dungeon-challenge's HealthMultiplierPerLevel /
        // DamageMultiplierPerLevel - the defaults ARE that module's live values
        // on this box. PDv2 owns its own keys so the two dungeons can diverge.
        int         diffHealthPctPerLevel = 5;
        int         diffDamagePctPerLevel = 2;

        // Share of a run's TRASH that wears the affixes, in percent. Default =
        // mod-dungeon-challenge's live DungeonChallenge.AffixPercentage.
        int         affixPct = 40;

        // Server-side props (torches, braziers). On by default: a dungeon
        // without them is lit by nothing at all, because the kit's terrain
        // carries no light sources. Off is for an operator hunting a GO budget
        // or a display problem - it costs nothing else, since the props are
        // decoration and no mechanic reads them.
        bool        decorEnable = true;

        // Round B / B3-B5 (2026-09-03). All eight are read live in LoadConfig
        // and none is persisted with a layout: they change what a RUN does,
        // not what a plan is, so a `.reload config` retunes the next barrier,
        // the next patroller and the next ambush without rerolling anybody's
        // dungeon.

        // B3. Share of a boss segment's planned trash that must fall before
        // the portcullis in front of that segment's boss room opens. The boss
        // room's own pack is not in the denominator (design §B3.1).
        int         barrierPct = 50;
        // The portcullis' facing, in radians, for the two lane orientations -
        // conf keys rather than constants so the operator can calibrate the
        // model against the doorway in game without a rebuild.
        float       barrierOrientNS = 0.0f;
        float       barrierOrientEW = 1.5708f;

        // B4. The patroller's health as a percent of the trash it is drawn
        // from, through SpawnTaggedMob's baseHealthOverride. Never below 100:
        // a patrol that is weaker than the pack it came from is not a threat
        // on the road, it is loot walking towards the player.
        int         patrolHealthMultPct = 300;

        // Round D / D2. Where the single file grows: one creature below
        // Size2Diff, two from it, three from Size3Diff, measured against the
        // run's frozen 1..100 difficulty. Read live like everything else here,
        // and read at SPAWN time only - a `.reload config` retunes the next
        // dungeon rather than adding a mob to a corridor a player is standing
        // in. Neither is clamped against the other: Size3Diff <= Size2Diff
        // merely makes the two-mob band empty, which is a legitimate thing for
        // an operator to type and not a mistake to refuse.
        int         patrolSize2Diff = 50;
        int         patrolSize3Diff = 75;
        // How far behind the creature in front of it a follower walks, times
        // its rank - so the file is FollowDistYd, 2x, 3x behind the leader. A
        // yard value rather than cells: MoveFollow is an engine call and its
        // range is in yards, and the lane is 16.67 yd wide, so the default 3.0
        // keeps the whole file inside it however the corridor turns.
        //
        // Round D / D2 (Task 3 review I1): read on every follow DECISION, not
        // merely on every follow ISSUE. The core's follow generator freezes its
        // range at construction, so the AI compares the live product against
        // the distance it last issued and re-issues when the two differ - which
        // is what makes `.reload config` re-space a file that is already
        // walking, exactly as the conf.dist for this key promises.
        float       patrolFollowDistYd = 3.0f;

        // Round C. The patrol diagnostics switch, and the only reason the AI
        // logs anything per leg. OFF by default and expected to stay off
        // everywhere but a run an operator is actively watching: the lines are
        // per waypoint, per movement inform and per evade, which is exactly
        // what someone hunting a patroller wants and exactly what the host
        // does not. Read live like every other V2 knob, so `.reload config`
        // arms it on a dungeon that is already being walked.
        bool        patrolDebug = false;

        // Round E / R3 (2026-09-10). The module-wide diagnostics switch, and
        // the only reason anything in PDv2 speaks per creature, per tick or
        // per client verb. OFF by default and expected to stay off everywhere
        // but a run somebody is actively watching: every line behind it is one
        // a five-room dungeon prints dozens of times, and none of them is a
        // fault - a fault names itself at WARN or ERROR and is never gated.
        //
        // Deliberately NOT the same key as patrolDebug above. That one arms
        // the patrol AI's per-leg trace, which is a different hunt at a
        // different volume: an operator chasing a spawn, a death or a client
        // verb should not have to read a corridor's movement informs to get
        // there. Read live like every other V2 knob, on the line that would
        // log, so `.reload config` both arms and disarms it mid-run.
        bool        debug = false;

        // B5. Chance per boss segment that one of its corridors is armed, how
        // many mobs the trap spawns, and the stun it opens with (0 = no stun).
        // The chance is read live and is not a layout input - BuildAmbushPlan
        // draws on its own stream. No radius: since Round C / C2 the trap
        // fires on the player standing in the corridor BLOCK, which needs no
        // tuning constant at all (PDv2InstanceScript.h, TickAmbushes).
        int         ambushChancePct = 50;
        int         ambushMobs = 4;
        uint32_t    ambushStunSpell = 20170;

        // Round E / WP5 (2026-09-10). The event room: a dead-end pocket off
        // the spine with a host who asks to be defended, one per boss segment
        // at most.
        //
        // ChancePct is a LAYOUT input, and the only one of the five that is:
        // the generator draws the pocket, so this value is read when a plan is
        // GENERATED, stored with it as pdungeon_account.gen_event_pct and read
        // back on login to rebuild the same dungeon. A `.reload config`
        // therefore reaches the NEXT generated dungeon and leaves every stored
        // one alone. Clamped 0..100 for two reasons at once: it is a percent,
        // and the column is TINYINT UNSIGNED - a typo above it would make
        // SavePlanToDB fail under strict sql_mode and lose the layout that was
        // just generated (the gen_branches lesson, LoadConfig says it again).
        int         eventChancePct = 25;

        // The other four are engine-side and read LIVE, like the ambush trio
        // above: how long one defence runs, how often it sends the next
        // attacker, how much of a wave is casters (far below the account's own
        // ratio on purpose - the wave has to CLOSE on the host, not shoot him
        // from the rim), and the Paragon XP a won defence pays. None of them
        // is part of what a layout IS, so retuning them re-arms the next event
        // instead of rerolling anybody's dungeon.
        int         eventDurationSec = 60;
        int         eventSpawnEverySec = 5;
        int         eventCasterPct = 10;
        uint32_t    eventParagonXp = 1000;

        // Round E / L2-L4 (2026-09-10). The loot half of a run: five
        // currencies, the room factor their chances are scaled by, the per-mob
        // material band, how much gear each source pays, where the gear pools
        // switch to ICC, and whether what drops fits the looter. All read live
        // like every other V2 knob and cached nowhere else, so `.reload config`
        // retunes the next kill and the next chest without disturbing the run
        // that is being walked.

        // The five currency item ids, tier 1..5. The conf is the ONLY place
        // this module names them - no PD item id is written anywhere in the
        // code - so an operator who regenerates mod_pdungeon_currency.sql at
        // other entries needs no rebuild, and mod-forgotten-talents, which
        // spends them, is pointed at the same five ids from its own keys.
        uint32_t    lootCurrencyItem[5] = { 920105, 920106, 920107, 920108, 920109 };
        // Base chance for one unit of that tier, before the room factor scales
        // it. T1-T3 are rolled per tagged mob for every player on the map, T4
        // and T5 once per looter when the final cache is opened - one array,
        // because the roll is one formula and only its call site differs.
        int         lootCurrencyChancePct[5] = { 100, 5, 1, 50, 10 };
        // The run difficulty a tier needs before it drops at all. T1-T3 sit at
        // 1, the bottom of the dial, which is the same as ungated and is why
        // those three have no conf key; only the two cache tiers are gated, and
        // those two gates are what makes a hard run worth setting up.
        int         lootCurrencyMinDiff[5] = { 1, 1, 1, 50, 75 };
        // The room factor (D8, GameRoomFactorX100): a run of lootRoomsBaseline
        // ordinary rooms pays full price, a shorter one pays its share, and
        // every room past the baseline adds lootRoomsBonusPctPerRoom percent.
        // Without it the shortest dungeon would be the most profitable one per
        // minute and nobody would ever build a long one again.
        int         lootRoomsBaseline = 10;
        int         lootRoomsBonusPctPerRoom = 1;
        // Whether the mobs that were never in the layout - event waves, respawn
        // copies - pay currency too. Off, because those mobs exist to be farmed
        // in place and would turn the currency into a faucet. Materials they do
        // always drop: mats are a crafting input, currency is progression.
        bool        lootExtraMobsDropCurrency = false;

        // Round E / WP8 (2026-09-10, operator finding 4). Whether a dungeon mob
        // keeps the item half of its OWN creature_template loot table. Off,
        // because the packs are drawn from stock entries - Shadowfang Keep,
        // Scholomance, Ahn'kahet - and their tables are those dungeons' content,
        // not this one's: what a PDv2 kill is worth is the currency, the
        // materials and, on a room boss, the injected gear. The GOLD is kept
        // whatever this key says; only the items are dropped.
        bool        lootNativeItems = false;

        // Materials, per tagged mob and per player. The chance is the whole
        // gate (0 turns materials off); the count is urand(1, max), with the
        // max running from 1 at dlvl 0 to lootMatsMaxPerMobAtCap at V2.DlvlCap,
        // so account progression shows up in the bag and not only on the sheet.
        int         lootMatsChancePct = 100;
        int         lootMatsMaxPerMobAtCap = 5;

        // Gear per source, before lootMult scales it (GameScaledCount rolls the
        // fraction, so one item at x2.50 is two plus a coin flip). Three keys
        // rather than one because the three sources are three different
        // promises: a chest is a find, a boss is a fight, and the final cache
        // is the run's payout.
        int         lootChestItems = 1;
        int         lootBossItems = 1;
        int         lootFinalItems = 1;
        // The account dlvl from which chests and the final cache draw from the
        // ICC pools instead of the heroic-5 / raid ones. The run's difficulty
        // dial deliberately does NOT move it: dlvl is the account's
        // progression, and the item level of a reward should follow that rather
        // than how hard one single run was set to.
        int         lootIccDlvl = 10;
        // Roll gear the looter's class can actually wear - armour type, weapon
        // subclass, AllowableClass/AllowableRace. On, because a pure draw from
        // a thousand items is mostly disenchant fodder; off is for an operator
        // who wants the raw pool, and the filter falls back to the unfiltered
        // pool anyway whenever it would leave nothing to roll.
        bool        lootClassFilter = true;

        // Round E / R1 (2026-09-10, spec D15). How far ONE completed run may
        // push the account's difficulty cap: the cap becomes
        // min(100, max(cap, runDifficulty + unlock)), and the unlock is the
        // clean value when nobody died in the run and the death value when
        // somebody did. Two keys rather than one factor because the whole
        // point is the gap between them - dying still progresses the account,
        // just more slowly, so a wipe-heavy clear is never a dead end.
        //
        // Measured against the run's OWN difficulty, never against the cap, so
        // farming easy runs at a high cap cannot inch it upwards. Both are
        // clamped into 0..100: 0 makes that outcome pay nothing (a legitimate
        // way to say "deaths do not unlock anything"), and no unlock can be
        // bigger than the whole dial.
        int         capDeathUnlock = 3;
        int         capCleanUnlock = 5;

        // Round E / WP6 (2026-09-10, spec D7). The respawn echoes: an ordinary
        // kill by a player who carries the Forgotten Talents "Restless Echoes"
        // node rises again as that many tagged copies. The TALENT is the
        // entitlement - a player who has not bought it gets nothing whatever
        // these two say - and this pair is only the operator's brake on it.
        //
        // Enable is the kill switch: off makes the node inert without touching
        // anyone's talent tree or refunding anything, which is what an
        // operator needs the evening a farm turns out to be one. MaxCopies is
        // the ceiling the node's own rank is capped against, clamped to 0..5:
        // the node maxes at 2 today, so the default costs nothing, and a later
        // rank - or a typo in the extension contract - cannot outgrow the
        // server's opinion of how many echoes one corpse may owe. 0 is the
        // softer switch of the two (the mechanic runs and pays nothing).
        //
        // Both read LIVE like every other engine-side V2 knob. Whether an echo
        // pays currency as well as materials is NOT here: that is the D7 half
        // V2.Loot.Currency.ExtraMobsDropCurrency already governs, for the event
        // waves and the echoes together.
        bool        respawnEnable = true;
        uint32_t    respawnMaxCopies = 2;

        // Round E / WP10 (2026-09-11). Where the C8 finale portal puts the
        // player: the NAME of an acore_world.game_tele row, resolved at click
        // time (PDExitObjects.cpp), never a set of coordinates typed into a
        // conf. A destination is world data - it moves when the world moves -
        // and the row is the one thing an operator already edits when a hub
        // moves, so the portal follows `.tele <name>` for free.
        //
        // A std::string and not a resolved GameTele const*, because the store
        // is loaded after this config is first read and `.reload config` must
        // not be able to cache a stale pointer into it. The lookup costs one
        // pass over the tele store per CLICK, which is a human pressing a
        // portal at the end of a run.
        std::string finaleTeleName = "flcapital";
    };

    // The 01 §7 gameplay half of a pdungeon_account row: progression, and the
    // cfg_* knobs the player owns. Cached beside the plans and under the same
    // lock, because both are per account and both are read from map threads.
    //
    // `loaded` distinguishes "the account has a row" from "these are defaults",
    // which is what decides whether GeneratePlan follows the account or the
    // server config.
    struct PDv2AccountState
    {
        uint32_t    dlvl = 0;
        uint32_t    dxp = 0;
        int         cfgRooms = 5;
        // The 1..100 dial (2026-08-08). cfg_diff_x100 is not read or written
        // anywhere any more - see mod_pdungeon_account_difficulty.sql for why
        // the column survives its own retirement.
        int         cfgDifficulty = PD_GAME_DIFF_DEFAULT;
        // Round E / R1 (spec D15): the ceiling cfgDifficulty may be set to.
        // Per ACCOUNT and earned by finishing runs, which is why it lives here
        // beside the knobs the player owns and not on the run - a run records
        // the difficulty it was PLAYED at, and the cap only bounds the choice
        // that started it.
        //
        // PD_GAME_DIFF_MIN rather than PD_GAME_DIFF_MAX: a state with no row
        // behind it has to read as "capped at the floor". The dial was freely
        // choosable from 2026-08-08 until Round E, so a default of 100 would
        // silently hand that back to every account whose row is missing - and
        // 1 is exactly what the column's own DEFAULT says a fresh account gets.
        int         diffCap = PD_GAME_DIFF_MIN;
        int         cfgCasterPct = PD_GAME_CASTER_PCT_DEFAULT;
        // 76 rather than the column's default of 1: 76..80 is the only band v1's
        // imported pack stock actually covers, so a fresh account that never
        // touched the setting still gets real creatures instead of an empty
        // pool. A stored row is always taken at face value.
        int         cfgBandMin = PD_GAME_BAND_MAX;
        // Round E / WP9 (cfg_stat_profile): which stat line the gear rolls
        // prefer - Off / Strength / Agility / Caster. A uint8_t and not an int
        // like its neighbours because it is a 0..3 wire value end to end (the
        // C payload, the column, PDv2LootMgr::RollGear's argument), and a
        // wider type would only invite a cast at each of those.
        //
        // The UNLOCK is not stored anywhere: it is the Forgotten Talents node
        // Discerning Eye, read live off the character's auras
        // (PD_TALENT_TAG_STATFILTER). A profile chosen and then refunded
        // therefore stops biting the moment the aura goes, and starts again if
        // the node is bought back - which is what a permission that lives on a
        // talent tree should do. Everything below this line still reads Off.
        uint8_t     cfgStatProfile = PD_STAT_PROFILE_OFF;
        std::string cfgPacks;
        bool        loaded = false;
    };

    struct PDv2RunReward
    {
        uint32_t dxpGained = 0;
        int      newDlvl = 0;
        bool     leveledUp = false;
    };

    // The geometry constants (PD_TILE_SIZE_YD and friends) moved to
    // generator/PDv2WorldMath.h so the world math that uses them is
    // harness-checkable; the include above keeps them visible here.

    // Stored with every persisted layout; bump on any change that would make
    // an old seed regenerate a DIFFERENT dungeon (generator logic, kit block
    // ids, field semantics). A mismatch at load means "reroll needed", never
    // "regenerate wrong".
    //
    // v2 (2026-08-30, Phase 2): dead-end stubs and visual alternates draw
    // from the stream, so a v1 seed no longer reproduces its stored layout.
    // Every stored dungeon rerolls once on first entry; dlvl/dxp are
    // untouched by design (layout columns update via ON DUPLICATE KEY only).
    //
    // v3 (2026-09-02, Round B): the chain generator replaces scatter + MST -
    // rooms are laid as one path through the boss rooms with pockets,
    // `gen_branches` joins the generation inputs, and `gen_loop_pct` carries
    // V2.DetourChance (B0b: loop rooms; the forward-cut mechanism the key was
    // named for is withdrawn). Every stored layout rerolls once; dlvl/dxp
    // untouched, as before.
    //
    // v4 (2026-09-10, Round E / R2): the room slider counts ORDINARY rooms and
    // the entrance is added on top (`total = max(2, rooms + bossRooms + 1)`),
    // so a stored v3 seed at the same cfg_rooms now builds one room more and
    // the whole chain draw shifts with it. Every stored layout rerolls once;
    // dlvl/dxp untouched, as before.
    //
    // v5 (2026-09-10, Round E / WP5): event pockets are a layout input. The
    // generator seats up to one dead-end event room per boss segment from
    // V2.Event.ChancePct, and `gen_event_pct` joins the stored generation
    // inputs beside gen_branches. A v4 row predates that column and carries
    // its DEFAULT 0, so it regenerates once rather than for ever: a plan
    // stored at 25 % that was generated with 0 % would silently miss its
    // events, and nothing would say so - at 0 % the coin costs no draw, so the
    // old seed still regenerates cleanly and not even the "should have been
    // bumped" error path below would fire. Every stored layout rerolls once;
    // dlvl/dxp untouched, as before.
    constexpr uint32_t PD_LAYOUT_VERSION = 5;

    class PDv2Mgr
    {
    public:
        static PDv2Mgr* instance();

        void LoadConfig();
        PDv2Config const& GetConfig() const { return _config; }
        bool IsEnabled() const { return _config.enabled; }

        // Builds a plan for `accountId`, replaces any previous one and saves
        // its generation inputs to the characters DB. Returns false when the
        // generator could not produce a valid layout.
        // themeOverride 0 follows the server config; a nonzero value is the
        // GM test path (`.pdungeon v2 gen [seed] [theme]`) and is persisted
        // like any other gen input - the theme is frozen into the layout.
        bool GeneratePlan(uint32_t accountId, uint32_t seed, BlockPlan& out,
                          int themeOverride = 0);

        // The stored plan, or an empty pointer when the account has none.
        //
        // shared_ptr rather than a raw pointer into the map, and the pointee is
        // immutable: StorePlan REPLACES the shared object instead of mutating
        // it, so a reader on a map thread keeps a complete plan even while a
        // re-roll swaps the account's current one. The raw-pointer form this
        // replaced was only safe because MapUpdate.Threads = 1 - a constraint
        // nobody should have to remember when F7 raises it (12-server-todo §5,
        // closed 2026-08-07).
        std::shared_ptr<BlockPlan const> GetPlan(uint32_t accountId) const;

        // Restores the account's persisted layout by REGENERATING it from the
        // stored seed + generation inputs (a plan is deterministic, so no
        // layout blob exists to load). Called at login; a missing row, seed 0
        // or a foreign layout_version simply means "no dungeon yet". No-op
        // when a plan is already cached for this account.
        void LoadPlanFromDB(uint32_t accountId);

        // Restores the account's dlvl/dxp and cfg_* knobs. Called at login
        // beside LoadPlanFromDB; a missing row simply means "the defaults".
        // No-op when the account is already cached.
        void LoadAccountState(uint32_t accountId);

        // A copy, because callers run on map threads and the cache is shared.
        PDv2AccountState GetAccountState(uint32_t accountId) const;

        // Replaces ONLY the cfg_* knobs in the cache, each clamped through the
        // 01 §8 math on the way in - so an out-of-band value can never reach a
        // dungeon regardless of which command or DB edit produced it. Does not
        // persist; call SaveAccountCfg for that.
        void SetAccountCfg(uint32_t accountId, PDv2AccountState const& cfg);

        // Writes ONLY the cfg_* columns, mirroring the rule SavePlanToDB
        // documents for the layout columns: settings must never clobber
        // progression, and a reroll must never clobber settings.
        void SaveAccountCfg(uint32_t accountId);

        // Round E / R1 (spec D15). Raises the account's difficulty cap towards
        // `wanted` and returns the cap now in force. `wanted` is clamped into
        // [1, 100] and the cap NEVER moves down - a run finished at a low
        // difficulty, a stale cached state or a second call with an older
        // value all have to be no-ops, so the caller may pass whatever a run
        // computed without checking it first.
        //
        // Writes `diff_cap` and nothing else, the fourth disjoint writer of
        // this row beside SavePlanToDB (layout), SaveAccountCfg (cfg_*) and
        // GrantRunReward (dlvl/dxp) - unlocking a difficulty must not speak
        // for the player's settings, the stored layout or the progression.
        int RaiseDiffCap(uint32_t accountId, int wanted);

        // Round E / R1. The TEST door beside the ratchet: sets the cap to
        // `wanted` (clamped into [1, 100]) in EITHER direction and returns it.
        //
        // Its own entry point rather than a `force` flag on RaiseDiffCap, so
        // that the ratchet has no bypass parameter a future caller could pass
        // by accident: every gameplay path calls RaiseDiffCap and cannot lower
        // a cap even by mistake, and the only caller of this one is the
        // GM-only `.pdungeon v2 cap`, which exists to test the bound the
        // gameplay path can only ever open.
        //
        // Writes `diff_cap` and nothing else, exactly like RaiseDiffCap -
        // including leaving cfgDifficulty alone. Lowering the cap under a dial
        // already set above it does NOT retune the setting here; SetAccountCfg
        // and LoadAccountState both re-clamp, so the dial is corrected the
        // next time it is written or read from the row, and a run already
        // spawned keeps the difficulty it froze.
        int SetDiffCap(uint32_t accountId, int wanted);

        // Pays out a finished run: 01 §8 dxp (difficulty-independent by
        // design), recomputes dlvl, and persists dlvl/dxp only.
        PDv2RunReward GrantRunReward(uint32_t accountId, int roomsUsed);

        // Writes the manifest for `plan` to the configured path. Until
        // PDClientLink exists this file IS the transport: the operator feeds it
        // to the DLL by hand.
        bool WriteManifest(BlockPlan const& plan, uint32_t seq, std::string& pathOut,
                           std::string& error) const;

        // World position of a point inside a block. `u` runs north to south and
        // `v` west to east, both in yards from the block's north-west corner -
        // the same FLPD-BLOCK-1 frame the kit's anchors use.
        void BlockToWorld(int bx, int by, double u, double v,
                          float& x, float& y, float& z) const;

        // Convenience: the middle of a plan's entrance block.
        bool EntranceWorldPos(BlockPlan const& plan, float& x, float& y, float& z) const;

        // Loads the kit's walk masks from `pdungeon_chunk_meta` (the SQL that
        // ships with the module, generated by 48_gen_t1_blockkit.py). Called
        // once at world startup; deliberately NOT on `.reload config`, because
        // a kit change also means a new client patch and DLL kit, and that is
        // a restart in any case. After startup the table is read-only, so map
        // threads may query it without a lock.
        void LoadChunkMeta();

        // The 8x8 walk mask for a kit chunk, or nullptr for an unknown id -
        // the shape BuildWalkGrid's WalkMaskProvider wants.
        uint8_t const* WalkMaskFor(int chunkId) const;

        size_t WalkMaskCount() const { return _walkMasks.size(); }

        // Round D / D2. The same row's patrol clearance layer, in the shape
        // BuildWalkGrid's PatrolLayerProvider wants: three 64-byte grids in the
        // walk mask's own cell order, or null pointers for a chunk that
        // publishes none. Null is not an error - a `pdungeon_chunk_meta` that
        // predates D2 has no such columns at all, and every cell is then read
        // as free, which is exactly what this module did before the layer
        // existed.
        PatrolLayers PatrolLayersFor(int chunkId) const;

        // How many chunks came with a clearance layer. 0 says the kit or the
        // database is older than D2, which is the one thing about this feature
        // an operator has to be able to tell from the outside.
        size_t PatrolLayerCount() const { return _chunkPatrol.size(); }

        // The kit's anchor points for a chunk (entry, boss, chest, spawns), in
        // the block-local FLPD-BLOCK-1 frame, or nullptr for a chunk with none
        // - which every corridor is. Loaded beside the walk masks out of the
        // same `pdungeon_chunk_meta` row, so the two can never describe
        // different kits. The decor planner keeps its props clear of these.
        std::vector<DecorAnchor> const* AnchorsFor(int chunkId) const;

        // The same row's anchors with their KINDS kept (entry, boss, chest,
        // spawns) - what the spawn veto's fallback, the loop-room chest and
        // B2's spawn placement read (B1's altar read `entry` too, until Round
        // C / C5 dropped it). nullptr for a chunk the SQL does not know.
        RoomAnchors const* RoomAnchorsFor(int chunkId) const;
        size_t RoomAnchorChunkCount() const { return _chunkRoomAnchors.size(); }

        // The chunk's structural GameObject props (fountain, cave-in, ...),
        // or nullptr - most corridors have none. Same lifetime and source as
        // the anchors: one chunk-meta row, decoded once at load.
        std::vector<KitProp> const* PropsFor(int chunkId) const;

        // Loads `pdungeon_decor_rules`, ascending id. Called once at world
        // startup beside LoadChunkMeta and read-only afterwards, for the same
        // reason: map threads read it without a lock.
        void LoadDecorRules();

        // The decor rules, in the order they were loaded (ascending id).
        // BuildDecorPlan sorts defensively all the same - see its header.
        std::vector<DecorRule> const& DecorRules() const { return _decorRules; }

        // Loads `pdungeon_critter_rules`, ascending id. Called once at world
        // startup beside LoadDecorRules and read-only afterwards, for the same
        // reason: map threads read it without a lock.
        void LoadCritterRules();

        // The critter rules, in the order they were loaded (ascending id).
        // BuildCritterPlan's determinism promise rests on that order.
        std::vector<CritterRule> const& CritterRules() const { return _critterRules; }

    private:
        void StorePlan(uint32_t accountId, BlockPlan const& plan);
        void SavePlanToDB(uint32_t accountId, BlockPlan const& plan);

        PDv2Config _config;
        mutable std::mutex _lock;
        std::unordered_map<uint32_t, std::shared_ptr<BlockPlan const>> _plans;
        std::unordered_map<uint32_t, PDv2AccountState> _accounts;
        std::unordered_map<int, std::array<uint8_t, PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK>> _walkMasks;
        // Round D / D2, one entry per chunk that HAS a layer - not per chunk,
        // so PatrolLayerCount() answers the question an operator asks. The
        // three grids live in one record because they are one measurement: a
        // clearance without its offset is a number nobody can walk to.
        struct PatrolLayerBytes
        {
            std::array<uint8_t, PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK> clear{};
            std::array<uint8_t, PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK> du{};
            std::array<uint8_t, PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK> dv{};
        };
        std::unordered_map<int, PatrolLayerBytes> _chunkPatrol;
        std::unordered_map<int, std::vector<DecorAnchor>> _chunkAnchors;
        std::unordered_map<int, RoomAnchors> _chunkRoomAnchors;
        std::unordered_map<int, std::vector<KitProp>> _chunkProps;
        std::vector<DecorRule> _decorRules;
        std::vector<CritterRule> _critterRules;
    };
}

#define sPDv2Mgr PDungeon::PDv2Mgr::instance()

namespace PDungeon
{
    // Round E / R3. The module-wide diagnostics gate, asked ON the line that
    // would log rather than cached anywhere: ProceduralDungeon.V2.Debug is
    // read live in LoadConfig, so an operator who types `.reload config`
    // mid-run starts and stops the evidence without a restart - which is the
    // whole point, because what is wanted is one pull, one spawn or one client
    // handshake and not the rest of the night.
    //
    // Mirrors PatrolDebug() in PDv2CreatureAI.cpp, which keeps its own key and
    // its own thirteen sites for the patrol AI's per-leg trace.
    //
    // Below the class and below the macro because it dereferences the
    // singleton; inline so a gated site costs a config read and a branch.
    inline bool PDv2Debug()
    {
        return sPDv2Mgr->GetConfig().debug;
    }
}

#endif
